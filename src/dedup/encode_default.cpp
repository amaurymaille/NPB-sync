//The queues between the pipeline stages
queue_t *deduplicate_que, *refine_que, *reorder_que, *compress_que;

// ===========================================================================
// Default version
// ============================================================================

#ifdef ENABLE_PTHREADS
void *FragmentDefault(void * targs){
    struct thread_args *args = (struct thread_args *)targs;
    size_t preloading_buffer_seek = 0;
    int qid = 0;
    int fd = args->fd;
    int i;

    ringbuffer_t send_buf;
    sequence_number_t anchorcount = 0;
    int r;
    int count = 0;

    chunk_t *temp = NULL;
    chunk_t *chunk = NULL;
    u32int * rabintab = (u32int*) malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*) malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

    r = ringbuffer_init(&send_buf, ANCHOR_DATA_PER_INSERT);
    assert(r==0);

    rf_win_dataprocess = 0;
    rabininit(rf_win_dataprocess, rabintab, rabinwintab);

    //Sanity check
    if(MAXBUF < 8 * ANCHOR_JUMP) {
        printf("WARNING: I/O buffer size is very small. Performance degraded.\n");
        fflush(NULL);
    }

    //read from input file / buffer
    while (1) {
        size_t bytes_left; //amount of data left over in last_mbuffer from previous iteration

        //Check how much data left over from previous iteration resp. create an initial chunk
        if(temp != NULL) {
            bytes_left = temp->uncompressed_data.n;
        } else {
            bytes_left = 0;
        }

        //Make sure that system supports new buffer size
        if(MAXBUF+bytes_left > SSIZE_MAX) {
            EXIT_TRACE("Input buffer size exceeds system maximum.\n");
        }
        //Allocate a new chunk and create a new memory buffer
        chunk = (chunk_t *)malloc(sizeof(chunk_t));
        if(chunk==NULL) EXIT_TRACE("Memory allocation failed.\n");
        r = mbuffer_create(&chunk->uncompressed_data, MAXBUF+bytes_left);
        if(r!=0) {
            EXIT_TRACE("Unable to initialize memory buffer.\n");
        }
        if(bytes_left > 0) {
            //FIXME: Short-circuit this if no more data available

            //"Extension" of existing buffer, copy sequence number and left over data to beginning of new buffer
            chunk->header.state = CHUNK_STATE_UNCOMPRESSED;
            chunk->sequence.l1num = temp->sequence.l1num;

            //NOTE: We cannot safely extend the current memory region because it has already been given to another thread
            memcpy(chunk->uncompressed_data.ptr, temp->uncompressed_data.ptr, temp->uncompressed_data.n);
            mbuffer_free(&temp->uncompressed_data);
            free(temp);
            temp = NULL;
        } else {
            //brand new mbuffer, increment sequence number
            chunk->header.state = CHUNK_STATE_UNCOMPRESSED;
            chunk->sequence.l1num = anchorcount;
            anchorcount++;
        }
        //Read data until buffer full
        size_t bytes_read=0;
        if(_g_data->_preloading) {
            size_t max_read = MIN(MAXBUF, args->input_file.size-preloading_buffer_seek);
            memcpy((uchar*)chunk->uncompressed_data.ptr+bytes_left, (uchar*)args->input_file.buffer+preloading_buffer_seek, max_read);
            bytes_read = max_read;
            preloading_buffer_seek += max_read;
        } else {
            while(bytes_read < MAXBUF) {
                r = read(fd, (uchar*)chunk->uncompressed_data.ptr+bytes_left+bytes_read, MAXBUF-bytes_read);
                if(r<0) switch(errno) {
                    case EAGAIN:
                        EXIT_TRACE("I/O error: No data available\n");break;
                    case EBADF:
                        EXIT_TRACE("I/O error: Invalid file descriptor\n");break;
                    case EFAULT:
                        EXIT_TRACE("I/O error: Buffer out of range\n");break;
                    case EINTR:
                        EXIT_TRACE("I/O error: Interruption\n");break;
                    case EINVAL:
                        EXIT_TRACE("I/O error: Unable to read from file descriptor\n");break;
                    case EIO:
                        EXIT_TRACE("I/O error: Generic I/O error\n");break;
                    case EISDIR:
                        EXIT_TRACE("I/O error: Cannot read from a directory\n");break;
                    default:
                        EXIT_TRACE("I/O error: Unrecognized error\n");break;
                }
                if(r==0) break;
                bytes_read += r;
            }
        }
        //No data left over from last iteration and also nothing new read in, simply clean up and quit
        if(bytes_left + bytes_read == 0) {
            mbuffer_free(&chunk->uncompressed_data);
            free(chunk);
            chunk = NULL;
            break;
        }
        //Shrink buffer to actual size
        if(bytes_left+bytes_read < chunk->uncompressed_data.n) {
            r = mbuffer_realloc(&chunk->uncompressed_data, bytes_left+bytes_read);
            assert(r == 0);
        }
        //Check whether any new data was read in, enqueue last chunk if not
        if(bytes_read == 0) {
            //put it into send buffer
            r = ringbuffer_insert(&send_buf, chunk);
            ++count;
            assert(r==0);
            //NOTE: No need to empty a full send_buf, we will break now and pass everything on to the queue
            break;
        }
        //partition input block into large, coarse-granular chunks
        int split;
        do {
            split = 0;
            //Try to split the buffer at least ANCHOR_JUMP bytes away from its beginning
            if(ANCHOR_JUMP < chunk->uncompressed_data.n) {
                int offset = rabinseg((uchar*)chunk->uncompressed_data.ptr + ANCHOR_JUMP, chunk->uncompressed_data.n - ANCHOR_JUMP, rf_win_dataprocess, rabintab, rabinwintab);
                //Did we find a split location?
                if(offset == 0) {
                    //Split found at the very beginning of the buffer (should never happen due to technical limitations)
                    assert(0);
                    split = 0;
                } else if(offset + ANCHOR_JUMP < chunk->uncompressed_data.n) {
                    //Split found somewhere in the middle of the buffer
                    //Allocate a new chunk and create a new memory buffer
                    temp = (chunk_t *)malloc(sizeof(chunk_t));
                    if(temp==NULL) EXIT_TRACE("Memory allocation failed.\n");

                    //split it into two pieces
                    r = mbuffer_split(&chunk->uncompressed_data, &temp->uncompressed_data, offset + ANCHOR_JUMP);
                    if(r!=0) EXIT_TRACE("Unable to split memory buffer.\n");
                    temp->header.state = CHUNK_STATE_UNCOMPRESSED;
                    temp->sequence.l1num = anchorcount;
                    anchorcount++;

                    //put it into send buffer
                    r = ringbuffer_insert(&send_buf, chunk);
                    ++count;
                    assert(r==0);

                    //send a group of items into the next queue in round-robin fashion
                    if(ringbuffer_isFull(&send_buf)) {
                        r = queue_enqueue(&refine_que[qid], &send_buf, ANCHOR_DATA_PER_INSERT);
                        // log_enqueue("Fragment", "Refine", r, args->tid, qid, &refine_que[qid]);
                        assert(r>=1);
                        qid = (qid+1) % args->nqueues;
                    }
                    //prepare for next iteration
                    chunk = temp;
                    temp = NULL;
                    split = 1;
                } else {
                    //Due to technical limitations we can't distinguish the cases "no split" and "split at end of buffer"
                    //This will result in some unnecessary (and unlikely) work but yields the correct result eventually.
                    temp = chunk;
                    chunk = NULL;
                    split = 0;
                }
            } else {
                //NOTE: We don't process the stub, instead we try to read in more data so we might be able to find a proper split.
                //        Only once the end of the file is reached do we get a genuine stub which will be enqueued right after the read operation.
                temp = chunk;
                chunk = NULL;
                split = 0;
            }
        } while(split);
    }

    //drain buffer
    while(!ringbuffer_isEmpty(&send_buf)) {
        r = queue_enqueue(&refine_que[qid], &send_buf, ANCHOR_DATA_PER_INSERT);
        // log_enqueue("Fragment", "Refine", r, args->tid, qid, &refine_que[qid]);
        assert(r>=1);
        qid = (qid+1) % args->nqueues;
    }

    free(rabintab);
    free(rabinwintab);
    ringbuffer_destroy(&send_buf);

    //shutdown
    for(i=0; i<args->nqueues; i++) {
        queue_terminate(&refine_que[i]);
    }

    printf("Fragment finished. Inserted %d values\n", count);

    return NULL;
}

void *FragmentRefineDefault(void * targs) {
    struct thread_args *args = (struct thread_args *)targs;
    const int qid = args->tid / MAX_THREADS_PER_QUEUE;
    ringbuffer_t recv_buf, send_buf;
    int r;
    int count = 0;

    chunk_t *temp;
    chunk_t *chunk;
    u32int * rabintab = (u32int*)malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*)malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

    r=0;
    r += ringbuffer_init(&recv_buf, MAX_PER_FETCH);
    r += ringbuffer_init(&send_buf, CHUNK_ANCHOR_PER_INSERT);
    assert(r==0);

#ifdef ENABLE_STATISTICS
    stats_t *thread_stats = (stats_t*)malloc(sizeof(stats_t));
    if(thread_stats == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }
    init_stats(thread_stats);
#endif //ENABLE_STATISTICS

    while (TRUE) {
        //if no item for process, get a group of items from the pipeline
        if (ringbuffer_isEmpty(&recv_buf)) {
            r = queue_dequeue(&refine_que[qid], &recv_buf, MAX_PER_FETCH);
            // log_dequeue("Refine", r, args->tid, qid, &refine_que[qid]);
            fflush(stdout);
            if (r < 0) {
                break;
            }
        }

        //get one item
        chunk = (chunk_t *)ringbuffer_remove(&recv_buf);
        assert(chunk!=NULL);

        rabininit(rf_win, rabintab, rabinwintab);

        int split;
        sequence_number_t chcount = 0;
        do {
            //Find next anchor with Rabin fingerprint
            int offset = rabinseg((uchar*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, rf_win, rabintab, rabinwintab);
            //Can we split the buffer?
            if(offset < chunk->uncompressed_data.n) {
                //Allocate a new chunk and create a new memory buffer
                temp = (chunk_t *)malloc(sizeof(chunk_t));
                if(temp==NULL) EXIT_TRACE("Memory allocation failed.\n");
                temp->header.state = chunk->header.state;
                temp->sequence.l1num = chunk->sequence.l1num;

                //split it into two pieces
                r = mbuffer_split(&chunk->uncompressed_data, &temp->uncompressed_data, offset);
                if(r!=0) EXIT_TRACE("Unable to split memory buffer.\n");

                //Set correct state and sequence numbers
                chunk->sequence.l2num = chcount;
                chunk->isLastL2Chunk = FALSE;
                chcount++;

#ifdef ENABLE_STATISTICS
                //update statistics
                thread_stats->nChunks[CHUNK_SIZE_TO_SLOT(chunk->uncompressed_data.n)]++;
#endif //ENABLE_STATISTICS

                //put it into send buffer
                r = ringbuffer_insert(&send_buf, chunk);
                ++count;
                assert(r==0);
                if (ringbuffer_isFull(&send_buf)) {
                    r = queue_enqueue(&deduplicate_que[qid], &send_buf, CHUNK_ANCHOR_PER_INSERT);
                    // log_enqueue("Refine", "Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
                    assert(r>=1);
                }
                //prepare for next iteration
                chunk = temp;
                split = 1;
            } else {
                //End of buffer reached, don't split but simply enqueue it
                //Set correct state and sequence numbers
                chunk->sequence.l2num = chcount;
                chunk->isLastL2Chunk = TRUE;
                chcount++;

#ifdef ENABLE_STATISTICS
                //update statistics
                thread_stats->nChunks[CHUNK_SIZE_TO_SLOT(chunk->uncompressed_data.n)]++;
#endif //ENABLE_STATISTICS

                //put it into send buffer
                r = ringbuffer_insert(&send_buf, chunk);
                ++count;
                assert(r==0);
                if (ringbuffer_isFull(&send_buf)) {
                    r = queue_enqueue(&deduplicate_que[qid], &send_buf, CHUNK_ANCHOR_PER_INSERT);
                    // log_enqueue("Refine", "Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
                    assert(r>=1);
                }
                //prepare for next iteration
                chunk = NULL;
                split = 0;
            }
        } while(split);
    }

    //drain buffer
    while(!ringbuffer_isEmpty(&send_buf)) {
        r = queue_enqueue(&deduplicate_que[qid], &send_buf, CHUNK_ANCHOR_PER_INSERT);
        // log_enqueue("Refine", "Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
        assert(r>=1);
    }

    free(rabintab);
    free(rabinwintab);
    ringbuffer_destroy(&recv_buf);
    ringbuffer_destroy(&send_buf);

    //shutdown
    queue_terminate(&deduplicate_que[qid]);
    printf("FragmentRefine finished, inserted %d values\n", count);
#ifdef ENABLE_STATISTICS
    return thread_stats;
#else
    return NULL;
#endif //ENABLE_STATISTICS
}

void * DeduplicateDefault(void * targs) {
    struct thread_args *args = (struct thread_args *)targs;
    const int qid = args->tid / MAX_THREADS_PER_QUEUE;
    chunk_t *chunk;
    int r;
    int compress_count = 0, reorder_count = 0;

    ringbuffer_t recv_buf, send_buf_reorder, send_buf_compress;

#ifdef ENABLE_STATISTICS
    stats_t *thread_stats = (stats_t*)malloc(sizeof(stats_t));
    if(thread_stats == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }
    init_stats(thread_stats);
#endif //ENABLE_STATISTICS

    r=0;
    r += ringbuffer_init(&recv_buf, CHUNK_ANCHOR_PER_FETCH);
    r += ringbuffer_init(&send_buf_reorder, ITEM_PER_INSERT);
    r += ringbuffer_init(&send_buf_compress, ITEM_PER_INSERT);
    assert(r==0);

    while (1) {
        //if no items available, fetch a group of items from the queue
        if (ringbuffer_isEmpty(&recv_buf)) {
            r = queue_dequeue(&deduplicate_que[qid], &recv_buf, CHUNK_ANCHOR_PER_FETCH);
            // log_dequeue("Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
            if (r < 0) break;
        }

        //get one chunk
        chunk = (chunk_t *)ringbuffer_remove(&recv_buf);
        assert(chunk!=NULL);

        //Do the processing
        int isDuplicate = sub_Deduplicate(chunk);

#ifdef ENABLE_STATISTICS
        if(isDuplicate) {
            thread_stats->nDuplicates++;
        } else {
            thread_stats->total_dedup += chunk->uncompressed_data.n;
        }
#endif //ENABLE_STATISTICS

        //Enqueue chunk either into compression queue or into send queue
        if(!isDuplicate) {
            r = ringbuffer_insert(&send_buf_compress, chunk);
            ++compress_count;
            assert(r==0);
            if (ringbuffer_isFull(&send_buf_compress)) {
                r = queue_enqueue(&compress_que[qid], &send_buf_compress, ITEM_PER_INSERT);
                // log_enqueue("Deduplicate", "Compress", r, args->tid, qid, &compress_que[qid]);
                assert(r>=1);
            }
        } else {
            r = ringbuffer_insert(&send_buf_reorder, chunk);
            ++reorder_count;
            assert(r==0);
            if (ringbuffer_isFull(&send_buf_reorder)) {
                r = queue_enqueue(&reorder_que[qid], &send_buf_reorder, ITEM_PER_INSERT);
                // log_enqueue("Deduplicate", "Reorder", r, args->tid, qid, &reorder_que[qid]);
                assert(r>=1);
            }
        }
    }

    //empty buffers
    while(!ringbuffer_isEmpty(&send_buf_compress)) {
        r = queue_enqueue(&compress_que[qid], &send_buf_compress, ITEM_PER_INSERT);
        // log_enqueue("Deduplicate", "Compress", r, args->tid, qid, &compress_que[qid]);
        assert(r>=1);
    }
    while(!ringbuffer_isEmpty(&send_buf_reorder)) {
        r = queue_enqueue(&reorder_que[qid], &send_buf_reorder, ITEM_PER_INSERT);
        // log_enqueue("Deduplicate", "Reorder", r, args->tid, qid, &reorder_que[qid]);
        assert(r>=1);
    }

    ringbuffer_destroy(&recv_buf);
    ringbuffer_destroy(&send_buf_compress);
    ringbuffer_destroy(&send_buf_reorder);

    //shutdown
    queue_terminate(&compress_que[qid]);

    printf("Deduplicate finished, produced %d compress values, %d reorder values\n", compress_count, reorder_count);
#ifdef ENABLE_STATISTICS
    return thread_stats;
#else
    return NULL;
#endif //ENABLE_STATISTICS
}

void *CompressDefault(void * targs) {
    struct thread_args *args = (struct thread_args *)targs;
    const int qid = args->tid / MAX_THREADS_PER_QUEUE;
    chunk_t * chunk;
    int r;
    int count = 0;

    ringbuffer_t recv_buf, send_buf;

#ifdef ENABLE_STATISTICS
    stats_t *thread_stats = (stats_t*)malloc(sizeof(stats_t));
    if(thread_stats == NULL) EXIT_TRACE("Memory allocation failed.\n");
    init_stats(thread_stats);
#endif //ENABLE_STATISTICS

    r=0;
    r += ringbuffer_init(&recv_buf, ITEM_PER_FETCH);
    r += ringbuffer_init(&send_buf, ITEM_PER_INSERT);
    assert(r==0);

    while(1) {
        //get items from the queue
        if (ringbuffer_isEmpty(&recv_buf)) {
            r = queue_dequeue(&compress_que[qid], &recv_buf, ITEM_PER_FETCH);
            // log_dequeue("Compress", r, args->tid, qid, &compress_que[qid]);
            if (r < 0) break;
        }

        //fetch one item
        chunk = (chunk_t *)ringbuffer_remove(&recv_buf);
        assert(chunk!=NULL);

        sub_Compress(chunk);

#ifdef ENABLE_STATISTICS
        thread_stats->total_compressed += chunk->compressed_data.n;
#endif //ENABLE_STATISTICS

        r = ringbuffer_insert(&send_buf, chunk);
        ++count;
        assert(r==0);

        //put the item in the next queue for the write thread
        if (ringbuffer_isFull(&send_buf)) {
            r = queue_enqueue(&reorder_que[qid], &send_buf, ITEM_PER_INSERT);
            // log_enqueue("Compress", "Reorder", r, args->tid, qid, &reorder_que[qid]);
            assert(r>=1);
        }
    }

    //Enqueue left over items
    while (!ringbuffer_isEmpty(&send_buf)) {
        r = queue_enqueue(&reorder_que[qid], &send_buf, ITEM_PER_INSERT);
        // log_enqueue("Compress", "Reorder", r, args->tid, qid, &reorder_que[qid]);
        assert(r>=1);
    }

    ringbuffer_destroy(&recv_buf);
    ringbuffer_destroy(&send_buf);

    //shutdown
    queue_terminate(&reorder_que[qid]);

    printf("Compress finished, produced %d values\n", count);
#ifdef ENABLE_STATISTICS
    return thread_stats;
#else
    return NULL;
#endif //ENABLE_STATISTICS
}

void *ReorderDefault(void * targs) {
    struct thread_args *args = (struct thread_args *)targs;
    int qid = 0;
    int fd = 0;

    ringbuffer_t recv_buf;
    chunk_t *chunk;

    SearchTree T;
    T = TreeMakeEmpty(NULL);
    Position pos = NULL;
    struct tree_element tele;

    sequence_t next;
    sequence_reset(&next);

    //We perform global anchoring in the first stage and refine the anchoring
    //in the second stage. This array keeps track of the number of chunks in
    //a coarse chunk.
    sequence_number_t *chunks_per_anchor;
    unsigned int chunks_per_anchor_max = 1024;
    chunks_per_anchor = (sequence_number_t*)malloc(chunks_per_anchor_max * sizeof(sequence_number_t));
    if(chunks_per_anchor == NULL) EXIT_TRACE("Error allocating memory\n");
    memset(chunks_per_anchor, 0, chunks_per_anchor_max * sizeof(sequence_number_t));
    int r;
    int i;

    r = ringbuffer_init(&recv_buf, ITEM_PER_FETCH);
    assert(r==0);

    fd = create_output_file(_g_data->_output_filename.c_str());

    while(1) {
        //get a group of items
        if (ringbuffer_isEmpty(&recv_buf)) {
            //process queues in round-robin fashion
            for(i=0,r=0; r<=0 && i<args->nqueues; i++) {
                r = queue_dequeue(&reorder_que[qid], &recv_buf, ITEM_PER_FETCH);
                // log_dequeue("Reorder", r, args->tid, qid, &reorder_que[qid]);
                qid = (qid+1) % args->nqueues;
            }
            if(r<0) break;
        }
        chunk = (chunk_t *)ringbuffer_remove(&recv_buf);
        if (chunk == NULL) break;

        //Double size of sequence number array if necessary
        if(chunk->sequence.l1num >= chunks_per_anchor_max) {
            chunks_per_anchor = (sequence_number_t*)realloc(chunks_per_anchor, 2 * chunks_per_anchor_max * sizeof(sequence_number_t));
            if(chunks_per_anchor == NULL) EXIT_TRACE("Error allocating memory\n");
            memset(&chunks_per_anchor[chunks_per_anchor_max], 0, chunks_per_anchor_max * sizeof(sequence_number_t));
            chunks_per_anchor_max *= 2;
        }
        //Update expected L2 sequence number
        if(chunk->isLastL2Chunk) {
            assert(chunks_per_anchor[chunk->sequence.l1num] == 0);
            chunks_per_anchor[chunk->sequence.l1num] = chunk->sequence.l2num+1;
        }

        //Put chunk into local cache if it's not next in the sequence 
        if(!sequence_eq(chunk->sequence, next)) {
            pos = TreeFind(chunk->sequence.l1num, T);
            if (pos == NULL) {
                //FIXME: Can we remove at least one of the two mallocs in this if-clause?
                //FIXME: Rename "INITIAL_SEARCH_TREE_SIZE" to something more accurate
                tele.l1num = chunk->sequence.l1num;
                tele.queue = Initialize(INITIAL_SEARCH_TREE_SIZE);
                Insert(chunk, tele.queue);
                T = TreeInsert(tele, T);
            } else {
                Insert(chunk, pos->Element.queue);
            }
            continue;
        }

        //write as many chunks as possible, current chunk is next in sequence
        pos = TreeFindMin(T);
        do {
            write_chunk_to_file(fd, chunk);
            if(chunk->header.isDuplicate) {
                free(chunk);
                chunk=NULL;
            }
            sequence_inc_l2(&next);
            if(chunks_per_anchor[next.l1num]!=0 && next.l2num==chunks_per_anchor[next.l1num]) sequence_inc_l1(&next);

            //Check whether we can write more chunks from cache
            if(pos != NULL && (pos->Element.l1num == next.l1num)) {
                chunk = FindMin(pos->Element.queue);
                if(sequence_eq(chunk->sequence, next)) {
                    //Remove chunk from cache, update position for next iteration
                    DeleteMin(pos->Element.queue);
                    if(IsEmpty(pos->Element.queue)) {
                        Destroy(pos->Element.queue);
                        T = TreeDelete(pos->Element, T);
                        pos = TreeFindMin(T);
                    }
                } else {
                    //level 2 sequence number does not match
                    chunk = NULL;
                }
            } else {
                //level 1 sequence number does not match or no chunks left in cache
                chunk = NULL;
            }
        } while(chunk != NULL);
    }

    //flush the blocks left in the cache to file
    pos = TreeFindMin(T);
    while(pos !=NULL) {
        if(pos->Element.l1num == next.l1num) {
            chunk = FindMin(pos->Element.queue);
            if(sequence_eq(chunk->sequence, next)) {
                //Remove chunk from cache, update position for next iteration
                DeleteMin(pos->Element.queue);
                if(IsEmpty(pos->Element.queue)) {
                    Destroy(pos->Element.queue);
                    T = TreeDelete(pos->Element, T);
                    pos = TreeFindMin(T);
                }
            } else {
                //level 2 sequence number does not match
                EXIT_TRACE("L2 sequence number mismatch.\n");
            }
        } else {
            //level 1 sequence number does not match
            EXIT_TRACE("L1 sequence number mismatch.\n");
        }
        write_chunk_to_file(fd, chunk);
        if(chunk->header.isDuplicate) {
            free(chunk);
            chunk=NULL;
        }
        sequence_inc_l2(&next);
        if(chunks_per_anchor[next.l1num]!=0 && next.l2num==chunks_per_anchor[next.l1num]) sequence_inc_l1(&next);

    }

    close(fd);

    ringbuffer_destroy(&recv_buf);
    free(chunks_per_anchor);

    return NULL;
}

#endif // ENABLE_PTHREADS

struct default_launch_args {
    void* (*_start_routine)(void*);
    void* _arg;
    pthread_barrier_t* _barrier;
};

void* default_launch_thread(void* arg) {
    default_launch_args* args = (default_launch_args*)arg;
    pthread_barrier_wait(args->_barrier);
    return args->_start_routine(args->_arg);
}

static void* _dfragment(void* arg) {
    return default_launch_thread(arg);
}

static void* _drefine(void* arg) {
    return default_launch_thread(arg);
}

static void* _ddeduplicate(void* arg) {
    return default_launch_thread(arg);
}

static void* _dcompress(void* arg) {
    return default_launch_thread(arg);
}

static void* _dreorder(void* arg) {
    return default_launch_thread(arg);
}

unsigned long long EncodeDefault(DedupData& data) {
    _g_data = &data; 
    struct stat filestat;
    int32 fd;

#ifdef ENABLE_STATISTICS
    init_stats(&stats);
#endif

    //Create chunk cache
    cache = hashtable_create(65536, hash_from_key_fn, keys_equal_fn, FALSE);
    if(cache == NULL) {
        printf("ERROR: Out of memory\n");
        exit(1);
    }

#ifdef ENABLE_PTHREADS
    printf("Pthread enabled\n");
    struct thread_args data_process_args;
    int i;

    //queue allocation & initialization
    const int nqueues = (data._nb_threads/ MAX_THREADS_PER_QUEUE) +
        ((data._nb_threads % MAX_THREADS_PER_QUEUE != 0) ? 1 : 0);
    deduplicate_que = (queue_t*)malloc(sizeof(queue_t) * nqueues);
    refine_que = (queue_t*)malloc(sizeof(queue_t) * nqueues);
    reorder_que = (queue_t*)malloc(sizeof(queue_t) * nqueues);
    compress_que = (queue_t*)malloc(sizeof(queue_t) * nqueues);
    if( (deduplicate_que == NULL) || (refine_que == NULL) || (reorder_que == NULL) || (compress_que == NULL)) {
        printf("Out of memory\n");
        exit(1);
    }
    int threads_per_queue;
    for(i=0; i<nqueues; i++) {
        if (i < nqueues -1 || data._nb_threads % MAX_THREADS_PER_QUEUE == 0) {
            //all but last queue
            threads_per_queue = MAX_THREADS_PER_QUEUE;
        } else {
            //remaining threads work on last queue
            threads_per_queue = data._nb_threads %MAX_THREADS_PER_QUEUE;
        }

        //call queue_init with threads_per_queue
        queue_init(&deduplicate_que[i], QUEUE_SIZE, threads_per_queue);
        queue_init(&refine_que[i], QUEUE_SIZE, 1);
        queue_init(&reorder_que[i], QUEUE_SIZE, threads_per_queue);
        queue_init(&compress_que[i], QUEUE_SIZE, threads_per_queue);
    }
#else
    struct thread_args generic_args;
#endif //ENABLE_PTHREADS

    int res = mbuffer_system_init();
    assert(res == 0);

    /* src file stat */
    if (stat(data._input_filename.c_str(), &filestat) < 0) 
        EXIT_TRACE("stat() %s failed: %s\n", data._input_filename.c_str(), strerror(errno));

    if (!S_ISREG(filestat.st_mode)) 
        EXIT_TRACE("not a normal file: %s\n", data._input_filename.c_str());
#ifdef ENABLE_STATISTICS
    stats.total_input = filestat.st_size;
#endif //ENABLE_STATISTICS

    /* src file open */
    if((fd = open(data._input_filename.c_str(), O_RDONLY | O_LARGEFILE)) < 0) 
        EXIT_TRACE("%s file open error %s\n", data._input_filename.c_str(), strerror(errno));

    //Load entire file into memory if requested by user
    void *preloading_buffer = NULL;
    if(data._preloading) {
        size_t bytes_read=0;
        int r;

        preloading_buffer = malloc(filestat.st_size);
        if(preloading_buffer == NULL)
            EXIT_TRACE("Error allocating memory for input buffer.\n");

        //Read data until buffer full
        while(bytes_read < filestat.st_size) {
            r = read(fd, (uchar*)preloading_buffer+bytes_read, filestat.st_size-bytes_read);
            if(r<0) switch(errno) {
                case EAGAIN:
                    EXIT_TRACE("I/O error: No data available\n");break;
                case EBADF:
                    EXIT_TRACE("I/O error: Invalid file descriptor\n");break;
                case EFAULT:
                    EXIT_TRACE("I/O error: Buffer out of range\n");break;
                case EINTR:
                    EXIT_TRACE("I/O error: Interruption\n");break;
                case EINVAL:
                    EXIT_TRACE("I/O error: Unable to read from file descriptor\n");break;
                case EIO:
                    EXIT_TRACE("I/O error: Generic I/O error\n");break;
                case EISDIR:
                    EXIT_TRACE("I/O error: Cannot read from a directory\n");break;
                default:
                    EXIT_TRACE("I/O error: Unrecognized error\n");break;
            }
            if(r==0) break;
            bytes_read += r;
        }
#ifdef ENABLE_PTHREADS
        data_process_args.input_file.size = filestat.st_size;
        data_process_args.input_file.buffer = preloading_buffer;
#else
        generic_args.input_file.size = filestat.st_size;
        generic_args.input_file.buffer = preloading_buffer;
#endif //ENABLE_PTHREADS
    }

    /* Variables for 3 thread pools and 2 pipeline stage threads.
     * The first and the last stage are serial (mostly I/O).
     */
    pthread_t threads_anchor[MAX_THREADS],
    threads_chunk[MAX_THREADS],
    threads_compress[MAX_THREADS],
    threads_send, threads_process;

    default_launch_args fragment, refine[MAX_THREADS], deduplicate[MAX_THREADS], compress[MAX_THREADS], reorder;
    data_process_args.tid = 0;
    data_process_args.nqueues = nqueues;
    data_process_args.fd = fd;

#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_begin();
#endif

    int total_threads = 1 + 1 + 3 * data._nb_threads + 1;
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, nullptr, total_threads);

    struct timespec begin, end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    //thread for first pipeline stage (input)
    fragment._start_routine = FragmentDefault;
    fragment._arg = &data_process_args;
    fragment._barrier = &barrier;
    pthread_create(&threads_process, NULL, &_dfragment, &fragment);

    //Create 3 thread pools for the intermediate pipeline stages
    struct thread_args anchor_thread_args[data._nb_threads];
    for (i = 0; i < data._nb_threads; i ++) {
        anchor_thread_args[i].tid = i;
        refine[i]._start_routine = FragmentRefineDefault;
        refine[i]._arg = &anchor_thread_args[i];
        refine[i]._barrier = &barrier;
        pthread_create(&threads_anchor[i], NULL, &_drefine, &refine[i]);
    }

    struct thread_args chunk_thread_args[data._nb_threads];
    for (i = 0; i < data._nb_threads; i ++) {
        chunk_thread_args[i].tid = i;
        deduplicate[i]._start_routine = DeduplicateDefault;
        deduplicate[i]._arg = &chunk_thread_args[i];
        deduplicate[i]._barrier = &barrier;
        pthread_create(&threads_chunk[i], NULL, &_ddeduplicate, &deduplicate[i]);
    }

    struct thread_args compress_thread_args[data._nb_threads];
    for (i = 0; i < data._nb_threads; i ++) {
        compress_thread_args[i].tid = i;
        compress[i]._start_routine = CompressDefault;
        compress[i]._arg = &compress_thread_args[i];
        compress[i]._barrier = &barrier;
        pthread_create(&threads_compress[i], NULL, &_dcompress, &compress[i]);
    }

    //thread for last pipeline stage (output)
    struct thread_args send_block_args;
    send_block_args.tid = 0;
    send_block_args.nqueues = nqueues;
    reorder._start_routine = ReorderDefault;
    reorder._arg = &send_block_args;
    reorder._barrier = &barrier;
    pthread_create(&threads_send, NULL, &_dreorder, &reorder);
    pthread_barrier_wait(&barrier);
    printf("Everybody ready\n");

    /*** parallel phase ***/

    //Return values of threads
    stats_t *threads_anchor_rv[data._nb_threads];
    stats_t *threads_chunk_rv[data._nb_threads];
    stats_t *threads_compress_rv[data._nb_threads];

    //join all threads 
    pthread_join(threads_process, NULL);
    for (i = 0; i < data._nb_threads; i ++)
        pthread_join(threads_anchor[i], (void **)&threads_anchor_rv[i]);
    for (i = 0; i < data._nb_threads; i ++)
        pthread_join(threads_chunk[i], (void **)&threads_chunk_rv[i]);
    for (i = 0; i < data._nb_threads; i ++)
        pthread_join(threads_compress[i], (void **)&threads_compress_rv[i]);
    pthread_join(threads_send, NULL);

#define BILLION 1000000000

    clock_gettime(CLOCK_MONOTONIC, &end);
    /* printf("Setting filename\n");
       unsigned long long diff = (end.tv_sec * BILLION + end.tv_nsec) - (begin.tv_sec * BILLION + begin.tv_nsec);
       char filename[4096];
       sprintf(filename, "/home/amaille/logs/dedup/time.%d.log", STEP);
       printf("filename = %s\n", filename);
       fflush(stdout);
       FILE* log_file = fopen(filename, "a");
       if (log_file == NULL) {
       perror("Error:");
       }
       printf("File: %p\n", log_file);
       fprintf(log_file, "%f\n", (double)diff / (double)BILLION);
       fclose(log_file); */

#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_end();
#endif

    /* free queues */
    for(i=0; i<nqueues; i++) {
        queue_destroy(&deduplicate_que[i]);
        queue_destroy(&refine_que[i]);
        queue_destroy(&reorder_que[i]);
        queue_destroy(&compress_que[i]);
    }
    free(deduplicate_que);
    free(refine_que);
    free(reorder_que);
    free(compress_que);

#ifdef ENABLE_STATISTICS
    //Merge everything into global `stats' structure
    for(i=0; i<data._nb_threads; i++) {
        merge_stats(&stats, threads_anchor_rv[i]);
        free(threads_anchor_rv[i]);
    }
    for(i=0; i<data._nb_threads; i++) {
        merge_stats(&stats, threads_chunk_rv[i]);
        free(threads_chunk_rv[i]);
    }
    for(i=0; i<data._nb_threads; i++) {
        merge_stats(&stats, threads_compress_rv[i]);
        free(threads_compress_rv[i]);
    }
#endif //ENABLE_STATISTICS

    //clean up after preloading
    if(data._preloading) {
        free(preloading_buffer);
    }

    /* clean up with the src file */
    if (data._input_filename.c_str() != NULL)
        close(fd);

    res = mbuffer_system_destroy();
    assert(res == 0);

    hashtable_destroy(cache, TRUE);

#ifdef ENABLE_STATISTICS
    /* dest file stat */
    if (stat(data._output_filename.c_str(), &filestat) < 0) 
        EXIT_TRACE("stat() %s failed: %s\n", data._output_filename.c_str(), strerror(errno));
    stats.total_output = filestat.st_size;

    //Analyze and print statistics
    // if(conf->verbose) print_stats(&stats);
#endif //ENABLE_STATISTICS

    return (end.tv_sec * BILLION + end.tv_nsec) - (begin.tv_sec * BILLION + begin.tv_nsec);
}

