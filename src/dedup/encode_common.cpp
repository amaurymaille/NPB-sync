#include "encode_common.h"

DedupData* _g_data;

int rf_win;
int rf_win_dataprocess;

unsigned int hash_from_key_fn( void *k ) {
    //NOTE: sha1 sum is integer-aligned
    return ((unsigned int *)k)[0];
}

int keys_equal_fn ( void *key1, void *key2 ) {
    return (memcmp(key1, key2, SHA1_LEN) == 0);
}

#ifdef ENABLE_STATISTICS

//Initialize a statistics record
void init_stats(stats_t *s) {
    int i;

    assert(s!=NULL);
    s->total_input = 0;
    s->total_dedup = 0;
    s->total_compressed = 0;
    s->total_output = 0;

    for(i=0; i<CHUNK_MAX_NUM; i++) {
        s->nChunks[i] = 0;
    }
    s->nDuplicates = 0;
}

//Merge two statistics records: s1=s1+s2
void merge_stats(stats_t *s1, stats_t *s2) {
    int i;

    assert(s1!=NULL);
    assert(s2!=NULL);
    s1->total_input += s2->total_input;
    s1->total_dedup += s2->total_dedup;
    s1->total_compressed += s2->total_compressed;
    s1->total_output += s2->total_output;

    for(i=0; i<CHUNK_MAX_NUM; i++) {
        s1->nChunks[i] += s2->nChunks[i];
    }
    s1->nDuplicates += s2->nDuplicates;
}

//Print statistics
void print_stats(stats_t *s) {
    const unsigned int unit_str_size = 7; //elements in unit_str array
    const char *unit_str[] = {"Bytes", "KB", "MB", "GB", "TB", "PB", "EB"};
    unsigned int unit_idx = 0;
    size_t unit_div = 1;

    assert(s!=NULL);

    //determine most suitable unit to use
    for(unit_idx=0; unit_idx<unit_str_size; unit_idx++) {
        unsigned int unit_div_next = unit_div * 1024;

        if(s->total_input / unit_div_next <= 0) break;
        if(s->total_dedup / unit_div_next <= 0) break;
        if(s->total_compressed / unit_div_next <= 0) break;
        if(s->total_output / unit_div_next <= 0) break;

        unit_div = unit_div_next;
    }

    printf("Total input size:                %14.2f %s\n", (float)(s->total_input)/(float)(unit_div), unit_str[unit_idx]);
    printf("Total output size:             %14.2f %s\n", (float)(s->total_output)/(float)(unit_div), unit_str[unit_idx]);
    printf("Effective compression factor:    %14.2fx\n", (float)(s->total_input)/(float)(s->total_output));
    printf("\n");

    //Total number of chunks
    unsigned int i;
    unsigned int nTotalChunks=0;
    for(i=0; i<CHUNK_MAX_NUM; i++) nTotalChunks+= s->nChunks[i];

    //Average size of chunks
    float mean_size = 0.0;
    for(i=0; i<CHUNK_MAX_NUM; i++) mean_size += (float)(SLOT_TO_CHUNK_SIZE(i)) * (float)(s->nChunks[i]);
    mean_size = mean_size / (float)nTotalChunks;

    //Variance of chunk size
    float var_size = 0.0;
    for(i=0; i<CHUNK_MAX_NUM; i++) var_size += (mean_size - (float)(SLOT_TO_CHUNK_SIZE(i))) *
        (mean_size - (float)(SLOT_TO_CHUNK_SIZE(i))) *
            (float)(s->nChunks[i]);

    printf("Mean data chunk size:            %14.2f %s (stddev: %.2f %s)\n", mean_size / 1024.0, "KB", sqrtf(var_size) / 1024.0, "KB");
    printf("Amount of duplicate chunks:    %14.2f%%\n", 100.0*(float)(s->nDuplicates)/(float)(nTotalChunks));
    printf("Data size after deduplication: %14.2f %s (compression factor: %.2fx)\n", (float)(s->total_dedup)/(float)(unit_div), unit_str[unit_idx], (float)(s->total_input)/(float)(s->total_dedup));
    printf("Data size after compression:     %14.2f %s (compression factor: %.2fx)\n", (float)(s->total_compressed)/(float)(unit_div), unit_str[unit_idx], (float)(s->total_dedup)/(float)(s->total_compressed));
    printf("Output overhead:                 %14.2f%%\n", 100.0*(float)(s->total_output-s->total_compressed)/(float)(s->total_output));
}

//variable with global statistics
stats_t stats;

#endif

//Simple write utility function
int write_file(int fd, u_char type, u_long len, u_char * content) {
    if (xwrite(fd, &type, sizeof(type)) < 0){
        perror("xwrite:");
        EXIT_TRACE("xwrite type fails\n");
        return -1;
    }
    if (xwrite(fd, &len, sizeof(len)) < 0){
        EXIT_TRACE("xwrite content fails\n");
    }
    if (xwrite(fd, content, len) < 0){
        EXIT_TRACE("xwrite content fails\n");
    }
    return 0;
}

/*
 * Helper function that creates and initializes the output file
 * Takes the file name to use as input and returns the file handle
 * The output file can be used to write chunks without any further steps
 */
int create_output_file(const char *outfile) {
    int fd;

    //Create output file
    fd = open(outfile, O_CREAT|O_TRUNC|O_WRONLY|O_TRUNC, S_IRGRP | S_IWUSR | S_IRUSR | S_IROTH);
    if (fd < 0) {
        EXIT_TRACE("Cannot open output file.");
    }

    //Write header
    if (write_header(fd, _g_data->_compression)) {
        EXIT_TRACE("Cannot write output file header.\n");
    }

    return fd;
}

/*
 * Helper function that writes a chunk to an output file depending on
 * its state. The function will write the SHA1 sum if the chunk has
 * already been written before, or it will write the compressed data
 * of the chunk if it has not been written yet.
 *
 * This function will block if the compressed data is not available yet.
 * This function might update the state of the chunk if there are any changes.
 */
#ifdef ENABLE_PTHREADS
//NOTE: The parallel version checks the state of each chunk to make sure the
//        relevant data is available. If it is not then the function waits.
void write_chunk_to_file(int fd, chunk_t *chunk) {
    assert(chunk!=NULL);

    //Find original chunk
    if(chunk->header.isDuplicate) chunk = chunk->compressed_data_ref;

    pthread_mutex_lock(&chunk->header.lock);
    while(chunk->header.state == CHUNK_STATE_UNCOMPRESSED) {
        pthread_cond_wait(&chunk->header.update, &chunk->header.lock);
    }

    //state is now guaranteed to be either COMPRESSED or FLUSHED
    if(chunk->header.state == CHUNK_STATE_COMPRESSED) {
        //Chunk data has not been written yet, do so now
        write_file(fd, TYPE_COMPRESS, chunk->compressed_data.n, (u_char*)chunk->compressed_data.ptr);
        mbuffer_free(&chunk->compressed_data);
        chunk->header.state = CHUNK_STATE_FLUSHED;
    } else {
        //Chunk data has been written to file before, just write SHA1
        write_file(fd, TYPE_FINGERPRINT, SHA1_LEN, (unsigned char *)(chunk->sha1));
    }
    pthread_mutex_unlock(&chunk->header.lock);
}
#else
//NOTE: The serial version relies on the fact that chunks are processed in-order,
//        which means if it reaches the function it is guaranteed all data is ready.
void write_chunk_to_file(int fd, chunk_t *chunk) {
    assert(chunk!=NULL);

    if(!chunk->header.isDuplicate) {
        //Unique chunk, data has not been written yet, do so now
        write_file(fd, TYPE_COMPRESS, chunk->compressed_data.n, chunk->compressed_data.ptr);
        mbuffer_free(&chunk->compressed_data);
    } else {
        //Duplicate chunk, data has been written to file before, just write SHA1
        write_file(fd, TYPE_FINGERPRINT, SHA1_LEN, (unsigned char *)(chunk->sha1));
    }
}
#endif //ENABLE_PTHREADS

/*
 * Computational kernel of compression stage
 *
 * Actions performed:
 *    - Compress a data chunk
 */
void sub_Compress(chunk_t *chunk) {
    size_t n;
    int r;

    assert(chunk!=NULL);
    //compress the item and add it to the database
#ifdef ENABLE_PTHREADS
    pthread_mutex_lock(&chunk->header.lock);
    assert(chunk->header.state == CHUNK_STATE_UNCOMPRESSED);
#endif //ENABLE_PTHREADS
    switch (_g_data->_compression) {
        case COMPRESS_NONE:
            //Simply duplicate the data
            n = chunk->uncompressed_data.n;
            r = mbuffer_create(&chunk->compressed_data, n);
            if(r != 0) {
                EXIT_TRACE("Creation of compression buffer failed.\n");
            }
            //copy the block
            memcpy(chunk->compressed_data.ptr, chunk->uncompressed_data.ptr, chunk->uncompressed_data.n);
            break;
#ifdef ENABLE_GZIP_COMPRESSION
        case COMPRESS_GZIP:
            //Gzip compression buffer must be at least 0.1% larger than source buffer plus 12 bytes
            n = chunk->uncompressed_data.n + (chunk->uncompressed_data.n >> 9) + 12;
            r = mbuffer_create(&chunk->compressed_data, n);
            if(r != 0) {
                EXIT_TRACE("Creation of compression buffer failed.\n");
            }
            //compress the block
            r = compress((Bytef*)chunk->compressed_data.ptr, &n, (const Bytef*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n);
            if (r != Z_OK) {
                EXIT_TRACE("Compression failed\n");
            }
            //Shrink buffer to actual size
            if(n < chunk->compressed_data.n) {
                r = mbuffer_realloc(&chunk->compressed_data, n);
                assert(r == 0);
            }
            break;
#endif //ENABLE_GZIP_COMPRESSION
#ifdef ENABLE_BZIP2_COMPRESSION
        case COMPRESS_BZIP2:
            //Bzip compression buffer must be at least 1% larger than source buffer plus 600 bytes
            n = chunk->uncompressed_data.n + (chunk->uncompressed_data.n >> 6) + 600;
            r = mbuffer_create(&chunk->compressed_data, n);
            if(r != 0) {
                EXIT_TRACE("Creation of compression buffer failed.\n");
            }
            //compress the block
            {
                unsigned int int_n = n;
                r = BZ2_bzBuffToBuffCompress((char*)chunk->compressed_data.ptr, &int_n, (char*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, 9, 0, 30);
                n = int_n;
            }
            if (r != BZ_OK) {
                EXIT_TRACE("Compression failed\n");
            }
            //Shrink buffer to actual size
            if(n < chunk->compressed_data.n) {
                r = mbuffer_realloc(&chunk->compressed_data, n);
                assert(r == 0);
            }
            break;
#endif //ENABLE_BZIP2_COMPRESSION
        default:
            EXIT_TRACE("Compression type not implemented.\n");
            break;
    }
    mbuffer_free(&chunk->uncompressed_data);

#ifdef ENABLE_PTHREADS
    chunk->header.state = CHUNK_STATE_COMPRESSED;
    pthread_cond_broadcast(&chunk->header.update);
    pthread_mutex_unlock(&chunk->header.lock);
#endif //ENABLE_PTHREADS

    return;
}

/*
 * Computational kernel of deduplication stage
 *
 * Actions performed:
 *    - Calculate SHA1 signature for each incoming data chunk
 *    - Perform database lookup to determine chunk redundancy status
 *    - On miss add chunk to database
 *    - Returns chunk redundancy status
 */
std::tuple<int, unsigned int> sub_Deduplicate(chunk_t *chunk) {
    int isDuplicate;
    chunk_t *entry;

    assert(chunk!=NULL);
    assert(chunk->uncompressed_data.ptr!=NULL);

    SHA1_Digest(chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, (unsigned char *)(chunk->sha1));

    //Query database to determine whether we've seen the data chunk before
#ifdef ENABLE_PTHREADS
    auto [ht_lock, idx] = hashtable_getlock(cache, (void *)(chunk->sha1));
    pthread_mutex_lock(ht_lock);
#endif
    entry = (chunk_t *)hashtable_search(cache, (void *)(chunk->sha1));
    isDuplicate = (entry != NULL);
    chunk->header.isDuplicate = isDuplicate;
    if (!isDuplicate) {
        // Cache miss: Create entry in hash table and forward data to compression stage
#ifdef ENABLE_PTHREADS
        pthread_mutex_init(&chunk->header.lock, NULL);
        pthread_cond_init(&chunk->header.update, NULL);
#endif
        //NOTE: chunk->compressed_data.buffer will be computed in compression stage
        if (hashtable_insert(cache, (void *)(chunk->sha1), (void *)chunk) == 0) {
            EXIT_TRACE("hashtable_insert failed");
        }
    } else {
        // Cache hit: Skipping compression stage
        chunk->compressed_data_ref = entry;
        mbuffer_free(&chunk->uncompressed_data);
    }
#ifdef ENABLE_PTHREADS
    pthread_mutex_unlock(ht_lock);
#endif

    return { isDuplicate, idx };
}


unsigned long long EncodeBase(DedupData& data, std::function<void(DedupData&, int, size_t, void*, tp&, tp&)>&& fn) {
    _g_data = &data;

    struct stat filestat;
    int32 fd;

    //Create chunk cache
    cache = hashtable_create(65536, hash_from_key_fn, keys_equal_fn, FALSE);
    if(cache == NULL) {
        printf("ERROR: Out of memory\n");
        exit(1);
    }
    
    int init_res = mbuffer_system_init();
    assert(!init_res);

    /* src file stat */
    if (stat(data._input_filename.c_str(), &filestat) < 0)
        EXIT_TRACE("stat() %s failed: %s\n", data._input_filename.c_str(), strerror(errno));

    if (!S_ISREG(filestat.st_mode))
        EXIT_TRACE("not a normal file: %s\n", data._input_filename.c_str());

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
    }

    /// Algorithm specific part
    tp begin, end;
    fn(data, fd, filestat.st_size, preloading_buffer, begin, end);
    unsigned long long diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

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

    return diff;
}

void compute_fifo_ids_for_layer(std::set<int>& fifo_ids, LayerData const& data) {
    for (ThreadData const& thread_data: data._thread_data) {
        for (auto const& [fifo_id, fifo_data]: thread_data._outputs) {
            fifo_ids.insert(fifo_id);
        }
    }
}

void compute_fifo_ids_for_reorder(std::set<int>& fifo_ids, LayerData const& deduplicate, LayerData const& compress) {
    for (ThreadData const& thread_data: deduplicate._thread_data) {
        for (auto const& [fifo_id, fifo_data]: thread_data._extras) {
            fifo_ids.insert(fifo_id);
        }
    }

    for (ThreadData const& thread_data: compress._thread_data) {
        for (auto const& [fifo_id, fifo_data]: thread_data._outputs) {
            fifo_ids.insert(fifo_id);
        }
    }
}

unsigned int nb_producers_for_fifo(int fifo_id, LayerData const& layer_data) {
    unsigned int nb_producers = 0;
    for (ThreadData const& thread_data: layer_data._thread_data) {
        if (thread_data._outputs.find(fifo_id) != thread_data._outputs.end()) {
            nb_producers++;
        }
    }

    return nb_producers;

}

unsigned int nb_producers_for_reorder_fifo(int fifo_id, LayerData const& deduplicate, LayerData const& compress) {
    unsigned int nb_producers = 0;

    for (ThreadData const& thread_data: deduplicate._thread_data) {
        if (thread_data._extras.find(fifo_id) != thread_data._extras.end()) {
            nb_producers++;
        }
    }

    return nb_producers + nb_producers_for_fifo(fifo_id, compress);
}
