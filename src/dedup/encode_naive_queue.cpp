#include <algorithm>
#include <fstream>
#include <future>
#include <iterator>
#include <queue>
#include <set>
#include <thread>
#include <vector>

#include "dedupdef.h"
#include "hashtable_private.h"
#include "encode_common.h"
#include "naive_queue.hpp"
#include "smart_fifo.h"
#include "hard_steps.h"

#include "encode_naive_queue_conf.h"

struct thread_args_naive {
    int fd;
    struct {
        void* buffer;
        size_t size;
    } input_file;

    /* std::vector<SmartFIFO<chunk_t*>*> _input_fifos;
    std::vector<SmartFIFO<chunk_t*>*> _output_fifos;
    std::vector<SmartFIFO<chunk_t*>*> _extra_output_fifos; */
    std::vector<NaiveQueueImpl<chunk_t*>*> _input_fifos, _output_fifos, _extra_output_fifos;
    std::vector<Observer<chunk_t*>*> _input_observers, _output_observers, _extra_output_observers;
    pthread_barrier_t* _barrier;
};

// ============================================================================
// Smart FIFO version
// ============================================================================

void FragmentNaiveQueue(thread_args_naive const& args) {
    pthread_barrier_wait(args._barrier);
    size_t preloading_buffer_seek = 0;
    int qid = 0;
    int fd = args.fd;
    int r;

    sequence_number_t anchorcount = 0;

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

    int count = 0;

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
        if(chunk==NULL)
            EXIT_TRACE("Memory allocation failed.\n");

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
            /* auto push_res = */ args._output_fifos[qid]->push(chunk);
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
                    /* auto push_res = */ args._output_fifos[qid]->push(chunk);
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

    for (NaiveQueueImpl<chunk_t*>* fifo: args._output_fifos) {
        fifo->terminate();
    }

    free(rabintab);
    free(rabinwintab);

    // printf("Fragment: %d\n", count);
}

std::mutex _split_mutex;
std::map<NaiveQueueImpl<chunk_t*>*, std::vector<uint64_t>> _split_data;

void RefineNaiveQueue(thread_args_naive const& args) {
    // Globals::SmartFIFOTSV& data = *args._timestamp_data;
    pthread_barrier_wait(args._barrier);
    int r;

    chunk_t *temp;
    chunk_t *chunk;
    u32int * rabintab = (u32int*)malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*)malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

    std::vector<uint64_t> split_data;
    split_data.reserve(300000);

    bool push_work_output = true;

    r=0;

    // Begin clock after poping an element from the queue and upon performing 
    // a new split.
    while (TRUE) {
        //if no item for process, get a group of items from the pipeline
// #if TIMED_PUSH == 1
//         auto [value, lock, critical, unlock] = args._input_fifos[0]->timed_pop();
// #else
        auto value = args._input_fifos[0]->pop();
// #endif

        if (!value) {
            break;
        }

        chunk = *value;
        TP begin = SteadyClock::now();

        //get one item
        rabininit(rf_win, rabintab, rabinwintab);

        int split;
        sequence_number_t chcount = 0;
        do {
            //Find next anchor with Rabin fingerprint
            //TP offset_begin = SteadyClock::now();
            int offset = rabinseg((uchar*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, rf_win, rabintab, rabinwintab);
            //auto offset_diff = diff(offset_begin, SteadyClock::now());
            //Can we split the buffer?
            if(offset < chunk->uncompressed_data.n) {
                //Allocate a new chunk and create a new memory buffer
                temp = (chunk_t *)malloc(sizeof(chunk_t));
                if(temp==NULL) EXIT_TRACE("Memory allocation failed.\n");
                temp->header.state = chunk->header.state;
                temp->sequence.l1num = chunk->sequence.l1num;

                //split it into two pieces
                //TP split_begin = SteadyClock::now();
                r = mbuffer_split(&chunk->uncompressed_data, &temp->uncompressed_data, offset);
                //auto split_diff = diff(split_begin, SteadyClock::now());
                // split_data.push_back(split_diff);
                if(r!=0) EXIT_TRACE("Unable to split memory buffer.\n");

                //Set correct state and sequence numbers
                chunk->sequence.l2num = chcount;
                chunk->isLastL2Chunk = FALSE;
                chcount++;

                //put it into send buffer
#if TIMED_PUSH == 1
                bool push_result = args._output_fifos[0]->generic_push(args._output_observers[0], chunk);
#else
                args._output_fifos[0]->push(chunk);
#endif

#if TIMED_PUSH == 1
                if (push_result && push_work_output) {
                    auto d = diff(begin, SteadyClock::now());
                    // printf("%llu\n", d - offset_diff);
                    push_work_output = args._output_observers[0]->add_work_time(args._output_fifos[0], d);
                }
#endif

                // Reset begin because we are technically producing another element by performing
                // a new split from now on.
                begin = SteadyClock::now();

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
#if TIMED_PUSH == 1
                bool push_result = args._output_fifos[0]->generic_push(args._output_observers[0], chunk);
#else
                args._output_fifos[0]->push(chunk);
#endif

#if TIMED_PUSH == 1
                if (push_result && push_work_output) {
                    auto d = diff(begin, SteadyClock::now());
                    // printf("%llu\n", d);
                    push_work_output = args._output_observers[0]->add_work_time(args._output_fifos[0], d);
                }
#endif

                //prepare for next iteration
                chunk = NULL;
                split = 0;
            }
        } while(split);
    }

    free(rabintab);
    free(rabinwintab);

    //shutdown
    args._output_fifos[0]->terminate();

    _split_mutex.lock();
    _split_data[args._input_fifos[0]] = std::move(split_data);
    _split_mutex.unlock();
    // printf("Refine: pop = %d, push = %d\n", pop_count, push_count);
}

void DeduplicateNaiveQueue(thread_args_naive const& args) {
    pthread_barrier_wait(args._barrier);
    chunk_t *chunk;

    int in_a_row = 0;
    bool last_was_compressed = false;
    constexpr const int row_limit = 100;

    bool push_work_input = true;
    bool push_work_output = true;
    bool push_work_extra = true;

    while (1) {
        //if no items available, fetch a group of items from the queue
// #if TIMED_PUSH == 1
//         auto [value, lock, critical, unlock] = args._input_fifos[0]->timed_pop();
// #else
        auto value = args._input_fifos[0]->pop();
// #endif

        if (!value) {
            break;
        }

        //get one chunk
        chunk = *value;

        TP begin = SteadyClock::now();
        //Do the processing
        auto [isDuplicate, lock_idx] = sub_Deduplicate(chunk);

        auto d = diff(begin, SteadyClock::now());
        //Enqueue chunk either into compression queue or into send queue
        if(!isDuplicate) {
#if DEDUP_TO_REORDER == 1
            if (last_was_compressed) {
                ++in_a_row;

                if (in_a_row == row_limit) {
                    // printf("%p: %d compress in a row\n", args._output_fifos[0], row_limit);
                    args._extra_output_fifos[0]->force_push();
                    in_a_row = 0;
                }
            } else {
                // printf("%p: reset to compress\n", args._extra_output_fifos[0]);
                last_was_compressed = true;
                in_a_row = 1;
            }
#endif
            last_was_compressed = true;
#if TIMED_PUSH == 1
            bool push_res = args._output_fifos[0]->generic_push(args._output_observers[0], chunk);
            if (push_res && push_work_output) {
                push_work_output = args._output_observers[0]->add_work_time(args._output_fifos[0], d);
            }
#else
            args._output_fifos[0]->push(chunk);
#endif
        } else {
#if DEDUP_TO_COMPRESS == 1
            if (!last_was_compressed) {
                ++in_a_row;

                if (in_a_row == row_limit) {
                    // printf("%p: %d deduplicate in a row\n", args._extra_output_fifos[0], row_limit);
                    args._output_fifos[0]->force_push();
                    in_a_row = 0;
                }
            } else {
                // printf("%p: reset to deduplicate\n", args._output_fifos[0]);
                last_was_compressed = false;
                in_a_row = 1;
            }
#endif
            last_was_compressed = false;

#if TIMED_PUSH == 1
            bool push_res = args._extra_output_fifos[0]->generic_push(args._extra_output_observers[0], chunk);
            if (push_res && push_work_extra) {
                push_work_extra = args._extra_output_observers[0]->add_work_time(args._extra_output_fifos[0], diff(begin, SteadyClock::now()));
            }
#else
            args._extra_output_fifos[0]->push(chunk);
#endif
        }

#if TIMED_PUSH == 1
        if (push_work_input) {
            push_work_input = args._input_observers[0]->add_work_time(args._input_fifos[0], d);
        }
#endif
    }

    args._output_fifos[0]->terminate();
    args._extra_output_fifos[0]->terminate();
}

void CompressNaiveQueue(thread_args_naive const& args) {
    pthread_barrier_wait(args._barrier);
    chunk_t * chunk;

    bool push_work_input = true;
    bool push_work_output = true;

    int count = 0;
    int pop_count = 0;

    while(1) {
#if COMPRESS_CROSS_POP == 1
        auto [value, success] = args._input_fifos[0]->cross_pop_no_timing(std::chrono::nanoseconds(500), args._output_fifos[0]);
        // auto [value, lock, critical, unlock] = timing_data;
#else
// #if TIMED_PUSH == 1
//        auto timing_data = args._input_fifos[0]->timed_pop();
//        auto [value, lock, critical, unlock] = timing_data;
// #else
        auto value = args._input_fifos[0]->pop();
// #endif
#endif

#if COMPRESS_CROSS_POP == 1
        if (!success) {
            continue;
        }
#endif

        if (!value) {
            break;
        }

        ++pop_count;

        //fetch one item
        chunk = *value;

        TP begin = SteadyClock::now();
        sub_Compress(chunk);
#if TIMED_PUSH == 1
        bool push_res = args._output_fifos[0]->generic_push(args._output_observers[0], chunk);
#else
        args._output_fifos[0]->push(chunk);

#endif
        ++count;

        auto d = diff(begin, SteadyClock::now());
#if TIMED_PUSH == 1
        if (push_res && push_work_output) {
            push_work_output = args._output_observers[0]->add_work_time(args._output_fifos[0],  d);
        }

        if (push_work_input) {
            push_work_input = args._input_observers[0]->add_work_time(args._input_fifos[0], d);
        }
#endif
    }

    args._output_fifos[0]->terminate();
}

struct ReorderComputeData {
    SearchTree T;
    sequence_number_t* chunks_per_anchor;
};

// static ReorderComputeData c_data;

void ReorderNaiveQueue(thread_args_naive const& args) {
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
    if(chunks_per_anchor == NULL) 
        EXIT_TRACE("Error allocating memory\n");

    memset(chunks_per_anchor, 0, chunks_per_anchor_max * sizeof(sequence_number_t));

    fd = create_output_file(_g_data->_output_filename.c_str());
    int qid = 0;

    std::map<NaiveQueueImpl<chunk_t*>*, bool> push_work_inputs;
    for (NaiveQueueImpl<chunk_t*>* queue: args._input_fifos) {
        push_work_inputs[queue] = true;
    }

    while(1) {
        std::optional<chunk_t*> value;
        NaiveQueueImpl<chunk_t*>* current_fifo;
        Observer<chunk_t*>* current_observer;
        for (int i = 0; i < args._input_fifos.size(); ++i) {
            // size_t lock, critical, unlock;
// #if TIMED_PUSH == 1
//            std::tie(value, lock, critical, unlock) = args._input_fifos[qid]->timed_pop();
// #else
            value = args._input_fifos[qid]->pop();
// #endif
            current_fifo = args._input_fifos[qid];
            current_observer = args._input_observers[qid];
            qid = (qid + 1) % args._input_fifos.size();
            if (value) {
                break;
            }
        }

        if (!value) {
            break;
        }

        chunk = *value;
        if (chunk == NULL) 
            break;

        TP now = SteadyClock::now();
        //Double size of sequence number array if necessary
        if(chunk->sequence.l1num >= chunks_per_anchor_max) {
            chunks_per_anchor = (sequence_number_t*)realloc(chunks_per_anchor, 2 * chunks_per_anchor_max * sizeof(sequence_number_t));
            if(chunks_per_anchor == NULL) 
                EXIT_TRACE("Error allocating memory\n");
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

#if TIMED_PUSH == 1
            if (push_work_inputs[current_fifo]) {
                auto d = diff(now, SteadyClock::now());
                push_work_inputs[current_fifo] = current_observer->add_work_time(current_fifo, d);
            }
#endif
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
            if(chunks_per_anchor[next.l1num] != 0 && next.l2num == chunks_per_anchor[next.l1num]) 
                sequence_inc_l1(&next);

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

        if (push_work_inputs[current_fifo]) {
            push_work_inputs[current_fifo] = current_observer->add_work_time(current_fifo, diff(now, SteadyClock::now()));
        }
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
                throw std::runtime_error("L2 sequence number mismatch");
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
        if(chunks_per_anchor[next.l1num] != 0 && next.l2num == chunks_per_anchor[next.l1num]) 
            sequence_inc_l1(&next);
    }

    close(fd);

    free(chunks_per_anchor);
}

std::vector<std::unique_ptr<thread_args_naive>> _thread_args_naive_vector;

thread_args_naive* thread_args_naive_copy_because_pthread(thread_args_naive const& src) {
    thread_args_naive* ptr = new thread_args_naive;
    *ptr = src;
    _thread_args_naive_vector.push_back(std::unique_ptr<thread_args_naive>(ptr));
    return ptr;
}

void* _FragmentNaiveQueue(void* args) {
    FragmentNaiveQueue(*static_cast<thread_args_naive*>(args));
    return nullptr;
}

void* _RefineNaiveQueue(void* args) {
    RefineNaiveQueue(*static_cast<thread_args_naive*>(args));
    return nullptr;
}

void* _DeduplicateNaiveQueue(void* args) {
    DeduplicateNaiveQueue(*static_cast<thread_args_naive*>(args));
    return nullptr;
}

void* _CompressNaiveQueue(void* args) {
    CompressNaiveQueue(*static_cast<thread_args_naive*>(args));
    return nullptr;
}

void* _ReorderNaiveQueue(void* args) {
    ReorderNaiveQueue(*static_cast<thread_args_naive*>(args));
    return nullptr;
}

static void _Encode(/* std::vector<Globals::SmartFIFOTSV>& timestamp_datas, */ DedupData& data, int fd, size_t filesize, void* buffer, tp& begin, tp& end) {
    LayerData& fragment = data._layers_data[Layers::FRAGMENT];
    LayerData& refine = data._layers_data[Layers::REFINE];
    LayerData& deduplicate = data._layers_data[Layers::DEDUPLICATE];
    LayerData& compress = data._layers_data[Layers::COMPRESS];
    LayerData& reorder = data._layers_data[Layers::REORDER];

    // Map the ID of a FIFO in Lua to the actual FIFO in C++.
    // Used when creating local buffers.
    // Filled when FIFOs are allocated in alloc_queues.
    std::map<int, NaiveQueueMaster<chunk_t*>*> ids_to_fifos;
    // Similar to the previous map, except it maps the ID of a FIFO to the 
    // observer to which it is bound.
    std::map<int, Observer<chunk_t*>*> ids_to_observers;

    // Observers, named after the producing side. Compress is the odd case as both
    // deduplicate and compress produce for reorder. Deduplicate observers are the 
    // one that send to compress, compress observers are the ones that send to reorder
    // and are shared with deduplicate
    //
    // Using pointers instead of vector because fuck type requirements.
    // Not using smart pointers because why should I?
    Observer<chunk_t*>* fragment_observers, *refine_observers, *deduplicate_observers, *compress_observers;
    size_t nb_fragment_observers, nb_refine_observers, nb_deduplicate_observers, nb_compress_observers;

    std::vector<Observer<chunk_t*>*> all_observers;
    
    // Allocate queues in fifos. data are the data about the layer. description 
    // is used to identify the FIFO through a string. observers is the set of 
    // observers to bind to the FIFOs. n_producers is the amount of producers that
    // will work on said FIFO. iter_prod is the amount of elements that will be
    // produced. n_threads is the amount of producers and consumers that will work
    // on the FIFO.
    auto alloc_queues = [&ids_to_fifos, &ids_to_observers, &data, &all_observers](NaiveQueueMaster<chunk_t*>** fifos, LayerData const& data, std::string&& description, Observer<chunk_t*>** observers, size_t* nb_observers, std::string const& observer_description, uint64_t iter_prod, int choice_step = 0, int dephase = 0, int prod_step = 0, int cons_step = 0) {
        std::cout << "Queue allocation" << std::endl;
        std::set<int> fifo_ids;
        (void)description;

        compute_fifo_ids_for_layer(fifo_ids, data);

        // *fifos = (NaiveQueueMaster<chunk_t*>*)std::aligned_alloc(fifo_ids.size() * sizeof(NaiveQueueMaster<chunk_t*>), alignof(NaiveQueueMaster<chunk_t*>));
        // *observers = (Observer<chunk_t*>*)std::aligned_alloc(fifo_ids.size() * sizeof(Observer<chunk_t*>), alignof(Observer<chunk_t*>));
        *fifos = new NaiveQueueMaster<chunk_t*>[fifo_ids.size()];
        *observers = new Observer<chunk_t*>[fifo_ids.size()];
        *nb_observers = fifo_ids.size();

        if (!*fifos) {
            std::cout << "OOM (fifos)" << std::endl;
        } else {
            std::cout << "Fifos at " << *fifos << std::endl;
        }

        if (!*observers) {
            std::cout << "OOM (observers)" << std::endl;
        } else {
            std::cout << "Observers at " << *observers << std::endl;
        }

        auto iter = fifo_ids.begin();
        for (int i = 0; i < fifo_ids.size(); ++i, ++iter) {
            // fprintf(stderr, "Using hardcoded size of master!\n");
            // std::cout << i << ", " << (*fifos) + i << ", " << (*observers) + i << std::endl;
            // new ((*fifos) + i) NaiveQueueMaster<chunk_t*>(500000, data.get_producing_threads(*iter));
            // new ((*observers) + i) Observer<chunk_t*>(iter_prod, data.get_interacting_threads(*iter));
            ((*fifos) + i)->delayed_init(1024 * 1024, data.get_producing_threads(*iter));
            ((*observers) + i)->delayed_init(observer_description, iter_prod, choice_step, dephase, prod_step, cons_step);

            all_observers.push_back((*observers) + i);
        }

        iter = fifo_ids.begin();
        for (int i = 0; i < fifo_ids.size(); ++i, ++iter) {
            // (*fifos)[i].set_description(std::move(description));
            ids_to_fifos[*iter] = *fifos + i;
            ids_to_observers[*iter] = *observers + i;
        }
    };

    auto coarse_fine = [&data]() -> std::tuple<int, int> {
        /* std::ifstream stream(data._input_filename, std::ios::in | std::ios::binary);
        return { 0, 0 }; */
        struct stat sb;
        stat(data._input_filename.c_str(), &sb);
        return { int(sb.st_size / ANCHOR_JUMP), int(sb.st_size / ANCHOR_JUMP) * 512 };
        // return { sb.st_size / MAXBUF, sb.st_size / MAXBUF * 
    };

    auto [coarse, fine] = coarse_fine();

    std::cout << "Starting to allocate queues" << std::endl;
    NaiveQueueMaster<chunk_t*>* fragment_to_refine, *refine_to_deduplicate, *deduplicate_to_compress, *dedupcompress_to_reorder;
    alloc_queues(&fragment_to_refine, fragment, "fragment to refine", &fragment_observers, &nb_fragment_observers, "Fragment to Refine", coarse);
    alloc_queues(&refine_to_deduplicate, refine, "refine to deduplicate", &refine_observers, &nb_refine_observers, "Refine to Deduplicate", fine, 0, 0, REFINE_TO_DEDUP, DEDUP_FROM_REFINE);
    alloc_queues(&deduplicate_to_compress, deduplicate, "deduplicate to compress", &deduplicate_observers, &nb_deduplicate_observers, "Deduplicate to Compress", fine, 0, 0, DEDUP_TO_COMPRESS_STEP, COMPRESS_FROM_DEDUP);
    
    {
        std::set<int> sreorder;
        compute_fifo_ids_for_reorder(sreorder, deduplicate, compress);
        // dedupcompress_to_reorder = new SmartFIFOImpl<chunk_t*>[sreorder.size()];
        // dedupcompress_to_reorder = (NaiveQueueMaster<chunk_t*>*)std::aligned_alloc(sizeof(NaiveQueueMaster<chunk_t*>) * sreorder.size(), alignof(NaiveQueueMaster<chunk_t*>));
        dedupcompress_to_reorder = new NaiveQueueMaster<chunk_t*>[sreorder.size()];
        compress_observers = new Observer<chunk_t*>[sreorder.size()];
        nb_compress_observers = sreorder.size();
        auto iter = sreorder.begin();
        for (int i = 0; i < sreorder.size(); ++i, ++iter) {
            // fprintf(stderr, "Using hardcoded size of master!\n");
            // new (dedupcompress_to_reorder + i) NaiveQueueMaster<chunk_t*>(500000, 10);
            dedupcompress_to_reorder[i].delayed_init(1024 * 1024, data.get_producing_threads(*iter));
            compress_observers[i].delayed_init("Dedup / Compress to Reorder", fine, 0, 0, DEDUP_TO_REORDER_STEP, REORDER_FROM_DEDUP);
            all_observers.push_back(compress_observers + i);
        }

        iter = sreorder.begin();
        for (int i = 0; i < sreorder.size(); ++i, ++iter) {
            ids_to_fifos[*iter] = dedupcompress_to_reorder + i;
            ids_to_observers[*iter] = compress_observers + i;
        }
    }

    std::cout << "Allocated all queues" << std::endl;

    pthread_barrier_t barrier;
    unsigned int nb_threads = data.get_total_threads();
    pthread_barrier_init(&barrier, nullptr, nb_threads + 1);

    struct Runner {
        pthread_t* thread;
        void* (*routine)(void*);
        thread_args_naive* args;

        void operator()() {
            pthread_create(thread, nullptr, routine, args);
        }
    };
    std::vector<Runner> runners;

    auto launch_stage = [&barrier, &data, &ids_to_fifos, &fd, &filesize, &buffer, &ids_to_observers, &runners](void* (*routine)(void*), LayerData const& layer_data, bool extra, std::string&& debug_msg) {
        thread_args_naive* args = new thread_args_naive[layer_data.get_total_threads()];
        /*for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            args[i]._times.resize(100000000);
        }*/

        pthread_t* threads = new pthread_t[layer_data.get_total_threads()];

        auto generate_views = [&data, &ids_to_fifos, &ids_to_observers](std::vector<NaiveQueueImpl<chunk_t*>*>& target, std::vector<Observer<chunk_t*>*>& observers, bool producer, std::map<int, FIFOData> const& fifo_ids, bool phantom = false) {
            for (auto const& [fifo, fifo_data]: fifo_ids) {
                // FIFOData& fifo_data = data._fifo_data[fifo];
                // target.push_back(ids_to_fifos[fifo]->view(producer, fifo_data._n, fifo_data._reconfigure, fifo_data._change_step_after, fifo_data._new_step));
                NaiveQueueImpl<chunk_t*>* impl = ids_to_fifos[fifo]->view(producer, fifo_data._n, false, 0, 0);
                target.push_back(impl);

                Observer<chunk_t*>* obs = ids_to_observers[fifo];
                observers.push_back(obs);
                if (phantom) {
                    if (producer) {
                        obs->add_phantom_producer(impl);
                    } else {
                        obs->add_phantom_consumer(impl);
                    }
                } else {
                    if (producer) {
                        obs->add_producer(impl);
                    } else {
                        obs->add_consumer(impl);
                    }
                }
            }
        };

        int i = 0;
        for (ThreadData const& thread_data: layer_data._thread_data) {
            generate_views(args[i]._output_fifos, args[i]._output_observers, true, thread_data._outputs);
            generate_views(args[i]._input_fifos, args[i]._input_observers, false, thread_data._inputs);

            if (extra) {
                generate_views(args[i]._extra_output_fifos, args[i]._extra_output_observers, true, thread_data._extras, true);
            }

            args[i]._barrier = &barrier;
            args[i].fd = fd;

            if (data._preloading) {
                args[i].input_file.size = filesize;
                args[i].input_file.buffer = buffer;
            }

            Runner runner;
            runner.thread = threads + i;
            runner.routine = routine;
            runner.args = args + i;
            runners.push_back(runner);
            // pthread_create(threads + i, nullptr, routine, args + i);
            ++i;
        }

        std::cout << debug_msg << std::endl;
        std::cout << "Input FIFOs " << std::endl;

        for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            std::cout << "\tThread " << i << std::endl;
            thread_args_naive const& a = args[i];
            if (a._input_fifos.size() != a._input_observers.size()) {
                std::cout << "Discrepancy between number of input FIFOs and number of input observers" << std::endl;
            }
            int j = 0;
            for (NaiveQueueImpl<chunk_t*>* fifo: a._input_fifos) {
                std::cout << "\t\tFIFO = " << fifo->get_master() << ", observer = " << a._input_observers[j] << std::endl;
                ++j;
            }
        }

        std::cout << "Output FIFOs " << std::endl;

        for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            std::cout << "\tThread " << i << std::endl;
            thread_args_naive const& a = args[i];
            if (a._output_fifos.size() != a._output_observers.size()) {
                std::cout << "Discrepancy between number of output FIFOs and number of output observers" << std::endl;
            }
            int j = 0;
            for (NaiveQueueImpl<chunk_t*>* fifo: a._output_fifos) {
                std::cout << "\t\tFIFO = " << fifo->get_master() << ", observer = " << a._output_observers[j] << std::endl;
                ++j;
            }
        }

        std::cout << "Extra output FIFOs " << std::endl;
        
        for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            std::cout << "\tThread " << i << std::endl;
            thread_args_naive const& a = args[i];
            if (a._extra_output_fifos.size() != a._extra_output_observers.size()) {
                std::cout << "Discrepancy between number of extra output FIFOs and number of extra output observers" << std::endl;
            }

            int j = 0;
            for (NaiveQueueImpl<chunk_t*>* fifo: a._extra_output_fifos) {
                std::cout << "\t\tFIFO = " << fifo->get_master() << ", observer = " << a._extra_output_observers[j] << std::endl;
                ++j;
            }
        }


        return std::tuple<pthread_t*, thread_args_naive*>(threads, args);
    };

    auto fragment_stage = launch_stage(_FragmentNaiveQueue, fragment, false, "fragment");
    auto refine_stage = launch_stage(_RefineNaiveQueue, refine, false, "refine");
    auto deduplicate_stage = launch_stage(_DeduplicateNaiveQueue, deduplicate, true, "deduplicate");
    auto compress_stage = launch_stage(_CompressNaiveQueue, compress, false, "compress");
    auto reorder_stage = launch_stage(_ReorderNaiveQueue, reorder, false, "reorder");

    for (Observer<chunk_t*>* obs: all_observers) {
        obs->set_first_reconfiguration_n(OBS_LIMIT);
        obs->set_second_reconfiguration_n(50);
    }

    std::cout << "Starting runners" << std::endl;
    for (Runner& runner: runners) {
        runner();
    }
    // return;

    // std::vector<std::vector<deduplicate_push>> dedup_data(deduplicate.get_total_threads());

    // Join all threads and free dynamically allocated memory
    auto terminate = [](auto stage, LayerData const& layer_data) {
        auto [threads, args] = stage;
        for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            pthread_join(threads[i], nullptr);
        }
    };

    std::set<NaiveQueueImpl<chunk_t*>*> naive_queues_addr;
    auto clear_mem = [&naive_queues_addr](auto stage, LayerData const& layer_data) {
        auto [threads, args] = stage;
        delete[] threads;

        for (int i = 0; i < layer_data.get_total_threads(); ++i) {
            for (NaiveQueueImpl<chunk_t*>* queue: args[i]._input_fifos) {
                naive_queues_addr.insert(queue);
            }

            for (NaiveQueueImpl<chunk_t*>* queue: args[i]._output_fifos) {
                naive_queues_addr.insert(queue);
            }

            for (NaiveQueueImpl<chunk_t*>* queue: args[i]._extra_output_fifos) {
                naive_queues_addr.insert(queue);
            }
        }

        delete[] args;
    };

    pthread_barrier_wait(&barrier);

    begin = sc::now();

    terminate(fragment_stage, fragment);
    terminate(refine_stage, refine);
    terminate(deduplicate_stage, deduplicate);
    terminate(compress_stage, compress);
    terminate(reorder_stage, reorder);

    end = sc::now();

    clear_mem(fragment_stage, fragment);
    clear_mem(refine_stage, refine);
    clear_mem(deduplicate_stage, deduplicate);
    clear_mem(compress_stage, compress);
    clear_mem(reorder_stage, reorder);

    auto dump_observers = [](Observer<chunk_t*>* observers, size_t n_observers, std::optional<std::string> const& filename_base, std::string const& layer) -> void {
        if (!filename_base)
            return;

        for (int i = 0; i < n_observers; ++i) {
            std::ostringstream filename;
            filename << *filename_base << "_" << layer << "_" << i << ".txt";
            std::ofstream stream(filename.str().c_str(), std::ios::out);
            stream << observers[i].serialize();
            stream.close();
        }
    };
    printf("Fragment\n");
    dump_observers(fragment_observers, nb_fragment_observers, data._observers, "fragment");
    delete[] fragment_observers;

    printf("Refine\n");
    dump_observers(refine_observers, nb_refine_observers, data._observers, "refine");
    delete[] refine_observers;

    printf("Deduplicate\n");
    dump_observers(deduplicate_observers, nb_deduplicate_observers, data._observers, "deduplicate");
    delete[] deduplicate_observers;

    printf("Compress\n");
    dump_observers(compress_observers, nb_compress_observers, data._observers, "compress");
    delete[] compress_observers;

    /* for (auto const& [queue, data]: _split_data) {
        std::cout << queue << ":" << std::endl;
        std::copy(data.begin(), data.end(), std::ostream_iterator<uint64_t>(std::cout, ","));
    } */

    for (NaiveQueueImpl<chunk_t*>* addr: naive_queues_addr) {
        delete addr;
    }

    delete[] fragment_to_refine;
    delete[] refine_to_deduplicate;
    delete[] deduplicate_to_compress;
    delete[] dedupcompress_to_reorder;
#ifndef REORDER_WRITE_AHEAD
    // Write();
#endif

    // int pos = 0;
    /* for (auto& v: dedup_data) {
        for (auto const& time: v) {
            unsigned long long int diff = std::chrono::duration_cast<std::chrono::nanoseconds>(time._tp - Globals::_start_time).count();
            if (time._reorder) {
                printf("[Deduplicate %d, log] %llu reorder\n", pos, diff);
            } else {
                printf("[Deduplicate %d, log] %llu compressr\n", pos, diff);
            }
        }
    } */
}

void FragmentNumbers(std::queue<chunk_t*>& out, int fd, size_t filesize, void* buffer) {
    size_t preloading_buffer_seek = 0;
    int r;

    sequence_number_t anchorcount = 0;

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
        if(chunk==NULL)
            EXIT_TRACE("Memory allocation failed.\n");

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
            size_t max_read = MIN(MAXBUF, filesize-preloading_buffer_seek);
            memcpy((uchar*)chunk->uncompressed_data.ptr+bytes_left, (uchar*)buffer+preloading_buffer_seek, max_read);
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
            out.push(chunk);
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
                    out.push(chunk);

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

    free(rabintab);
    free(rabinwintab);
}

unsigned int RefineNumbers(std::queue<chunk_t*>& in, std::queue<chunk_t*>& out) {
    // fprintf(stderr, "AU SECOURS JE COMPRENDS RIEN\n");
    int r;

    chunk_t *temp;
    chunk_t *chunk;
    u32int * rabintab = (u32int*)malloc(256*sizeof rabintab[0]);
    u32int * rabinwintab = (u32int*)malloc(256*sizeof rabintab[0]);
    if(rabintab == NULL || rabinwintab == NULL) {
        EXIT_TRACE("Memory allocation failed.\n");
    }

    r=0;
    std::atomic<bool> failed;
    failed.store(false, std::memory_order_release);

    // Begin clock after poping an element from the queue and upon performing 
    // a new split.
    while (!in.empty()) {
        // fprintf(stderr, "About to pop from FIFO\n");
        chunk = in.front();
        // fprintf(stderr, "Fronted from FIFO\n");
        in.pop();
        // fprintf(stderr, "Poped from FIFO\n");

        //get one item
        rabininit(rf_win, rabintab, rabinwintab);

        int split;
        sequence_number_t chcount = 0;
        do {
            // std::cerr << "DO" << std::endl;
            //Find next anchor with Rabin fingerprint
            //TP offset_begin = SteadyClock::now();
            int offset = rabinseg((uchar*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, rf_win, rabintab, rabinwintab);
            //auto offset_diff = diff(offset_begin, SteadyClock::now());
            //Can we split the buffer?
            if(offset < chunk->uncompressed_data.n) {
                //Allocate a new chunk and create a new memory buffer
                temp = (chunk_t *)malloc(sizeof(chunk_t));
                if(temp==NULL) { EXIT_TRACE("Memory allocation failed.\n"); }
                temp->header.state = chunk->header.state;
                temp->sequence.l1num = chunk->sequence.l1num;

                //split it into two pieces
                //TP split_begin = SteadyClock::now();
                r = mbuffer_split(&chunk->uncompressed_data, &temp->uncompressed_data, offset);
                // std::cerr << "Split = " << r << std::endl;
                //auto split_diff = diff(split_begin, SteadyClock::now());
                // split_data.push_back(split_diff);
                if(r!=0) { fprintf(stderr, "FUCKING KILL ME\n"); failed.store(true, std::memory_order_release); EXIT_TRACE("Unable to split memory buffer.\n"); }

                //Set correct state and sequence numbers
                chunk->sequence.l2num = chcount;
                chunk->isLastL2Chunk = FALSE;
                chcount++;

                //put it into send buffer

                // fprintf(stderr, "About to push in FIFO (1)\n");
                out.push(chunk);
                // fprintf(stderr, "Pushed in FIFO (1)\n");

                chunk = temp;
                split = 1;
            } else {
                //End of buffer reached, don't split but simply enqueue it
                //Set correct state and sequence numbers
                chunk->sequence.l2num = chcount;
                chunk->isLastL2Chunk = TRUE;
                chcount++;

                //put it into send buffer
                // fprintf(stderr, "About to push in FIFO (2)\n");
                out.push(chunk);
                // fprintf(stderr, "Pushed in FIFO (2)\n");

                //prepare for next iteration
                chunk = NULL;
                split = 0;
            }
        } while(split);

        // fprintf(stderr, "Done with chunk\n");
    }

    // std::cerr << rabintab << ", " << rabinwintab << ", " << failed.load(std::memory_order_acquire) << std::endl;
    free(rabintab);
    free(rabinwintab);

    return 0;
}

std::tuple<unsigned int, unsigned int> DeduplicateNumbers(std::queue<chunk_t*>& in) {
    chunk_t *chunk;
    int n_compress = 0, n_deduplicate = 0;

    std::set<chunk_t*> chunks;

    while (!in.empty()) {
        chunk = in.front();
        in.pop();

        auto [isDuplicate, lock_idx] = sub_Deduplicate(chunk);

        //Enqueue chunk either into compression queue or into send queue
        if(!isDuplicate) {
            n_compress++;
        } else {
            n_deduplicate++;
        }

        chunks.insert(chunk);
    }

    return { n_deduplicate, n_compress };
}

void _EncodeForNumbers(DedupData&, int fd, size_t filesize, void* buffer, tp&, tp&) {
    std::queue<chunk_t*> fragments;
    FragmentNumbers(fragments, fd, filesize, buffer);
    unsigned int n_fragment = fragments.size();

    // std::cout << "Fragment done" << std::endl;

    std::queue<chunk_t*> refined;
    RefineNumbers(fragments, refined);
    // std::cerr << "RefineNumbers done" << std::endl;
    unsigned int n_refine = refined.size();

    // std::cout << "Refine done" << std::endl;

    auto [n_deduplicate, n_compress] = DeduplicateNumbers(refined);

    std::cout << "Fragments = " << n_fragment << std::endl << "Refined = " << n_refine << std::endl << "Deduplicate / Compress = " << n_deduplicate << " " << n_compress << " => " << float(n_deduplicate) / float(n_refine) * 100 << "% of deduplicate" << std::endl;
}

void EncodeForNumbers(DedupData& data) {
    EncodeBase(data, [](DedupData& data, int fd, size_t filesize, void* buffer, tp& begin, tp& end) {
                _EncodeForNumbers(data, fd, filesize, buffer, begin, end);
            });
}

// std::tuple<unsigned long long, std::vector<Globals::SmartFIFOTSV>> EncodeSmart(DedupData& data) {
unsigned long long EncodeNaiveQueue(DedupData& data) {
    // std::vector<Globals::SmartFIFOTSV> timestamp_datas;
    // I don't have std::bind_front :(
    return EncodeBase(data, [](DedupData& data, int fd, size_t filesize, void* buffer, tp& begin, tp& end) {
                _Encode(data, fd, filesize, buffer, begin, end);
            });
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
//    struct thread_args_naive fragment_args;
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
//    pthread_create(threads.data(), nullptr, _FragmentNaiveQueue, thread_args_naive_copy_because_pthread(fragment_args));
//
//    for (int i = 0; i < data._nb_threads; ++i) {
//        int queue_id = i / MAX_THREADS_PER_QUEUE;
//
//        thread_args_naive refine_args;
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
//            pthread_create(threads.data() + 1 + i, nullptr, _RefineNaiveQueue, thread_args_naive_copy_because_pthread(refine_args));
//        }
//
//        thread_args_naive deduplicate_args;
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
//        pthread_create(threads.data() + 1 + data._nb_threads + i, nullptr, _DeduplicateNaiveQueue, thread_args_naive_copy_because_pthread(deduplicate_args));
//
//        thread_args_naive compress_args;
//        compress_args.tid = i;
//        compress_args.nqueues = nqueues;
//        compress_args._barrier = &barrier;
//        FIFOData& compress_input = data._fifo_data[Layers::DEDUPLICATE][Layers::COMPRESS][FIFORole::CONSUMER][i];
//        compress_args._input_fifos.push_back(new SmartFIFO<chunk_t*>(deduplicate_to_compress[queue_id], compress_input._n, compress_input._reconfigure, compress_input._change_step_after, compress_input._new_step));
//
//        FIFOData& compress_output = data._fifo_data[Layers::COMPRESS][Layers::REORDER][FIFORole::PRODUCER][i];
//        compress_args._output_fifos.push_back(new SmartFIFO<chunk_t*>(dedup_compress_to_reorder[queue_id], compress_output._n, compress_output._reconfigure, compress_output._change_step_after, compress_output._new_step));
//        compress_args._timestamp_data = timestamp_datas.data() + 1 + 2 * data._nb_threads + i;
//        pthread_create(threads.data() + 1 + 2 * data._nb_threads + i, nullptr, _CompressNaiveQueue, thread_args_naive_copy_because_pthread(compress_args));
//    }
//
//    thread_args_naive reorder_args;
//    for (int i = 0; i < nqueues; ++i) {
//        FIFOData& reorder_input = data._fifo_data[Layers::DEDUPLICATE][Layers::REORDER][FIFORole::CONSUMER][0];
//        reorder_args._input_fifos.push_back(new SmartFIFO<chunk_t*>(dedup_compress_to_reorder[i], reorder_input._n, reorder_input._reconfigure, reorder_input._change_step_after, reorder_input._new_step));
//    }
//
//    reorder_args.tid = 0;
//    reorder_args.nqueues = nqueues;
//    reorder_args._barrier = &barrier;
//    reorder_args._timestamp_data = timestamp_datas.data() + 1 + 3 * data._nb_threads;
//    pthread_create(threads.data() + 1 + 3 * data._nb_threads, nullptr, _ReorderNaiveQueue, thread_args_naive_copy_because_pthread(reorder_args));
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


