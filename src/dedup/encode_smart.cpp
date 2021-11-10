#include <set>

#include "encode_common.h"
#include "smart_fifo.h"

struct deduplicate_push {
    bool _reorder;
    std::chrono::time_point<std::chrono::steady_clock> _tp;
};

struct thread_args_smart {
    int fd;
    struct {
        void* buffer;
        size_t size;
    } input_file;

    std::vector<SmartFIFO<chunk_t*>*> _input_fifos;
    std::vector<SmartFIFO<chunk_t*>*> _output_fifos;
    std::vector<SmartFIFO<chunk_t*>*> _extra_output_fifos;
    pthread_barrier_t* _barrier;
    Globals::SmartFIFOTSV* _timestamp_data;
    std::vector<deduplicate_push> _times;
};

// ============================================================================
// Smart FIFO version
// ============================================================================

void FragmentSmart(thread_args_smart const& args) {
    Globals::SmartFIFOTSV& data = *args._timestamp_data;

    size_t preloading_buffer_seek = 0;
    int qid = 0;
    int fd = args.fd;
    int r;

    sequence_number_t anchorcount = 0;
    int count = 0;

    chunk_t *temp = NULL;
    chunk_t *chunk = NULL;
    u32int * rabintab = (u32int*) malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*) malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

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
            size_t max_read = MIN(MAXBUF, args.input_file.size-preloading_buffer_seek);
            memcpy((uchar*)chunk->uncompressed_data.ptr+bytes_left, (uchar*)args.input_file.buffer+preloading_buffer_seek, max_read);
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
            //printf ("FragmentSmart: pushing chunk %p\n", chunk);
            // dump_chunk(chunk);
            size_t push_res = args._output_fifos[qid]->push(chunk);
            if (push_res) {
                // data.push_back(std::make_tuple(Globals::now(), args._output_fifos[qid], args._output_fifos[qid]->impl(), Globals::Action::PUSH, push_res));
            }
            ++count;
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
                    //printf("FragmentSmart: pushing chunk %p\n", chunk);
                    // dump_chunk(chunk);
                    size_t push_res = args._output_fifos[qid]->push(chunk);
                    if (push_res) {
                        // data.push_back(std::make_tuple(Globals::now(), args._output_fifos[qid], args._output_fifos[qid]->impl(), Globals::Action::PUSH, push_res));
                    }
                    ++count;

                    //send a group of items into the next queue in round-robin fashion
                    // No need to reset count because modulo
                    if (count % args._output_fifos[qid]->get_step() == 0) {
                        qid = (qid + 1) % args._output_fifos.size();
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

    for (SmartFIFO<chunk_t*>* fifo: args._output_fifos) {
        fifo->terminate_producer();
    }

    free(rabintab);
    free(rabinwintab);

    pthread_barrier_wait(args._barrier);
    // printf("Fragment finished. Inserted %d values\n", count);
}

void RefineSmart(thread_args_smart const& args) {
    Globals::SmartFIFOTSV& data = *args._timestamp_data;
    pthread_barrier_wait(args._barrier);
    int r;
    int count = 0;
    int pop = 0;

    chunk_t *temp;
    chunk_t *chunk;
    u32int * rabintab = (u32int*)malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*)malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

    r=0;

    while (TRUE) {
        //if no item for process, get a group of items from the pipeline
        std::optional<chunk_t**> value;
        auto [valid, nb_elements] = args._input_fifos[0]->pop(value);

        if (!value) {
            break;
        }

        chunk = **value;
        // printf("Refine %p: %d, %p\n", args._input_fifos[0], ++pop, chunk);

        if (valid) {
            // data.push_back(std::make_tuple(Globals::now(), args._input_fifos[0], args._input_fifos[0]->impl(), Globals::Action::POP, nb_elements));
        }
        //printf("RefineSmart: poped chunk %p\n", chunk);
        // check_chunk(chunk);

        //get one item
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

                //put it into send buffer
                //printf("RefineSmart: pushing chunk %p\n", chunk);
                // dump_chunk(chunk);
                size_t push_res = args._output_fifos[0]->push(chunk);
                if (push_res) {
                    // data.push_back(std::make_tuple(Globals::now(), args._output_fifos[0], args._output_fifos[0]->impl(), Globals::Action::PUSH, push_res));
                }
                ++count;

                //prepare for next iteration
                chunk = temp;
                split = 1;
            } else {
                //End of buffer reached, don't split but simply enqueue it
                //Set correct state and sequence numbers
                chunk->sequence.l2num = chcount;
                chunk->isLastL2Chunk = TRUE;
                chcount++;

                //put it into send buffer
                //printf("RefineSmart: pushing chunk %p\n", chunk);
                // dump_chunk(chunk);
                size_t push_res = args._output_fifos[0]->push(chunk);
                if (push_res) {
                    // data.push_back(std::make_tuple(Globals::now(), args._output_fifos[0], args._output_fifos[0]->impl(), Globals::Action::PUSH, push_res));
                }
                ++count;

                //prepare for next iteration
                chunk = NULL;
                split = 0;
            }
        } while(split);
    }

    free(rabintab);
    free(rabinwintab);

    //shutdown
    args._output_fifos[0]->terminate_producer();
    // printf("FragmentRefine finished, inserted %d values\n", count);
}


void DeduplicateSmart(thread_args_smart const& args) {
    Globals::SmartFIFOTSV& data = *args._timestamp_data;
    pthread_barrier_wait(args._barrier);
    chunk_t *chunk;
    int compress_count = 0, reorder_count = 0;
    int pos = 0;
    std::vector<deduplicate_push>& times = const_cast<std::vector<deduplicate_push>&>(args._times);

    while (1) {
        //if no items available, fetch a group of items from the queue
        std::optional<chunk_t**> value;
        auto [valid, nb_elements] = args._input_fifos[0]->pop(value);

        if (!value) {
            break;
        }

        //get one chunk
        chunk = **value;

        /* if (valid && args.tid % args.nqueues == 1) {
            data.push_back(std::make_tuple(Globals::now(), args._input_fifos[0], args._input_fifos[0]->impl(), Globals::Action::POP, nb_elements));
        } */
        
        //printf("DeduplicateSmart: poped chunk %p\n", chunk);
        // check_chunk(chunk);

        //Do the processing
        int isDuplicate = sub_Deduplicate(chunk);

        //Enqueue chunk either into compression queue or into send queue
        if(!isDuplicate) {
            //printf("DeduplicateSmart: pushed non duplicated chunk %p\n", chunk);
            // dump_chunk(chunk);
            size_t push_res = args._output_fifos[0]->push(chunk);
            /* deduplicate_push& dedup_data = times[pos++];
            dedup_data._reorder = false;
            dedup_data._tp = std::chrono::steady_clock::now(); */
            /* if (push_res && args.tid % args.nqueues == 1) {
                data.push_back(std::make_tuple(Globals::now(), args._output_fifos[0], args._output_fifos[0]->impl(), Globals::Action::PUSH, push_res));
            } */
            ++compress_count;
        } else {
            //printf("DeduplicateSmart: pushed duplicated chunk %p\n", chunk);
            // dump_chunk(chunk);
            size_t push_res = args._extra_output_fifos[0]->push(chunk);
            /* deduplicate_push& dedup_data = times[pos++];
            dedup_data._reorder = true;
            dedup_data._tp = std::chrono::steady_clock::now(); */

            /* if (push_res && args.tid % args.nqueues == 1) {
                data.push_back(std::make_tuple(Globals::now(), args._extra_output_fifo, args._extra_output_fifo->impl(), Globals::Action::PUSH, push_res));
                } */
            ++reorder_count;
        }
    }

    times.resize(pos);
    args._output_fifos[0]->terminate_producer();
    args._extra_output_fifos[0]->terminate_producer();
    // printf("Deduplicate finished, produced %d compress values, %d reorder values\n", compress_count, reorder_count);
}

void CompressSmart(thread_args_smart const& args) {
    Globals::SmartFIFOTSV& data = *args._timestamp_data;
    pthread_barrier_wait(args._barrier);
    chunk_t * chunk;
    int count = 0;

    while(1) {
        std::optional<chunk_t**> value;
        auto before = std::chrono::steady_clock::now();
        auto [valid, nb_elements] = args._input_fifos[0]->pop(value);

        if (valid) {

        }

        if (!value) {
            break;
        }

        //fetch one item
        chunk = **value;

        /* if (valid && args.tid % args.nqueues == 1) {
            data.push_back(std::make_tuple(Globals::now(), args._input_fifos[0], args._input_fifos[0]->impl(), Globals::Action::POP, nb_elements));
            } */
        //printf("CompressSmart: poped chunk %p\n", chunk);
        // check_chunk(chunk);

        sub_Compress(chunk);

        //printf("CompressSmart: pushed chunk %p\n", chunk);
        // dump_chunk(chunk);
        size_t push_res = args._output_fifos[0]->push(chunk);
        /* if (push_res && args.tid % args.nqueues == 1) {
            data.push_back(std::make_tuple(Globals::now(), args._output_fifos[0], args._output_fifos[0]->impl(), Globals::Action::PUSH, push_res));
            } */
        ++count;

        //put the item in the next queue for the write thread
    }

    args._output_fifos[0]->terminate_producer();
    // printf("Compress finished, produced %d values\n", count);
}

void ReorderSmart(thread_args_smart const& args) {
    Globals::SmartFIFOTSV& data = *args._timestamp_data;
    pthread_barrier_wait(args._barrier);
    int fd = 0;

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

    fd = create_output_file(_g_data->_output_filename.c_str());
    int qid = 0;

    while(1) {
        std::optional<chunk_t**> value;
        SmartFIFO<chunk_t*>* lfifo = nullptr;
        SmartFIFOImpl<chunk_t*>* fifo = nullptr;
        bool valid = false;
        size_t nb_elements = 0;
        for (int i = 0; i < args._input_fifos.size(); ++i) {
            lfifo = args._input_fifos[qid];
            fifo = args._input_fifos[qid]->impl();
            std::tie(valid, nb_elements) = args._input_fifos[qid]->pop(value);
            qid = (qid + 1) % args._input_fifos.size();
            if (value) {
                break;
            }
        }

        if (!value) {
            break;
        }

        if (valid) {
            // data.push_back(std::make_tuple(Globals::now(), lfifo, fifo, Globals::Action::POP, nb_elements));
        }

        chunk = **value;
        //printf("ReorderSmart: poped chunk %p\n", chunk);
        // check_chunk(chunk);
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

    /* for (SmartFIFO<chunk_t*>* fifo: args._input_fifos) {
        fifo->dump();
    } */

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
                throw std::runtime_error("L2 sequencen number mismatch");
                EXIT_TRACE("L2 sequence number mismatch.\n");
            }
        } else {
            //level 1 sequence number does not match
            throw std::runtime_error("L1 sequence number mismatch");
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

    free(chunks_per_anchor);
}

std::vector<std::unique_ptr<thread_args_smart>> _thread_args_smart_vector;

thread_args_smart* thread_args_smart_copy_because_pthread(thread_args_smart const& src) {
    thread_args_smart* ptr = new thread_args_smart;
    *ptr = src;
    _thread_args_smart_vector.push_back(std::unique_ptr<thread_args_smart>(ptr));
    return ptr;
}

void* _FragmentSmart(void* args) {
    FragmentSmart(*static_cast<thread_args_smart*>(args));
    return nullptr;
}

void* _RefineSmart(void* args) {
    RefineSmart(*static_cast<thread_args_smart*>(args));
    return nullptr;
}

void* _DeduplicateSmart(void* args) {
    DeduplicateSmart(*static_cast<thread_args_smart*>(args));
    return nullptr;
}

void* _CompressSmart(void* args) {
    CompressSmart(*static_cast<thread_args_smart*>(args));
    return nullptr;
}

void* _ReorderSmart(void* args) {
    ReorderSmart(*static_cast<thread_args_smart*>(args));
    return nullptr;
}

void _Encode(std::vector<Globals::SmartFIFOTSV>& timestamp_datas, DedupData& data, int fd, size_t filesize, void* buffer, tp& begin, tp& end) {
    LayerData& fragment = data._layers_data[Layers::FRAGMENT];
    LayerData& refine = data._layers_data[Layers::REFINE];
    LayerData& deduplicate = data._layers_data[Layers::DEDUPLICATE];
    LayerData& compress = data._layers_data[Layers::COMPRESS];
    LayerData& reorder = data._layers_data[Layers::REORDER];

    std::set<int> sfragment, srefine, sdeduplicate, scompress, sreorder;
    std::map<int, SmartFIFOImpl<chunk_t*>*> ids_to_fifos;

    size_t timestamp_pos = 0;
    timestamp_datas.resize(data.get_total_threads());
    
    auto alloc_queues = [&ids_to_fifos](SmartFIFOImpl<chunk_t*>** fifos, std::set<int>& fifo_ids, LayerData const& data, std::string&& description) {
        compute_fifo_ids_for_layer(fifo_ids, data);
        *fifos = new SmartFIFOImpl<chunk_t*>[fifo_ids.size()];

        auto iter = fifo_ids.begin();
        for (int i = 0; i < fifo_ids.size(); ++i, ++iter) {
            (*fifos)[i].set_description(std::move(description));
            ids_to_fifos[*iter] = *fifos + i;
        }
    };

    SmartFIFOImpl<chunk_t*>* fragment_to_refine, *refine_to_deduplicate, *deduplicate_to_compress, *dedupcompress_to_reorder;
    alloc_queues(&fragment_to_refine, srefine, fragment, "fragment to refine");
    alloc_queues(&refine_to_deduplicate, sdeduplicate, refine, "refine to deduplicate");
    alloc_queues(&deduplicate_to_compress, scompress, deduplicate, "deduplicate to compress");
    
    {
        compute_fifo_ids_for_reorder(sreorder, deduplicate, compress);
        dedupcompress_to_reorder = new SmartFIFOImpl<chunk_t*>[sreorder.size()];

        auto iter = sreorder.begin();
        for (int i = 0; i < sreorder.size(); ++i, ++iter) {
            dedupcompress_to_reorder[i].set_description("dedup / compress to reorder");
            ids_to_fifos[*iter] = dedupcompress_to_reorder + i;
        }
    }

    pthread_barrier_t barrier;
    unsigned int nb_threads = data.get_total_threads();
    pthread_barrier_init(&barrier, nullptr, nb_threads + 1);

    auto launch_stage = [&barrier, &data, &ids_to_fifos, &timestamp_datas, &timestamp_pos, &fd, &filesize, &buffer](void* (*routine)(void*), LayerData const& layer_data, bool extra = false) {
        thread_args_smart* args = new thread_args_smart[layer_data.get_total_threads()];
        for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            args[i]._times.resize(100000000);
        }

        pthread_t* threads = new pthread_t[layer_data.get_total_threads()];

        auto generate_views = [&data, &ids_to_fifos](std::vector<SmartFIFO<chunk_t*>*>& target, bool producer, std::map<int, FIFOData> const& fifo_ids) {
            for (auto const& [fifo, fifo_data]: fifo_ids) {
                // FIFOData& fifo_data = data._fifo_data[fifo];
                target.push_back(ids_to_fifos[fifo]->view(producer, fifo_data._n, fifo_data._reconfigure, fifo_data._change_step_after, fifo_data._new_step));
            }
        };

        int i = 0;
        for (ThreadData const& thread_data: layer_data._thread_data) {
            generate_views(args[i]._output_fifos, true, thread_data._outputs);
            generate_views(args[i]._input_fifos, false, thread_data._inputs);

            if (extra) {
                generate_views(args[i]._extra_output_fifos, true, thread_data._extras);
            }

            args[i]._barrier = &barrier;
            args[i]._timestamp_data = timestamp_datas.data() + timestamp_pos;
            args[i].fd = fd;

            if (data._preloading) {
                args[i].input_file.size = filesize;
                args[i].input_file.buffer = buffer;
            }

            ++timestamp_pos;

            pthread_create(threads + i, nullptr, routine, args + i);
            ++i;
        }

        return std::tuple<pthread_t*, thread_args_smart*>(threads, args);
    };

    auto fragment_stage = launch_stage(_FragmentSmart, fragment);
    auto refine_stage = launch_stage(_RefineSmart, refine);
    auto deduplicate_stage = launch_stage(_DeduplicateSmart, deduplicate, true);
    auto compress_stage = launch_stage(_CompressSmart, compress);
    auto reorder_stage = launch_stage(_ReorderSmart, reorder);

    std::vector<std::vector<deduplicate_push>> dedup_data(deduplicate.get_total_threads());

    auto terminate = [&dedup_data](auto stage, LayerData const& layer_data, bool dedup = false) {
        auto [threads, args] = stage;
        for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            pthread_join(threads[i], nullptr);
            if (dedup) {
                dedup_data[i] = std::move(args[i]._times);
            }
        }

        delete[] threads;
        delete[] args;
    };

    pthread_barrier_wait(&barrier);

    begin = sc::now();

    terminate(fragment_stage, fragment);
    terminate(refine_stage, refine);
    terminate(deduplicate_stage, deduplicate, true);
    terminate(compress_stage, compress);
    terminate(reorder_stage, reorder);

    end = sc::now();

    int pos = 0;
    for (auto& v: dedup_data) {
        for (auto const& time: v) {
            unsigned long long int diff = std::chrono::duration_cast<std::chrono::nanoseconds>(time._tp - Globals::_start_time).count();
            if (time._reorder) {
                printf("[Deduplicate %d, log] %llu reorder\n", pos, diff);
            } else {
                printf("[Deduplicate %d, log] %llu compressr\n", pos, diff);
            }
        }
    }
}

std::tuple<unsigned long long, std::vector<Globals::SmartFIFOTSV>> EncodeSmart(DedupData& data) {
    std::vector<Globals::SmartFIFOTSV> timestamp_datas;
    // I don't have std::bind_front :(
    return { EncodeBase(data, [&timestamp_datas](DedupData& data, int fd, size_t filesize, void* buffer, tp& begin, tp& end) {
                _Encode(timestamp_datas, data, fd, filesize, buffer, begin, end);
            }), timestamp_datas };
//    struct stat filestat;
//    int32 fd;
//
//    _g_data = &data;
//
//    //Create chunk cache
//    cache = hashtable_create(65536, hash_from_key_fn, keys_equal_fn, FALSE);
//    if(cache == NULL) {
//        printf("ERROR: Out of memory\n");
//        exit(1);
//    }
//
//    std::vector<Globals::SmartFIFOTSV> timestamp_datas(1 + 3 * data._nb_threads + 1);
//    struct thread_args_smart fragment_args;
//
//    //queue allocation & initialization
//    const int nqueues = (data._nb_threads / MAX_THREADS_PER_QUEUE) +
//        ((data._nb_threads % MAX_THREADS_PER_QUEUE != 0) ? 1 : 0);
//    bool extra_queue = (data._nb_threads % MAX_THREADS_PER_QUEUE) != 0;
//
//    int init_res = mbuffer_system_init();
//    assert(!init_res);
//
//    /* src file stat */
//    if (stat(data._input_filename.c_str(), &filestat) < 0)
//        EXIT_TRACE("stat() %s failed: %s\n", data._input_filename.c_str(), strerror(errno));
//
//    if (!S_ISREG(filestat.st_mode))
//        EXIT_TRACE("not a normal file: %s\n", data._input_filename.c_str());
//
//    /* src file open */
//    if((fd = open(data._input_filename.c_str(), O_RDONLY | O_LARGEFILE)) < 0)
//        EXIT_TRACE("%s file open error %s\n", data._input_filename.c_str(), strerror(errno));
//
//    //Load entire file into memory if requested by user
//    void *preloading_buffer = NULL;
//    if(data._preloading) {
//        size_t bytes_read=0;
//        int r;
//
//        preloading_buffer = malloc(filestat.st_size);
//        if(preloading_buffer == NULL)
//            EXIT_TRACE("Error allocating memory for input buffer.\n");
//
//        //Read data until buffer full
//        while(bytes_read < filestat.st_size) {
//            r = read(fd, (char*)(preloading_buffer)+bytes_read, filestat.st_size-bytes_read);
//            if(r<0) switch(errno) {
//                case EAGAIN:
//                    EXIT_TRACE("I/O error: No data available\n");break;
//                case EBADF:
//                    EXIT_TRACE("I/O error: Invalid file descriptor\n");break;
//                case EFAULT:
//                    EXIT_TRACE("I/O error: Buffer out of range\n");break;
//                case EINTR:
//                    EXIT_TRACE("I/O error: Interruption\n");break;
//                case EINVAL:
//                    EXIT_TRACE("I/O error: Unable to read from file descriptor\n");break;
//                case EIO:
//                    EXIT_TRACE("I/O error: Generic I/O error\n");break;
//                case EISDIR:
//                    EXIT_TRACE("I/O error: Cannot read from a directory\n");break;
//                default:
//                    EXIT_TRACE("I/O error: Unrecognized error\n");break;
//            }
//            if(r==0) break;
//            bytes_read += r;
//        }
//        fragment_args.input_file.size = filestat.st_size;
//        fragment_args.input_file.buffer = preloading_buffer;
//    }
//
//    fragment_args.tid = 0;
//    fragment_args.nqueues = nqueues;
//    fragment_args.fd = fd;
//    
//    std::vector<SmartFIFOImpl<chunk_t*>*> fragment_to_refine, refine_to_deduplicate,
//                deduplicate_to_compress, dedup_compress_to_reorder;
//
//    std::vector<pthread_t> threads(1 + 3 * data._nb_threads + 1);
//    pthread_barrier_t barrier;
//    pthread_barrier_init(&barrier, nullptr, 1 + 1 + 3 * data._nb_threads + 1);
//
//    for (int i = 0; i < nqueues; ++i) {
//        unsigned int amount = 0;
//        if (i == nqueues - 1 && extra_queue) {
//            amount = data._nb_threads % MAX_THREADS_PER_QUEUE;
//        } else {
//            amount = MAX_THREADS_PER_QUEUE;
//        }
//
//        SmartFIFOImpl<chunk_t*>* f_to_r = new SmartFIFOImpl<chunk_t*>(1, "fragment_to_refine_" + std::to_string(i));
//        f_to_r->add_producer();
//        fragment_to_refine.push_back(f_to_r);
//
//        SmartFIFOImpl<chunk_t*>* r_to_d = new SmartFIFOImpl<chunk_t*>(1, "refine_to_deduplicate_" + std::to_string(i));
//        r_to_d->add_producers(amount);
//        refine_to_deduplicate.push_back(r_to_d);
//
//        SmartFIFOImpl<chunk_t*>* d_to_c = new SmartFIFOImpl<chunk_t*>(1, "deduplicate_to_compress_" + std::to_string(i));
//        d_to_c->add_producers(amount);
//        deduplicate_to_compress.push_back(d_to_c);
//
//        SmartFIFOImpl<chunk_t*>* dc_to_r = new SmartFIFOImpl<chunk_t*>(1, "dedupcompress_to_reorder_" + std::to_string(i));
//        dc_to_r->add_producers(2 * amount);
//        dedup_compress_to_reorder.push_back(dc_to_r);
//    }
//
//    for (int i = 0; i < nqueues; ++i) {
//        FIFOData& fragment_data = data._fifo_data[Layers::FRAGMENT][Layers::REFINE][FIFORole::PRODUCER][0];
//        fragment_args._output_fifos.push_back(new SmartFIFO<chunk_t*>(fragment_to_refine[i], fragment_data._n, fragment_data._reconfigure, fragment_data._change_step_after, fragment_data._new_step));
//    }
//
//    fragment_args.tid = 0;
//    fragment_args._barrier = &barrier;
//    fragment_args._timestamp_data = timestamp_datas.data();
//    pthread_create(threads.data(), nullptr, _FragmentSmart, thread_args_smart_copy_because_pthread(fragment_args));
//
//    for (int i = 0; i < data._nb_threads; ++i) {
//        int queue_id = i / MAX_THREADS_PER_QUEUE;
//
//        thread_args_smart refine_args;
//        refine_args.tid = i;
//        refine_args.nqueues = nqueues;
//        refine_args._barrier = &barrier;
//        FIFOData& refine = data._fifo_data[Layers::FRAGMENT][Layers::REFINE][FIFORole::CONSUMER][i];
//        refine_args._input_fifos.push_back(new SmartFIFO<chunk_t*>(fragment_to_refine[queue_id], refine._n, refine._reconfigure, refine._change_step_after, refine._new_step));
//
//        if (i < data._fifo_data[Layers::REFINE][Layers::DEDUPLICATE][FIFORole::PRODUCER].size()) {
//            FIFOData& refine_output = data._fifo_data[Layers::REFINE][Layers::DEDUPLICATE][FIFORole::PRODUCER][i];
//            refine_args._output_fifos.push_back(new SmartFIFO<chunk_t*>(refine_to_deduplicate[queue_id], refine_output._n, refine_output._reconfigure, refine_output._change_step_after, refine_output._new_step));
//            refine_args._timestamp_data = timestamp_datas.data() + 1 + i;
//            pthread_create(threads.data() + 1 + i, nullptr, _RefineSmart, thread_args_smart_copy_because_pthread(refine_args));
//        }
//
//        thread_args_smart deduplicate_args;
//        deduplicate_args.tid = i;
//        deduplicate_args.nqueues = nqueues;
//        deduplicate_args._barrier = &barrier;
//        if (i < data._fifo_data[Layers::REFINE][Layers::DEDUPLICATE][FIFORole::CONSUMER].size()) {
//            FIFOData& deduplicate_input = data._fifo_data[Layers::REFINE][Layers::DEDUPLICATE][FIFORole::CONSUMER][i];
//            deduplicate_args._input_fifos.push_back(new SmartFIFO<chunk_t*>(refine_to_deduplicate[queue_id], deduplicate_input._n, deduplicate_input._reconfigure, deduplicate_input._change_step_after, deduplicate_input._new_step));
//        }
//
//        
//        FIFOData& deduplicate_output = data._fifo_data[Layers::DEDUPLICATE][Layers::COMPRESS][FIFORole::PRODUCER][i];
//        deduplicate_args._output_fifos.push_back(new SmartFIFO<chunk_t*>(deduplicate_to_compress[queue_id], deduplicate_output._n, deduplicate_output._reconfigure, deduplicate_output._change_step_after, deduplicate_output._new_step));
//
//        FIFOData& deduplicate_extra = data._fifo_data[Layers::DEDUPLICATE][Layers::REORDER][FIFORole::PRODUCER][i];
//        deduplicate_args._extra_output_fifo = new SmartFIFO<chunk_t*>(dedup_compress_to_reorder[queue_id], deduplicate_extra._n, deduplicate_extra._reconfigure, deduplicate_extra._change_step_after, deduplicate_extra._new_step);
//        deduplicate_args._timestamp_data = timestamp_datas.data() + 1 + data._nb_threads + i;
//        pthread_create(threads.data() + 1 + data._nb_threads + i, nullptr, _DeduplicateSmart, thread_args_smart_copy_because_pthread(deduplicate_args));
//
//        thread_args_smart compress_args;
//        compress_args.tid = i;
//        compress_args.nqueues = nqueues;
//        compress_args._barrier = &barrier;
//        FIFOData& compress_input = data._fifo_data[Layers::DEDUPLICATE][Layers::COMPRESS][FIFORole::CONSUMER][i];
//        compress_args._input_fifos.push_back(new SmartFIFO<chunk_t*>(deduplicate_to_compress[queue_id], compress_input._n, compress_input._reconfigure, compress_input._change_step_after, compress_input._new_step));
//
//        FIFOData& compress_output = data._fifo_data[Layers::COMPRESS][Layers::REORDER][FIFORole::PRODUCER][i];
//        compress_args._output_fifos.push_back(new SmartFIFO<chunk_t*>(dedup_compress_to_reorder[queue_id], compress_output._n, compress_output._reconfigure, compress_output._change_step_after, compress_output._new_step));
//        compress_args._timestamp_data = timestamp_datas.data() + 1 + 2 * data._nb_threads + i;
//        pthread_create(threads.data() + 1 + 2 * data._nb_threads + i, nullptr, _CompressSmart, thread_args_smart_copy_because_pthread(compress_args));
//    }
//
//    thread_args_smart reorder_args;
//    for (int i = 0; i < nqueues; ++i) {
//        FIFOData& reorder_input = data._fifo_data[Layers::DEDUPLICATE][Layers::REORDER][FIFORole::CONSUMER][0];
//        reorder_args._input_fifos.push_back(new SmartFIFO<chunk_t*>(dedup_compress_to_reorder[i], reorder_input._n, reorder_input._reconfigure, reorder_input._change_step_after, reorder_input._new_step));
//    }
//
//    reorder_args.tid = 0;
//    reorder_args.nqueues = nqueues;
//    reorder_args._barrier = &barrier;
//    reorder_args._timestamp_data = timestamp_datas.data() + 1 + 3 * data._nb_threads;
//    pthread_create(threads.data() + 1 + 3 * data._nb_threads, nullptr, _ReorderSmart, thread_args_smart_copy_because_pthread(reorder_args));
//
//    pthread_barrier_wait(&barrier);
//    std::chrono::time_point<std::chrono::steady_clock> begin = std::chrono::steady_clock::now();
//
//    for (pthread_t& t: threads) {
//        pthread_join(t, nullptr);
//    }
//    
//    std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
//    unsigned long long diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
//
//    //clean up after preloading
//    if(data._preloading) {
//        free(preloading_buffer);
//    }
//
//    /* clean up with the src file */
//    if (!data._input_filename.empty())
//        close(fd);
//
//    int des_res = mbuffer_system_destroy();
//    assert(!des_res);
//
//    hashtable_destroy(cache, TRUE);
//
//    return { diff, timestamp_datas };
}


