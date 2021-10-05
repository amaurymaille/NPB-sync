#include "encode_common.h"
#include "fifo_plus.tpp"

#ifdef FIFO_PLUS_TIMESTAMP_DATA
std::vector<FIFOPlus<chunk_t*>*> Globals::fifos;
#endif

// Arguments to pass to each thread
struct thread_args {
    //thread id, unique within a thread pool (i.e. unique for a pipeline stage)
    int tid;
    //number of queues available, first and last pipeline stage only
    int nqueues;
    //file descriptor, first pipeline stage only
    int fd;
    //input file buffer, first pipeline stage & preloading only
    struct {
        void *buffer;
        size_t size;
    } input_file;

    /// FIFOs
    FIFOPlus<chunk_t*>* _input_fifo;
    FIFOPlus<chunk_t*>* _output_fifo;
    // For deduplicate stage
    FIFOPlus<chunk_t*>* _extra_output_fifo = nullptr;

    /// FIFOs configuration
    std::map<FIFORole, std::vector<FIFOData>>* _input_fifo_data;
    std::map<FIFORole, std::vector<FIFOData>>* _output_fifo_data;
    std::map<FIFORole, std::vector<FIFOData>>* _extra_output_fifo_data;
};

static void configure_fifo(FIFOPlus<chunk_t*>& fifo, FIFOData const& data, FIFORole role) {
    fifo.set_role(role);
    fifo.set_multipliers(data._increase_mult, data._decrease_mult);
    fifo.set_n(data._min, data._n, data._max);
    fifo.set_thresholds(data._no_work_threshold,
            data._with_work_threshold,
            data._critical_threshold);
    fifo.resize_local_events();
}

/*
 * Pipeline stage function of compression stage
 *
 * Actions performed:
 *    - Dequeue items from compression queue
 *    - Execute compression kernel for each item
 *    - Enqueue each item into send queue
 */
#ifdef ENABLE_PTHREADS
void *Compress(void * targs) {
    struct thread_args *args = (struct thread_args *)targs;
    // const int qid = args->tid / MAX_THREADS_PER_QUEUE;
    FIFOPlus<chunk_t*>* input_fifo = args->_input_fifo;
    configure_fifo(*input_fifo, (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid], FIFORole::CONSUMER);

    FIFOPlus<chunk_t*>* output_fifo = args->_output_fifo;
    configure_fifo(*output_fifo, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid], FIFORole::PRODUCER);
    chunk_t* chunk;
    // int r;
    // int count = 0;

    /* thread_data_t data;
       data.thread_id = args->tid;
       strcpy(data.fname, "Compress");
       pthread_setspecific(thread_data_key, &data); */

    // ringbuffer_t recv_buf, send_buf;

#ifdef ENABLE_STATISTICS
    stats_t *thread_stats = (stats_t*)malloc(sizeof(stats_t));
    if(thread_stats == NULL) EXIT_TRACE("Memory allocation failed.\n");
    init_stats(thread_stats);
#endif //ENABLE_STATISTICS

    /* unsigned int recv_step = compress_initial_extract_step();
       int recv_it = 0;
       bool first = true;
       unsigned int send_step = reorder_initial_insert_step(); 
       int send_it = 0;
       r=0;
       r += ringbuffer_init(&recv_buf, recv_step);
       r += ringbuffer_init(&send_buf, send_step);
       assert(r==0); */

    while(1) {
        //get items from the queue
        /* if (ringbuffer_isEmpty(&recv_buf)) {
           if (!first) {
           update_compress_extract_step(&recv_step, recv_it++);
           ringbuffer_reinit(&recv_buf, recv_step);
           } else {
           first = false;
           }
           r = queue_dequeue(&compress_que[qid], &recv_buf, recv_step);
        // log_dequeue("Compress", r, args->tid, qid, &compress_que[qid]);
        if (r < 0) break;
        } */

        //fetch one item


        std::optional<chunk_t*> chunk_opt;
        input_fifo->pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid]._reconfigure);
        if (!chunk_opt) {
            break;
        }

        chunk = *chunk_opt; // (chunk_t *)ringbuffer_remove(&recv_buf);
        assert(chunk!=NULL);

        sub_Compress(chunk);

#ifdef ENABLE_STATISTICS
        thread_stats->total_compressed += chunk->compressed_data.n;
#endif //ENABLE_STATISTICS

        // r = ringbuffer_insert(&send_buf, chunk);
        output_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid]._reconfigure);
        /* ++count;
           assert(r==0); */

        //put the item in the next queue for the write thread
        /* if (ringbuffer_isFull(&send_buf)) {
           r = queue_enqueue(&reorder_que[qid], &send_buf, send_step);
           update_reorder_insert_step(&send_step, send_it++);
           ringbuffer_reinit(&send_buf, send_step);
        // log_enqueue("Compress", "Reorder", r, args->tid, qid, &reorder_que[qid]);
        assert(r>=1);
        } */
    }

    //Enqueue left over items
    /* while (!ringbuffer_isEmpty(&send_buf)) {
       r = queue_enqueue(&reorder_que[qid], &send_buf, ITEM_PER_INSERT);
    // log_enqueue("Compress", "Reorder", r, args->tid, qid, &reorder_que[qid]);
    assert(r>=1);
    } */

    output_fifo->transfer();

    /* ringbuffer_destroy(&recv_buf);
       ringbuffer_destroy(&send_buf); */

    //shutdown
    // queue_terminate(&reorder_que[qid]);
    output_fifo->terminate();

#ifdef ENABLE_STATISTICS
    return thread_stats;
#else
    return NULL;
#endif //ENABLE_STATISTICS
}
#endif //ENABLE_PTHREADS

/*
 * Pipeline stage function of deduplication stage
 *
 * Actions performed:
 *    - Take input data from fragmentation stages
 *    - Execute deduplication kernel for each data chunk
 *    - Route resulting package either to compression stage or to reorder stage, depending on deduplication status
 */
#ifdef ENABLE_PTHREADS
void * Deduplicate(void * targs) {
    struct thread_args *args = (struct thread_args *)targs;
    FIFOPlus<chunk_t*>* input_fifo = args->_input_fifo;
    configure_fifo(*input_fifo, (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid], FIFORole::CONSUMER);

    FIFOPlus<chunk_t*>* compress_fifo = args->_output_fifo;
    configure_fifo(*compress_fifo, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid], FIFORole::PRODUCER);

    FIFOPlus<chunk_t*>* reorder_fifo = args->_extra_output_fifo;
    configure_fifo(*reorder_fifo, (*args->_extra_output_fifo_data)[FIFORole::PRODUCER][args->tid], FIFORole::PRODUCER);

    // const int qid = args->tid / MAX_THREADS_PER_QUEUE;
    chunk_t *chunk;
    // int r;
    // int compress_count = 0, reorder_count = 0;

    /* thread_data_t data;
       data.thread_id = args->tid;
       strcpy(data.fname, "Deduplicate");
       pthread_setspecific(thread_data_key, &data);

       unsigned int recv_step = dedup_initial_extract_step();
       int recv_it = 0;
       bool first = true;
       unsigned int send_compress_step = compress_initial_insert_step();
       int send_compress_it = 0;
       unsigned int send_reorder_step = reorder_initial_insert_step();
       int send_reorder_it = 0;
       ringbuffer_t recv_buf, send_buf_reorder, send_buf_compress; */

#ifdef ENABLE_STATISTICS
    stats_t *thread_stats = (stats_t*)malloc(sizeof(stats_t));
    if(thread_stats == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }
    init_stats(thread_stats);
#endif //ENABLE_STATISTICS

    /* r=0;
       r += ringbuffer_init(&recv_buf, recv_step);
       r += ringbuffer_init(&send_buf_reorder, send_reorder_step);
       r += ringbuffer_init(&send_buf_compress, send_compress_step);
       assert(r==0); */

    while (1) {
        //if no items available, fetch a group of items from the queue
        /* if (ringbuffer_isEmpty(&recv_buf)) {
           if (!first) {
           update_dedup_extract_step(&recv_step, recv_it++);
           ringbuffer_reinit(&recv_buf, recv_step);
           } else {
           first = false;
           }
           r = queue_dequeue(&deduplicate_que[qid], &recv_buf, recv_step);
        // log_dequeue("Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
        if (r < 0) break;
        } */

        //get one chunk
        /*chunk = (chunk_t *)ringbuffer_remove(&recv_buf);
          assert(chunk!=NULL); */

        std::optional<chunk_t*> chunk_opt;
        input_fifo->pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid]._reconfigure);

        if (!chunk_opt) {
            break;
        }
        chunk = *chunk_opt;

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
            /* r = ringbuffer_insert(&send_buf_compress, chunk);
               ++compress_count;
               assert(r==0);
               if (ringbuffer_isFull(&send_buf_compress)) {
               r = queue_enqueue(&compress_que[qid], &send_buf_compress, send_compress_step);
               update_compress_insert_step(&send_compress_step, send_compress_it++);
               ringbuffer_reinit(&send_buf_compress, send_compress_step);
            // log_enqueue("Deduplicate", "Compress", r, args->tid, qid, &compress_que[qid]);
            assert(r>=1);
            } */
            compress_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid]._reconfigure);
        } else {
            /* r = ringbuffer_insert(&send_buf_reorder, chunk);
               ++reorder_count;
               assert(r==0);
               if (ringbuffer_isFull(&send_buf_reorder)) {
               r = queue_enqueue(&reorder_que[qid], &send_buf_reorder, send_reorder_step);
               update_reorder_insert_step(&send_reorder_step, send_reorder_it++);
               ringbuffer_reinit(&send_buf_reorder, send_reorder_step);
            // log_enqueue("Deduplicate", "Reorder", r, args->tid, qid, &reorder_que[qid]);
            assert(r>=1);
            } */
            reorder_fifo->push(chunk, (*args->_extra_output_fifo_data)[FIFORole::PRODUCER][args->tid]._reconfigure);
        }
    }

    //empty buffers
    /* while(!ringbuffer_isEmpty(&send_buf_compress)) {
       r = queue_enqueue(&compress_que[qid], &send_buf_compress, send_compress_step); // ringbuffer_nb_elements(&send_buf_compress));
    // log_enqueue("Deduplicate", "Compress", r, args->tid, qid, &compress_que[qid]);
    assert(r>=1);
    }
    while(!ringbuffer_isEmpty(&send_buf_reorder)) {
    r = queue_enqueue(&reorder_que[qid], &send_buf_reorder, send_reorder_step); // ringbuffer_nb_elements(&send_buf_reorder));
    // log_enqueue("Deduplicate", "Reorder", r, args->tid, qid, &reorder_que[qid]);
    assert(r>=1);
    }

    ringbuffer_destroy(&recv_buf);
    ringbuffer_destroy(&send_buf_compress);
    ringbuffer_destroy(&send_buf_reorder);

    //shutdown
    queue_terminate(&compress_que[qid]); */

    compress_fifo->transfer();
    compress_fifo->terminate();

    reorder_fifo->transfer();
    reorder_fifo->terminate();

#ifdef ENABLE_STATISTICS
    return thread_stats;
#else
    return NULL;
#endif //ENABLE_STATISTICS
}
#endif //ENABLE_PTHREADS

/*
 * Pipeline stage function and computational kernel of refinement stage
 *
 * Actions performed:
 *    - Take coarse chunks from fragmentation stage
 *    - Partition data block into smaller chunks with Rabin rolling fingerprints
 *    - Send resulting data chunks to deduplication stage
 *
 * Notes:
 *    - Allocates mbuffers for fine-granular chunks
 */
#ifdef ENABLE_PTHREADS
void *FragmentRefine(void * targs) {
    struct thread_args *args = (struct thread_args *)targs;
    /* const int qid = args->tid / MAX_THREADS_PER_QUEUE;
       ringbuffer_t recv_buf, send_buf; */
    int r;
    /* int count = 0;

       thread_data_t data;
       data.thread_id = args->tid;
       strcpy(data.fname, "Refine");
       pthread_setspecific(thread_data_key, &data); */

    FIFOPlus<chunk_t*>* input_fifo = args->_input_fifo;
    configure_fifo(*input_fifo, (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid], FIFORole::CONSUMER);

    FIFOPlus<chunk_t*>* output_fifo = args->_output_fifo;
    configure_fifo(*output_fifo, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid], FIFORole::PRODUCER);

    chunk_t *temp;
    chunk_t *chunk;
    u32int * rabintab = (u32int*)malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*)malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

    /* unsigned int recv_step = refine_initial_extract_step();
       int recv_it = 0;
       unsigned int send_step = dedup_initial_insert_step();
       int send_it = 0;
       bool first = true;

       r=0;
       r += ringbuffer_init(&recv_buf, recv_step);
       r += ringbuffer_init(&send_buf, send_step);
       assert(r==0); */

#ifdef ENABLE_STATISTICS
    stats_t *thread_stats = (stats_t*)malloc(sizeof(stats_t));
    if(thread_stats == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }
    init_stats(thread_stats);
#endif //ENABLE_STATISTICS

    while (TRUE) {
        //if no item for process, get a group of items from the pipeline
        /* if (ringbuffer_isEmpty(&recv_buf)) {
           if (!first) {
           update_refine_extract_step(&recv_step, recv_it++);
           ringbuffer_reinit(&recv_buf, recv_step);
           } else {
           first = false;
           }
           r = queue_dequeue(&refine_que[qid], &recv_buf, recv_step);
        // log_dequeue("Refine", r, args->tid, qid, &refine_que[qid]);
        fflush(stdout);
        if (r < 0) {
        break;
        }
        } */

        //get one item
        /* chunk = (chunk_t *)ringbuffer_remove(&recv_buf);
           assert(chunk!=NULL); */
        std::optional<chunk_t*> chunk_opt;
        input_fifo->pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid]._reconfigure);

        if (!chunk_opt) {
            break;
        }

        chunk = *chunk_opt;

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
                /* r = ringbuffer_insert(&send_buf, chunk);
                   ++count;
                   assert(r==0);
                   if (ringbuffer_isFull(&send_buf)) {
                   r = queue_enqueue(&deduplicate_que[qid], &send_buf, send_step);
                   update_dedup_insert_step(&send_step, send_it++);
                   ringbuffer_reinit(&send_buf, send_step);
                // log_enqueue("Refine", "Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
                assert(r>=1);
                } */
                output_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid]._reconfigure);
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
                /* r = ringbuffer_insert(&send_buf, chunk);
                   ++count;
                   assert(r==0);
                   if (ringbuffer_isFull(&send_buf)) {
                   r = queue_enqueue(&deduplicate_que[qid], &send_buf, send_step);
                   update_dedup_insert_step(&send_step, send_it++);
                   ringbuffer_reinit(&send_buf, send_step);
                // log_enqueue("Refine", "Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
                assert(r>=1);
                } */
                output_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid]._reconfigure);
                //prepare for next iteration
                chunk = NULL;
                split = 0;
            }
        } while(split);
    }

    //drain buffer
    /* while(!ringbuffer_isEmpty(&send_buf)) {
       r = queue_enqueue(&deduplicate_que[qid], &send_buf, send_step);
    // log_enqueue("Refine", "Deduplicate", r, args->tid, qid, &deduplicate_que[qid]);
    assert(r>=1);
    } */

    free(rabintab);
    free(rabinwintab);
    /* ringbuffer_destroy(&recv_buf);
       ringbuffer_destroy(&send_buf); */

    //shutdown
    // queue_terminate(&deduplicate_que[qid]);
    output_fifo->transfer();
    output_fifo->terminate();
#ifdef ENABLE_STATISTICS
    return thread_stats;
#else
    return NULL;
#endif //ENABLE_STATISTICS
}
#endif //ENABLE_PTHREADS

/*
 * Pipeline stage function of fragmentation stage
 *
 * Actions performed:
 *    - Read data from file (or preloading buffer)
 *    - Perform coarse-grained chunking
 *    - Send coarse chunks to refinement stages for further processing
 *
 * Notes:
 * This pipeline stage is a bottleneck because it is inherently serial. We
 * therefore perform only coarse chunking and pass on the data block as fast
 * as possible so that there are no delays that might decrease scalability.
 * With very large numbers of threads this stage will not be able to keep up
 * which will eventually limit scalability. A solution to this is to increase
 * the size of coarse-grained chunks with a comparable increase in total
 * input size.
 */
#ifdef ENABLE_PTHREADS
void *Fragment(void * targs){
    struct thread_args *args = (struct thread_args *)targs;
    size_t preloading_buffer_seek = 0;
    FIFOPlus<chunk_t*>* output_fifo = args->_output_fifo;
    for (int i = 0; i < args->nqueues; ++i) {
        configure_fifo(output_fifo[i], (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid], FIFORole::PRODUCER);
    }

    int qid = 0;
    int fd = args->fd;

    /* thread_data_t data;
       data.thread_id = 0;
       strcpy(data.fname, "Fragment");
       pthread_setspecific(thread_data_key, &data);

       ringbuffer_t send_buf; */
    sequence_number_t anchorcount = 0;
    int r;
    /* int count = 0; */

    chunk_t *temp = NULL;
    chunk_t *chunk = NULL;
    u32int * rabintab = (u32int*)malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*)malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

    /* unsigned int step = refine_initial_insert_step();
       unsigned int it = 0;
       r = ringbuffer_init(&send_buf, step);
       assert(r==0); */

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
            memcpy((char*)(chunk->uncompressed_data.ptr)+bytes_left, (char*)(args->input_file.buffer)+preloading_buffer_seek, max_read);
            bytes_read = max_read;
            preloading_buffer_seek += max_read;
        } else {
            while(bytes_read < MAXBUF) {
                r = read(fd, (char*)(chunk->uncompressed_data.ptr)+bytes_left+bytes_read, MAXBUF-bytes_read);
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
            /* r = ringbuffer_insert(&send_buf, chunk);
               ++count;
               assert(r==0); */
            output_fifo[qid].push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid]._reconfigure);
            qid = (qid + 1) % args->nqueues;
            //NOTE: No need to empty a full send_buf, we will break now and pass everything on to the queue
            break;
        }
        //partition input block into large, coarse-granular chunks
        int split;
        do {
            split = 0;
            //Try to split the buffer at least ANCHOR_JUMP bytes away from its beginning
            if(ANCHOR_JUMP < chunk->uncompressed_data.n) {
                int offset = rabinseg((uchar*)(chunk->uncompressed_data.ptr) + ANCHOR_JUMP, chunk->uncompressed_data.n - ANCHOR_JUMP, rf_win_dataprocess, rabintab, rabinwintab);
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
                    /* r = ringbuffer_insert(&send_buf, chunk);
                       ++count;
                       assert(r==0);

                    //send a group of items into the next queue in round-robin fashion
                    if(ringbuffer_isFull(&send_buf)) {
                    r = queue_enqueue(&refine_que[qid], &send_buf, step);
                    update_refine_insert_step(&step, it++);
                    ringbuffer_reinit(&send_buf, step);
                    // log_enqueue("Fragment", "Refine", r, args->tid, qid, &refine_que[qid]);
                    assert(r>=1);
                    qid = (qid+1) % args->nqueues;
                    } */
                    //prepare for next iteration
                    output_fifo[qid].push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER][args->tid]._reconfigure);
                    qid = (qid + 1) % args->nqueues;
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
    /* while(!ringbuffer_isEmpty(&send_buf)) {
       r = queue_enqueue(&refine_que[qid], &send_buf, step);
    // log_enqueue("Fragment", "Refine", r, args->tid, qid, &refine_que[qid]);
    assert(r>=1);
    qid = (qid+1) % args->nqueues;
    } */

    free(rabintab);
    free(rabinwintab);
    /* ringbuffer_destroy(&send_buf);

    //shutdown
    for(i=0; i<args->nqueues; i++) {
    queue_terminate(&refine_que[i]);
    } */

    for (int i = 0; i < args->nqueues; ++i) {
        output_fifo[i].transfer();
        output_fifo[i].terminate();
    }

    return NULL;
}
#endif //ENABLE_PTHREADS


/*
 * Pipeline stage function of reorder stage
 *
 * Actions performed:
 *    - Receive chunks from compression and deduplication stage
 *    - Check sequence number of each chunk to determine correct order
 *    - Cache chunks that arrive out-of-order until predecessors are available
 *    - Write chunks in-order to file (or preloading buffer)
 *
 * Notes:
 *    - This function blocks if the compression stage has not finished supplying
 *    the compressed data for a duplicate chunk.
 */
#ifdef ENABLE_PTHREADS
void *Reorder(void * targs) {

    struct thread_args *args = (struct thread_args *)targs;
    int qid = 0;
    FIFOPlus<chunk_t*>* input_fifo = args->_input_fifo;
    for (int i = 0; i < args->nqueues; ++i) {
        configure_fifo(input_fifo[i], (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid], FIFORole::CONSUMER);
    }
    int fd = 0;

    /* thread_data_t data;
       data.thread_id = 0;
       strcpy(data.fname, "Reorder");
       pthread_setspecific(thread_data_key, &data);

       ringbuffer_t recv_buf; */
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
    // int r;
    int i;

    /* unsigned int recv_step = reorder_initial_extract_step();
       int recv_it = 0;
       bool first = true;
       r = ringbuffer_init(&recv_buf, recv_step);
       assert(r==0); */

    fd = create_output_file(_g_data->_output_filename.c_str());

    while(1) {
        //get a group of items
        /* if (ringbuffer_isEmpty(&recv_buf)) {
        //process queues in round-robin fashion
        for(i=0,r=0; r<=0 && i<args->nqueues; i++) {
        if (!first) {
        update_reorder_extract_step(&recv_step, recv_it++);
        ringbuffer_reinit(&recv_buf, recv_step);
        } else {
        first = false;
        }
        r = queue_dequeue(&reorder_que[qid], &recv_buf, recv_step);
        // log_dequeue("Reorder", r, args->tid, qid, &reorder_que[qid]);
        qid = (qid+1) % args->nqueues;
        }
        if(r<0) break;
        } */
        // chunk = (chunk_t *)ringbuffer_remove(&recv_buf);
        // if (chunk == NULL) break;


        std::optional<chunk_t*> chunk_opt;
        for (i = 0; i < args->nqueues; ++i) {
            input_fifo[qid].pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER][args->tid]._reconfigure);
            qid = (qid + 1) % args->nqueues;

            if (chunk_opt) {
                break;
            } else {
                chunk_opt = std::nullopt;
            }
        }

        if (!chunk_opt) {
            break;
        }

        chunk = *chunk_opt;

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

    // ringbuffer_destroy(&recv_buf);
    free(chunks_per_anchor);

    return NULL;
}
#endif //ENABLE_PTHREADS

struct launch_stage_args {
    void* (*_start_routine)(void*);
    void* _arg;
    std::vector<PThreadThreadIdentifier*> _identifiers;
    pthread_barrier_t* _barrier;
};

static void* launch_stage_thread(void* arg) {
    launch_stage_args* launch = (launch_stage_args*)arg;
    for (PThreadThreadIdentifier* identifier: launch->_identifiers) {
        identifier->register_thread();
    }

    pthread_barrier_wait(launch->_barrier);

    return launch->_start_routine(launch->_arg);
}

static void* _fragment(void* arg) {
    return launch_stage_thread(arg);
}

static void* _refine(void* arg) {
    return launch_stage_thread(arg);
}

static void* _deduplicate(void* arg) {
    return launch_stage_thread(arg);
}

static void* _compress(void* arg) {
    return launch_stage_thread(arg);
}

static void* _reorder(void* arg) {
    return launch_stage_thread(arg);
}

unsigned long long EncodeMutex(DedupData& data) {
    struct stat filestat;
    int32 fd;

    _g_data = &data;

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
    const int nqueues = (data._nb_threads / MAX_THREADS_PER_QUEUE) +
        ((data._nb_threads % MAX_THREADS_PER_QUEUE != 0) ? 1 : 0);
#else
    struct thread_args generic_args;
#endif //ENABLE_PTHREADS

    int init_res = mbuffer_system_init();
    assert(!init_res);

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
            r = read(fd, (char*)(preloading_buffer)+bytes_read, filestat.st_size-bytes_read);
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

    // PThreadThreadIdentifier* identifier = new PThreadThreadIdentifier();
    data_process_args.tid = 0;
    data_process_args.nqueues = nqueues;
    data_process_args.fd = fd;

    alignas(FIFOPlus<chunk_t*>) unsigned char* refine_input = (unsigned char*)malloc(nqueues * sizeof(FIFOPlus<chunk_t*>));
    alignas(FIFOPlus<chunk_t*>) unsigned char* deduplicate_input = (unsigned char*)malloc(nqueues * sizeof(FIFOPlus<chunk_t*>));
    alignas(FIFOPlus<chunk_t*>) unsigned char* compress_input = (unsigned char*)malloc(nqueues * sizeof(FIFOPlus<chunk_t*>));
    alignas(FIFOPlus<chunk_t*>) unsigned char* reorder_input = (unsigned char*)malloc(nqueues * sizeof(FIFOPlus<chunk_t*>));
    bool extra_queue = (data._nb_threads % MAX_THREADS_PER_QUEUE) != 0;

    std::map<void*, PThreadThreadIdentifier*> identifiers;
    FIFOReconfigure reconfiguration = data._algorithm;

    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, nullptr, 
            1 /* this_thread */    + 
            1 /* Fragment */     +
            3 * data._nb_threads /* Refine + Deduplicate + Compress */ +
            1 /* Reorder */
            );

    auto allocate = [&](void* ptr, FIFOReconfigure reconfiguration_policy, unsigned int nb_producers, unsigned int nb_consumers, unsigned int history_size, std::string&& description) -> void {
        PThreadThreadIdentifier* identifier = new PThreadThreadIdentifier;
        identifiers[ptr] = identifier;
#ifdef FIFO_PLUS_TIMESTAMP_DATA
        new(ptr) FIFOPlus<chunk_t*>(FIFOPlusPopPolicy::POP_WAIT, reconfiguration_policy, identifier, nb_producers, nb_consumers, history_size, std::move(description), Globals::start_time);
        Globals::fifos.push_back((FIFOPlus<chunk_t*>*)ptr);
#else

        new(ptr) FIFOPlus<chunk_t*>(FIFOPlusPopPolicy::POP_WAIT, reconfiguration_policy, identifier, nb_producers, nb_consumers, history_size, std::move(description));
#endif
    };

    for (int i = 0; i < nqueues; ++i) {
        if (i == nqueues - 1 && extra_queue) {
            allocate((refine_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, 1, data._nb_threads % MAX_THREADS_PER_QUEUE, data._fifo_data[FRAGMENT][REFINE][FIFORole::PRODUCER][0]._history_size, "fragment to refine");
            allocate((deduplicate_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, data._nb_threads % MAX_THREADS_PER_QUEUE, data._nb_threads % MAX_THREADS_PER_QUEUE, data._fifo_data[REFINE][DEDUPLICATE][FIFORole::PRODUCER][0]._history_size, "refine to deduplicate");
            allocate((compress_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, data._nb_threads % MAX_THREADS_PER_QUEUE, data._nb_threads % MAX_THREADS_PER_QUEUE, data._fifo_data[DEDUPLICATE][COMPRESS][FIFORole::PRODUCER][0]._history_size, "deduplicate to compress");
            allocate((reorder_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, 2 * (data._nb_threads % MAX_THREADS_PER_QUEUE), 1, data._fifo_data[DEDUPLICATE][REORDER][FIFORole::PRODUCER][0]._history_size, "dedup / compress to reorder");
        } else {
            allocate((refine_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, 1, MAX_THREADS_PER_QUEUE, data._fifo_data[FRAGMENT][REFINE][FIFORole::PRODUCER][0]._history_size, "fragment to refine");
            allocate((deduplicate_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, MAX_THREADS_PER_QUEUE, MAX_THREADS_PER_QUEUE, data._fifo_data[REFINE][DEDUPLICATE][FIFORole::PRODUCER][0]._history_size, "refine to deduplicate");
            allocate((compress_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, MAX_THREADS_PER_QUEUE, MAX_THREADS_PER_QUEUE, data._fifo_data[DEDUPLICATE][COMPRESS][FIFORole::PRODUCER][0]._history_size, "deduplicate to compress");
            allocate((reorder_input + i * sizeof(FIFOPlus<chunk_t*>)), reconfiguration, 2 * MAX_THREADS_PER_QUEUE, 1, data._fifo_data[DEDUPLICATE][REORDER][FIFORole::PRODUCER][0]._history_size, "dedup / compress to reorder");
        }
    }

    FIFOPlus<chunk_t*>* refine = reinterpret_cast<FIFOPlus<chunk_t*>*>(refine_input);
    FIFOPlus<chunk_t*>* deduplicate = reinterpret_cast<FIFOPlus<chunk_t*>*>(deduplicate_input);
    FIFOPlus<chunk_t*>* compress = reinterpret_cast<FIFOPlus<chunk_t*>*>(compress_input);
    FIFOPlus<chunk_t*>* reorder = reinterpret_cast<FIFOPlus<chunk_t*>*>(reorder_input);
    data_process_args._output_fifo = refine;
    data_process_args._output_fifo_data = &(data._fifo_data[FRAGMENT][REFINE]);

#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_begin();
#endif

    /* struct timespec begin, end;
       clock_gettime(CLOCK_MONOTONIC, &begin); */
    //thread for first pipeline stage (input)
    // identifiers[refine]->pthread_create(&threads_process, NULL, Fragment, &data_process_args);
    launch_stage_args fragment_args;
    fragment_args._start_routine = Fragment;
    fragment_args._arg = &data_process_args;
    for (int i = 0; i < nqueues; ++i) {
        fragment_args._identifiers.push_back(identifiers[refine + i]);
    }
    fragment_args._barrier = &barrier;
    pthread_create(&threads_process, nullptr, _fragment, &fragment_args);

    launch_stage_args refine_args[data._nb_threads];
    //Create 3 thread pools for the intermediate pipeline stages
    struct thread_args anchor_thread_args[data._nb_threads];
    for (i = 0; i < data._nb_threads; i ++) {
        int queue_id = i / MAX_THREADS_PER_QUEUE;

        anchor_thread_args[i].tid = i;
        anchor_thread_args[i]._input_fifo = &refine[queue_id];
        anchor_thread_args[i]._output_fifo = &deduplicate[queue_id];
        anchor_thread_args[i]._input_fifo_data = &(data._fifo_data[FRAGMENT][REFINE]);
        anchor_thread_args[i]._output_fifo_data = &(data._fifo_data[REFINE][DEDUPLICATE]);
        // identifiers[refine + (i / MAX_THREADS_PER_QUEUE)]->pthread_create(&threads_anchor[i], nullptr, FragmentRefine, &anchor_thread_args[i]);
        refine_args[i]._start_routine = FragmentRefine;
        refine_args[i]._arg = &anchor_thread_args[i];
        refine_args[i]._identifiers.push_back(identifiers[refine + queue_id]);
        refine_args[i]._identifiers.push_back(identifiers[deduplicate + queue_id]);
        refine_args[i]._barrier = &barrier;
        pthread_create(&threads_anchor[i], nullptr, _refine, refine_args + i);
    }

    struct thread_args chunk_thread_args[data._nb_threads];
    launch_stage_args deduplicate_args[data._nb_threads];
    for (i = 0; i < data._nb_threads; i ++) {
        int queue_id = i / MAX_THREADS_PER_QUEUE;

        chunk_thread_args[i].tid = i;
        chunk_thread_args[i]._input_fifo = &deduplicate[queue_id];
        chunk_thread_args[i]._output_fifo = &compress[queue_id];
        chunk_thread_args[i]._extra_output_fifo = &reorder[queue_id];
        chunk_thread_args[i]._input_fifo_data = &(data._fifo_data[REFINE][DEDUPLICATE]);
        chunk_thread_args[i]._output_fifo_data = &(data._fifo_data[DEDUPLICATE][COMPRESS]);
        chunk_thread_args[i]._extra_output_fifo_data = &(data._fifo_data[DEDUPLICATE][REORDER]);
        // identifiers[deduplicate + (i / MAX_THREADS_PER_QUEUE)]->pthread_create(&threads_chunk[i], NULL, Deduplicate, &chunk_thread_args[i]);
        deduplicate_args[i]._start_routine = Deduplicate;
        deduplicate_args[i]._arg = &chunk_thread_args[i];
        deduplicate_args[i]._identifiers.push_back(identifiers[deduplicate + queue_id]);
        deduplicate_args[i]._identifiers.push_back(identifiers[compress + queue_id]);
        deduplicate_args[i]._identifiers.push_back(identifiers[reorder + queue_id]);
        deduplicate_args[i]._barrier = &barrier;
        pthread_create(&threads_chunk[i], nullptr, _deduplicate, deduplicate_args + i);
    }

    struct thread_args compress_thread_args[data._nb_threads];
    launch_stage_args compress_args[data._nb_threads];
    for (i = 0; i < data._nb_threads; i ++) {
        int queue_id = i / MAX_THREADS_PER_QUEUE;

        compress_thread_args[i].tid = i;
        compress_thread_args[i]._input_fifo = &compress[queue_id];
        compress_thread_args[i]._output_fifo = &reorder[queue_id];
        compress_thread_args[i]._input_fifo_data = &(data._fifo_data[DEDUPLICATE][COMPRESS]);
        compress_thread_args[i]._output_fifo_data = &(data._fifo_data[DEDUPLICATE][REORDER]);
        // identifiers[compress + i / MAX_THREADS_PER_QUEUE]->pthread_create(&threads_compress[i], NULL, Compress, &compress_thread_args[i]);
        compress_args[i]._start_routine = Compress;
        compress_args[i]._arg = &compress_thread_args[i];
        compress_args[i]._identifiers.push_back(identifiers[compress + queue_id]);
        compress_args[i]._identifiers.push_back(identifiers[reorder + queue_id]);
        compress_args[i]._barrier = &barrier;
        pthread_create(&threads_compress[i], nullptr, _compress, compress_args + i);
    }

    //thread for last pipeline stage (output)
    struct thread_args send_block_args;
    send_block_args.tid = 0;
    send_block_args.nqueues = nqueues;
    send_block_args._input_fifo = reorder;
    send_block_args._input_fifo_data = &(data._fifo_data[DEDUPLICATE][REORDER]);
    // identifiers[reorder]->pthread_create(&threads_send, NULL, Reorder, &send_block_args);
    launch_stage_args reorder_args;
    reorder_args._start_routine = Reorder;
    reorder_args._arg = &send_block_args;
    for (int i = 0; i < nqueues; ++i) {
        reorder_args._identifiers.push_back(identifiers[reorder + i]);
    }
    reorder_args._barrier = &barrier;
    pthread_create(&threads_send, nullptr, _reorder, &reorder_args);

    pthread_barrier_wait(&barrier);
    // May have a really small overhead because of how heavy the barrier is.
    timespec begin, end; clock_gettime(CLOCK_MONOTONIC, &begin);

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

    clock_gettime(CLOCK_MONOTONIC, &end);
#define BILLION 1000000000
    unsigned long long diff = (end.tv_sec * BILLION + end.tv_nsec) - (begin.tv_sec * BILLION + begin.tv_nsec);

    /* clock_gettime(CLOCK_MONOTONIC, &end);
       unsigned long long diff = (end.tv_sec * BILLION + end.tv_nsec) - (begin.tv_sec * BILLION + begin.tv_nsec);
       char filename[4096];
       sprintf(filename, "/home/amaille/logs/dedup/time.log");
       fflush(stdout);
       FILE* log_file = fopen(filename, "a");
       if (log_file == NULL) {
       perror("Error:");
       } else {
       printf("Writing in file: %s\n", filename);
       fprintf(log_file, "%f\n", (double)diff / (double)BILLION);
       fclose(log_file);
       } */

#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_end();
#endif

    std::vector<const FIFOPlus<chunk_t*>*> fifos;
    fifos.push_back(refine);
    fifos.push_back(deduplicate);
    fifos.push_back(compress);
    fifos.push_back(reorder);

    for (const FIFOPlus<chunk_t*>*& fifo: fifos) {
        for (int i = 0; i < nqueues; ++i) {
            auto const& tss = fifo[i].get_tss();
            auto const& data = tss.get_values();

            int j = 0;
            for (auto const& element: data) {
                if (element._role != FIFORole::PRODUCER)
                    continue;
                std::cout << "Queue ";

                if (fifo == refine) {
                    std::cout << "Refine ";
                } else if (fifo == deduplicate) {
                    std::cout << "Deduplicate ";
                } else if (fifo == compress) {
                    std::cout << "Compress ";
                } else {
                    std::cout << "Reorder ";
                }

                std::cout << i << ", thread " << j++ << " ended at N = " << element._n << std::endl;
            }
        }
    }

    /* for (int i = 0; i < nqueues; ++i) {
       refine[i].~FIFOPlus<chunk_t*>();
       deduplicate[i].~FIFOPlus<chunk_t*>();
       compress[i].~FIFOPlus<chunk_t*>();
       reorder[i].~FIFOPlus<chunk_t*>();
       } */

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
    if (!data._input_filename.empty())
        close(fd);

    int des_res = mbuffer_system_destroy();
    assert(!des_res);

    hashtable_destroy(cache, TRUE);

#ifdef ENABLE_STATISTICS
    /* dest file stat */
    if (stat(data._output_filename.c_str(), &filestat) < 0)
        EXIT_TRACE("stat() %s failed: %s\n", data._output_filename.c_str(), strerror(errno));
    stats.total_output = filestat.st_size;

    //Analyze and print statistics
    // if(conf->verbose) print_stats(&stats);
#endif //ENABLE_STATISTICS

    return diff;
}

