/*
 * Decoder for dedup files
 *
 * Copyright 2010 Princeton University.
 * All rights reserved.
 *
 * Originally written by Minlan Yu.
 * Largely rewritten by Christian Bienia.
 */

/*
 * The pipeline model for Encode is Fragment->FragmentRefine->Deduplicate->Compress->Reorder
 * Each stage has basically three steps:
 * 1. fetch a group of items from the queue
 * 2. process the items
 * 3. put them in the queue for the next stage
 */

#include <assert.h>
#include <stdbool.h>
#include <strings.h>
#include <math.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include <set>
#include <vector>

#include "util.h"
#include "dedupdef.h"
#include "encoder.h"
#include "debug.h"
#include "hashtable.h"
#include "config.h"
#include "rabin.h"
#include "mbuffer.h"
#include "step.h"
#include "script_mgr.h"

#ifdef ENABLE_PTHREADS
#include <pthread.h>
#include "queue.h"
#include "binheap.h"
#include "tree.h"
#include "fifo_plus.tpp"
#include "lua_core.h"
#endif //ENABLE_PTHREADS

#ifdef ENABLE_GZIP_COMPRESSION
#include <zlib.h>
#endif //ENABLE_GZIP_COMPRESSION

#ifdef ENABLE_BZIP2_COMPRESSION
#include <bzlib.h>
#endif //ENABLE_BZIP2_COMPRESSION

#ifdef ENABLE_PTHREADS
#include <pthread.h>
#endif //ENABLE_PTHREADS

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif //ENABLE_PARSEC_HOOKS


#define INITIAL_SEARCH_TREE_SIZE 4096


//The configuration block defined in main
//config_t * conf;

//Hash table data structure & utility functions
//struct hashtable *cache;

static DedupData* _g_data;

static unsigned int hash_from_key_fn( void *k ) {
  //NOTE: sha1 sum is integer-aligned
  return ((unsigned int *)k)[0];
}

static int keys_equal_fn ( void *key1, void *key2 ) {
  return (memcmp(key1, key2, SHA1_LEN) == 0);
}

/* static void log_enqueue(const char* src_phase, const char* dst_phase, int r, int tid, int qid, queue_t const* queue) {
  printf("[%s] Thread %d pushed %d chunks to %s queue %d (%p)\n", src_phase, tid, r, dst_phase, qid, &queue->buf);
}

static void log_dequeue(const char* src_phase, int r, int tid, int qid, queue_t const* queue) {
  printf("[%s] Thread %d poped %d chunks from queue %d (%p)\n", src_phase, tid, r, qid, &queue->buf);
} */

//Arguments to pass to each thread
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
  std::map<FIFORole, FIFOData>* _input_fifo_data;
  std::map<FIFORole, FIFOData>* _output_fifo_data;
  std::map<FIFORole, FIFOData>* _extra_output_fifo_data;
};


#ifdef ENABLE_STATISTICS
//Keep track of block granularity with 2^CHUNK_GRANULARITY_POW resolution (for statistics)
#define CHUNK_GRANULARITY_POW (7)
//Number of blocks to distinguish, CHUNK_MAX_NUM * 2^CHUNK_GRANULARITY_POW is biggest block being recognized (for statistics)
#define CHUNK_MAX_NUM (8*32)
//Map a chunk size to a statistics array slot
#define CHUNK_SIZE_TO_SLOT(s) ( ((s)>>(CHUNK_GRANULARITY_POW)) >= (CHUNK_MAX_NUM) ? (CHUNK_MAX_NUM)-1 : ((s)>>(CHUNK_GRANULARITY_POW)) )
//Get the average size of a chunk from a statistics array slot
#define SLOT_TO_CHUNK_SIZE(s) ( (s)*(1<<(CHUNK_GRANULARITY_POW)) + (1<<((CHUNK_GRANULARITY_POW)-1)) )
//Deduplication statistics (only used if ENABLE_STATISTICS is defined)
typedef struct {
  /* Cumulative sizes */
  size_t total_input; //Total size of input in bytes
  size_t total_dedup; //Total size of input without duplicate blocks (after global compression) in bytes
  size_t total_compressed; //Total size of input stream after local compression in bytes
  size_t total_output; //Total size of output in bytes (with overhead) in bytes

  /* Size distribution & other properties */
  unsigned int nChunks[CHUNK_MAX_NUM]; //Coarse-granular size distribution of data chunks
  unsigned int nDuplicates; //Total number of duplicate blocks
} stats_t;

//Initialize a statistics record
static void init_stats(stats_t *s) {
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

#ifdef ENABLE_PTHREADS
//The queues between the pipeline stages
queue_t *deduplicate_que, *refine_que, *reorder_que, *compress_que;
// pthread_key_t thread_data_key;

//Merge two statistics records: s1=s1+s2
static void merge_stats(stats_t *s1, stats_t *s2) {
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
#endif //ENABLE_PTHREADS

//Print statistics
static void print_stats(stats_t *s) {
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

  printf("Total input size:              %14.2f %s\n", (float)(s->total_input)/(float)(unit_div), unit_str[unit_idx]);
  printf("Total output size:             %14.2f %s\n", (float)(s->total_output)/(float)(unit_div), unit_str[unit_idx]);
  printf("Effective compression factor:  %14.2fx\n", (float)(s->total_input)/(float)(s->total_output));
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

  printf("Mean data chunk size:          %14.2f %s (stddev: %.2f %s)\n", mean_size / 1024.0, "KB", sqrtf(var_size) / 1024.0, "KB");
  printf("Amount of duplicate chunks:    %14.2f%%\n", 100.0*(float)(s->nDuplicates)/(float)(nTotalChunks));
  printf("Data size after deduplication: %14.2f %s (compression factor: %.2fx)\n", (float)(s->total_dedup)/(float)(unit_div), unit_str[unit_idx], (float)(s->total_input)/(float)(s->total_dedup));
  printf("Data size after compression:   %14.2f %s (compression factor: %.2fx)\n", (float)(s->total_compressed)/(float)(unit_div), unit_str[unit_idx], (float)(s->total_dedup)/(float)(s->total_compressed));
  printf("Output overhead:               %14.2f%%\n", 100.0*(float)(s->total_output-s->total_compressed)/(float)(s->total_output));
}

//variable with global statistics
stats_t stats;
#endif //ENABLE_STATISTICS


//Simple write utility function
static int write_file(int fd, u_char type, u_long len, u_char * content) {
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
static int create_output_file(const char *outfile) {
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
//      relevant data is available. If it is not then the function waits.
static void write_chunk_to_file(int fd, chunk_t *chunk) {
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
//      which means if it reaches the function it is guaranteed all data is ready.
static void write_chunk_to_file(int fd, chunk_t *chunk) {
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

int rf_win;
int rf_win_dataprocess;

static void configure_fifo(FIFOPlus<chunk_t*>& fifo, FIFOData const& data, FIFORole role) {
    fifo.set_role(role);
    fifo.set_multipliers(data._increase_mult, data._decrease_mult);
    fifo.set_n(data._n);
    fifo.set_thresholds(data._no_work_threshold,
                        data._with_work_threshold,
                        data._critical_threshold);
}

/*
 * Computational kernel of compression stage
 *
 * Actions performed:
 *  - Compress a data chunk
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
 * Pipeline stage function of compression stage
 *
 * Actions performed:
 *  - Dequeue items from compression queue
 *  - Execute compression kernel for each item
 *  - Enqueue each item into send queue
 */
#ifdef ENABLE_PTHREADS
void *Compress(void * targs) {
  struct thread_args *args = (struct thread_args *)targs;
  // const int qid = args->tid / MAX_THREADS_PER_QUEUE;
  FIFOPlus<chunk_t*>* input_fifo = args->_input_fifo;
  configure_fifo(*input_fifo, (*args->_input_fifo_data)[FIFORole::CONSUMER], FIFORole::CONSUMER);

  FIFOPlus<chunk_t*>* output_fifo = args->_output_fifo;
  configure_fifo(*output_fifo, (*args->_output_fifo_data)[FIFORole::PRODUCER], FIFORole::PRODUCER);
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
    input_fifo->pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER]._reconfigure);
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
    output_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER]._reconfigure);
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
 * Computational kernel of deduplication stage
 *
 * Actions performed:
 *  - Calculate SHA1 signature for each incoming data chunk
 *  - Perform database lookup to determine chunk redundancy status
 *  - On miss add chunk to database
 *  - Returns chunk redundancy status
 */
int sub_Deduplicate(chunk_t *chunk) {
  int isDuplicate;
  chunk_t *entry;

  assert(chunk!=NULL);
  assert(chunk->uncompressed_data.ptr!=NULL);

  SHA1_Digest(chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, (unsigned char *)(chunk->sha1));

  //Query database to determine whether we've seen the data chunk before
#ifdef ENABLE_PTHREADS
  pthread_mutex_t *ht_lock = hashtable_getlock(cache, (void *)(chunk->sha1));
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

  return isDuplicate;
}

/*
 * Pipeline stage function of deduplication stage
 *
 * Actions performed:
 *  - Take input data from fragmentation stages
 *  - Execute deduplication kernel for each data chunk
 *  - Route resulting package either to compression stage or to reorder stage, depending on deduplication status
 */
#ifdef ENABLE_PTHREADS
void * Deduplicate(void * targs) {
  struct thread_args *args = (struct thread_args *)targs;
  FIFOPlus<chunk_t*>* input_fifo = args->_input_fifo;
  configure_fifo(*input_fifo, (*args->_input_fifo_data)[FIFORole::CONSUMER], FIFORole::CONSUMER);

  FIFOPlus<chunk_t*>* compress_fifo = args->_output_fifo;
  configure_fifo(*compress_fifo, (*args->_output_fifo_data)[FIFORole::PRODUCER], FIFORole::PRODUCER);

  FIFOPlus<chunk_t*>* reorder_fifo = args->_extra_output_fifo;
  configure_fifo(*reorder_fifo, (*args->_extra_output_fifo_data)[FIFORole::PRODUCER], FIFORole::PRODUCER);

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
    input_fifo->pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER]._reconfigure);

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
      compress_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER]._reconfigure);
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
      reorder_fifo->push(chunk, (*args->_extra_output_fifo_data)[FIFORole::PRODUCER]._reconfigure);
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
 *  - Take coarse chunks from fragmentation stage
 *  - Partition data block into smaller chunks with Rabin rolling fingerprints
 *  - Send resulting data chunks to deduplication stage
 *
 * Notes:
 *  - Allocates mbuffers for fine-granular chunks
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
  configure_fifo(*input_fifo, (*args->_input_fifo_data)[FIFORole::CONSUMER], FIFORole::CONSUMER);

  FIFOPlus<chunk_t*>* output_fifo = args->_output_fifo;
  configure_fifo(*output_fifo, (*args->_output_fifo_data)[FIFORole::PRODUCER], FIFORole::PRODUCER);

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
    input_fifo->pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER]._reconfigure);

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
        output_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER]._reconfigure);
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
        output_fifo->push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER]._reconfigure);
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
 * Integrate all computationally intensive pipeline
 * stages to improve cache efficiency.
 */
void *SerialIntegratedPipeline(void * targs) {
  struct thread_args *args = (struct thread_args *)targs;
  size_t preloading_buffer_seek = 0;
  int fd = args->fd;
  int fd_out = create_output_file(_g_data->_output_filename->c_str());
  int r;

  chunk_t *temp = NULL;
  chunk_t *chunk = NULL;
  u32int * rabintab = (u32int*)malloc(256*sizeof rabintab[0]);
  u32int * rabinwintab = (u32int*)malloc(256*sizeof rabintab[0]);
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
    chunk->header.state = CHUNK_STATE_UNCOMPRESSED;
    if(bytes_left > 0) {
      //FIXME: Short-circuit this if no more data available

      //"Extension" of existing buffer, copy sequence number and left over data to beginning of new buffer
      //NOTE: We cannot safely extend the current memory region because it has already been given to another thread
      memcpy(chunk->uncompressed_data.ptr, temp->uncompressed_data.ptr, temp->uncompressed_data.n);
      mbuffer_free(&temp->uncompressed_data);
      free(temp);
      temp = NULL;
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

    //Check whether any new data was read in, process last chunk if not
    if(bytes_read == 0) {
#ifdef ENABLE_STATISTICS
      //update statistics
      stats.nChunks[CHUNK_SIZE_TO_SLOT(chunk->uncompressed_data.n)]++;
#endif //ENABLE_STATISTICS

      //Deduplicate
      int isDuplicate = sub_Deduplicate(chunk);
#ifdef ENABLE_STATISTICS
      if(isDuplicate) {
        stats.nDuplicates++;
      } else {
        stats.total_dedup += chunk->uncompressed_data.n;
      }
#endif //ENABLE_STATISTICS

      //If chunk is unique compress & archive it.
      if(!isDuplicate) {
        sub_Compress(chunk);
#ifdef ENABLE_STATISTICS
        stats.total_compressed += chunk->compressed_data.n;
#endif //ENABLE_STATISTICS
      }

      write_chunk_to_file(fd_out, chunk);
      if(chunk->header.isDuplicate) {
        free(chunk);
        chunk=NULL;
      }

      //stop fetching from input buffer, terminate processing
      break;
    }

    //partition input block into fine-granular chunks
    int split;
    do {
      split = 0;
      //Try to split the buffer
      int offset = rabinseg((uchar*)chunk->uncompressed_data.ptr, chunk->uncompressed_data.n, rf_win_dataprocess, rabintab, rabinwintab);
      //Did we find a split location?
      if(offset == 0) {
        //Split found at the very beginning of the buffer (should never happen due to technical limitations)
        assert(0);
        split = 0;
      } else if(offset < chunk->uncompressed_data.n) {
        //Split found somewhere in the middle of the buffer
        //Allocate a new chunk and create a new memory buffer
        temp = (chunk_t *)malloc(sizeof(chunk_t));
        if(temp==NULL) EXIT_TRACE("Memory allocation failed.\n");

        //split it into two pieces
        r = mbuffer_split(&chunk->uncompressed_data, &temp->uncompressed_data, offset);
        if(r!=0) EXIT_TRACE("Unable to split memory buffer.\n");
        temp->header.state = CHUNK_STATE_UNCOMPRESSED;

#ifdef ENABLE_STATISTICS
        //update statistics
        stats.nChunks[CHUNK_SIZE_TO_SLOT(chunk->uncompressed_data.n)]++;
#endif //ENABLE_STATISTICS

        //Deduplicate
        int isDuplicate = sub_Deduplicate(chunk);
#ifdef ENABLE_STATISTICS
        if(isDuplicate) {
          stats.nDuplicates++;
        } else {
          stats.total_dedup += chunk->uncompressed_data.n;
        }
#endif //ENABLE_STATISTICS

        //If chunk is unique compress & archive it.
        if(!isDuplicate) {
          sub_Compress(chunk);
#ifdef ENABLE_STATISTICS
          stats.total_compressed += chunk->compressed_data.n;
#endif //ENABLE_STATISTICS
        }

        write_chunk_to_file(fd_out, chunk);
        if(chunk->header.isDuplicate){
          free(chunk);
          chunk=NULL;
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
    } while(split);
  }

  free(rabintab);
  free(rabinwintab);

  close(fd_out);

  return NULL;
}

/*
 * Pipeline stage function of fragmentation stage
 *
 * Actions performed:
 *  - Read data from file (or preloading buffer)
 *  - Perform coarse-grained chunking
 *  - Send coarse chunks to refinement stages for further processing
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
    configure_fifo(output_fifo[i], (*args->_output_fifo_data)[FIFORole::PRODUCER], FIFORole::PRODUCER);
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
      output_fifo[qid].push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER]._reconfigure);
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
          output_fifo[qid].push(chunk, (*args->_output_fifo_data)[FIFORole::PRODUCER]._reconfigure);
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
        //      Only once the end of the file is reached do we get a genuine stub which will be enqueued right after the read operation.
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
 *  - Receive chunks from compression and deduplication stage
 *  - Check sequence number of each chunk to determine correct order
 *  - Cache chunks that arrive out-of-order until predecessors are available
 *  - Write chunks in-order to file (or preloading buffer)
 *
 * Notes:
 *  - This function blocks if the compression stage has not finished supplying
 *    the compressed data for a duplicate chunk.
 */
#ifdef ENABLE_PTHREADS
void *Reorder(void * targs) {

  struct thread_args *args = (struct thread_args *)targs;
  int qid = 0;
  FIFOPlus<chunk_t*>* input_fifo = args->_input_fifo;
  for (int i = 0; i < args->nqueues; ++i) {
    configure_fifo(input_fifo[i], (*args->_input_fifo_data)[FIFORole::CONSUMER], FIFORole::CONSUMER);
  }
  int fd = 0;
  int count = 0;

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

  fd = create_output_file(_g_data->_output_filename->c_str());

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
        input_fifo[qid].pop(chunk_opt, (*args->_input_fifo_data)[FIFORole::CONSUMER]._reconfigure);
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

/* static void debug_queue(queue_t* queue, ringbuffer_t* buffer, int limit) {
    thread_data_t* data = (thread_data_t*)pthread_getspecific(thread_data_key);
    printf("[%s %d] Thread %llu requesting %d elements in queue %p containing %d elements\n", data->fname, data->thread_id, pthread_self(), limit, queue, queue_size(queue));
}

static void debug_push_queue(queue_t* queue, ringbuffer_t* buffer, int limit) {
    thread_data_t* data = (thread_data_t*)pthread_getspecific(thread_data_key);
    printf("[%s %d] Thread %llu adding %d element to queue %p containing %d elements\n", data->fname, data->thread_id, pthread_self(), limit, queue, queue_size(queue));
} */

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

unsigned long long Encode(DedupData& data) {
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

  int threads_per_queue;
  for(i=0; i<nqueues; i++) {
    if (i < nqueues -1 || data._nb_threads %MAX_THREADS_PER_QUEUE == 0) {
      //all but last queue
      threads_per_queue = MAX_THREADS_PER_QUEUE;
    } else {
      //remaining threads work on last queue
      threads_per_queue = data._nb_threads %MAX_THREADS_PER_QUEUE;
    }
  }
#else
  struct thread_args generic_args;
#endif //ENABLE_PTHREADS

  int init_res = mbuffer_system_init();
  assert(!init_res);

  /* src file stat */
  if (stat(data._input_filename->c_str(), &filestat) < 0)
      EXIT_TRACE("stat() %s failed: %s\n", data._input_filename->c_str(), strerror(errno));

  if (!S_ISREG(filestat.st_mode))
    EXIT_TRACE("not a normal file: %s\n", data._input_filename->c_str());
#ifdef ENABLE_STATISTICS
  stats.total_input = filestat.st_size;
#endif //ENABLE_STATISTICS

  /* src file open */
  if((fd = open(data._input_filename->c_str(), O_RDONLY | O_LARGEFILE)) < 0)
    EXIT_TRACE("%s file open error %s\n", data._input_filename->c_str(), strerror(errno));

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

#ifdef ENABLE_PTHREADS
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

  alignas(FIFOPlus<chunk_t*>) unsigned char refine_input[nqueues * sizeof(FIFOPlus<chunk_t*>)];
  alignas(FIFOPlus<chunk_t*>) unsigned char deduplicate_input[nqueues * sizeof(FIFOPlus<chunk_t*>)];
  alignas(FIFOPlus<chunk_t*>) unsigned char compress_input[nqueues * sizeof(FIFOPlus<chunk_t*>)];
  alignas(FIFOPlus<chunk_t*>) unsigned char reorder_input[nqueues * sizeof(FIFOPlus<chunk_t*>)];
  bool extra_queue = (data._nb_threads % MAX_THREADS_PER_QUEUE) != 0;

  std::map<void*, PThreadThreadIdentifier*> identifiers;

  pthread_barrier_t barrier;
  pthread_barrier_init(&barrier, nullptr, 
    1 /* this_thread */  + 
    1 /* Fragment */     +
    3 * data._nb_threads /* Refine + Deduplicate + Compress */ +
    1 /* Reorder */
  );

  auto allocate = [&](void* ptr, unsigned int nb_producers, unsigned int nb_consumers, unsigned int history_size) -> void {
    PThreadThreadIdentifier* identifier = new PThreadThreadIdentifier;
    identifiers[ptr] = identifier;
    new(ptr) FIFOPlus<chunk_t*>(FIFOPlusPopPolicy::POP_WAIT, identifier, nb_producers, nb_consumers, history_size);
  };

  for (int i = 0; i < nqueues; ++i) {
    if (i == nqueues - 1 && extra_queue) {
      allocate((refine_input + i * sizeof(FIFOPlus<chunk_t*>)), 1, data._nb_threads % MAX_THREADS_PER_QUEUE, (*data._fifo_data)[FRAGMENT][REFINE][FIFORole::PRODUCER]._history_size);
      allocate((deduplicate_input + i * sizeof(FIFOPlus<chunk_t*>)), data._nb_threads % MAX_THREADS_PER_QUEUE, data._nb_threads % MAX_THREADS_PER_QUEUE, (*data._fifo_data)[REFINE][DEDUPLICATE][FIFORole::PRODUCER]._history_size);
      allocate((compress_input + i * sizeof(FIFOPlus<chunk_t*>)), data._nb_threads % MAX_THREADS_PER_QUEUE, data._nb_threads % MAX_THREADS_PER_QUEUE, (*data._fifo_data)[DEDUPLICATE][COMPRESS][FIFORole::PRODUCER]._history_size);
      allocate((reorder_input + i * sizeof(FIFOPlus<chunk_t*>)), 2 * (data._nb_threads % MAX_THREADS_PER_QUEUE), 1, (*data._fifo_data)[DEDUPLICATE][REORDER][FIFORole::PRODUCER]._history_size);
    } else {
      allocate((refine_input + i * sizeof(FIFOPlus<chunk_t*>)), 1, MAX_THREADS_PER_QUEUE, (*data._fifo_data)[FRAGMENT][REFINE][FIFORole::PRODUCER]._history_size);
      allocate((deduplicate_input + i * sizeof(FIFOPlus<chunk_t*>)), MAX_THREADS_PER_QUEUE, MAX_THREADS_PER_QUEUE, (*data._fifo_data)[REFINE][DEDUPLICATE][FIFORole::PRODUCER]._history_size);
      allocate((compress_input + i * sizeof(FIFOPlus<chunk_t*>)), MAX_THREADS_PER_QUEUE, MAX_THREADS_PER_QUEUE, (*data._fifo_data)[DEDUPLICATE][COMPRESS][FIFORole::PRODUCER]._history_size);
      allocate((reorder_input + i * sizeof(FIFOPlus<chunk_t*>)), 2 * MAX_THREADS_PER_QUEUE, 1, (*data._fifo_data)[DEDUPLICATE][REORDER][FIFORole::PRODUCER]._history_size);
    }
  }

  FIFOPlus<chunk_t*>* refine = reinterpret_cast<FIFOPlus<chunk_t*>*>(refine_input);
  FIFOPlus<chunk_t*>* deduplicate = reinterpret_cast<FIFOPlus<chunk_t*>*>(deduplicate_input);
  FIFOPlus<chunk_t*>* compress = reinterpret_cast<FIFOPlus<chunk_t*>*>(compress_input);
  FIFOPlus<chunk_t*>* reorder = reinterpret_cast<FIFOPlus<chunk_t*>*>(reorder_input);
  data_process_args._output_fifo = refine;
  data_process_args._output_fifo_data = &((*data._fifo_data)[FRAGMENT][REFINE]);

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
    anchor_thread_args[i]._input_fifo_data = &((*data._fifo_data)[FRAGMENT][REFINE]);
    anchor_thread_args[i]._output_fifo_data = &((*data._fifo_data)[REFINE][DEDUPLICATE]);
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
    chunk_thread_args[i]._input_fifo_data = &((*data._fifo_data)[REFINE][DEDUPLICATE]);
    chunk_thread_args[i]._output_fifo_data = &((*data._fifo_data)[DEDUPLICATE][COMPRESS]);
    chunk_thread_args[i]._extra_output_fifo_data = &((*data._fifo_data)[DEDUPLICATE][REORDER]);
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
    compress_thread_args[i]._input_fifo_data = &((*data._fifo_data)[DEDUPLICATE][COMPRESS]);
    compress_thread_args[i]._output_fifo_data = &((*data._fifo_data)[DEDUPLICATE][REORDER]);
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
  send_block_args._input_fifo_data = &((*data._fifo_data)[DEDUPLICATE][REORDER]);
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

  for (int i = 0; i < nqueues; ++i) {
      refine[i].~FIFOPlus<chunk_t*>();
      deduplicate[i].~FIFOPlus<chunk_t*>();
      compress[i].~FIFOPlus<chunk_t*>();
      reorder[i].~FIFOPlus<chunk_t*>();
  }

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

#else //serial version

  generic_args.tid = 0;
  generic_args.nqueues = -1;
  generic_args.fd = fd;

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_begin();
#endif

  //Do the processing
  SerialIntegratedPipeline(&generic_args);

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_end();
#endif

#endif //ENABLE_PTHREADS

  //clean up after preloading
  if(data._preloading) {
    free(preloading_buffer);
  }

  /* clean up with the src file */
  if (!data._input_filename->empty())
    close(fd);

  int des_res = mbuffer_system_destroy();
  assert(!des_res);

  hashtable_destroy(cache, TRUE);

#ifdef ENABLE_STATISTICS
  /* dest file stat */
  if (stat(data._output_filename->c_str(), &filestat) < 0)
      EXIT_TRACE("stat() %s failed: %s\n", data._output_filename->c_str(), strerror(errno));
  stats.total_output = filestat.st_size;

  //Analyze and print statistics
  // if(conf->verbose) print_stats(&stats);
#endif //ENABLE_STATISTICS

  return diff;
}

/*--------------------------------------------------------------------------*/
/* Encode
 * Compress an input stream
 *
 * Arguments:
 *   conf:    Configuration parameters
 *
 */
/* void Encode(config_t * _conf) {
  // sScriptMgr->add_push_callback(debug_queue);
  // sScriptMgr->add_pop_callback(debug_push_queue);

  struct stat filestat;
  int32 fd;

  conf = _conf;

#ifdef ENABLE_STATISTICS
  init_stats(&stats);
  // pthread_key_create(&thread_data_key, NULL);
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
  const int nqueues = (conf->nthreads / MAX_THREADS_PER_QUEUE) +
                      ((conf->nthreads % MAX_THREADS_PER_QUEUE != 0) ? 1 : 0);
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
    if (i < nqueues -1 || conf->nthreads %MAX_THREADS_PER_QUEUE == 0) {
      //all but last queue
      threads_per_queue = MAX_THREADS_PER_QUEUE;
    } else {
      //remaining threads work on last queue
      threads_per_queue = conf->nthreads %MAX_THREADS_PER_QUEUE;
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

  int init_res = mbuffer_system_init();
  assert(!init_res);

  // src file stat 
  if (stat(conf->infile, &filestat) < 0) 
      EXIT_TRACE("stat() %s failed: %s\n", conf->infile, strerror(errno));

  if (!S_ISREG(filestat.st_mode)) 
    EXIT_TRACE("not a normal file: %s\n", conf->infile);
#ifdef ENABLE_STATISTICS
  stats.total_input = filestat.st_size;
#endif //ENABLE_STATISTICS

  // src file open
  if((fd = open(conf->infile, O_RDONLY | O_LARGEFILE)) < 0) 
    EXIT_TRACE("%s file open error %s\n", conf->infile, strerror(errno));

  //Load entire file into memory if requested by user
  void *preloading_buffer = NULL;
  if(conf->preloading) {
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

#ifdef ENABLE_PTHREADS
  // Variables for 3 thread pools and 2 pipeline stage threads.
  // The first and the last stage are serial (mostly I/O).
  //
  pthread_t threads_anchor[MAX_THREADS],
    threads_chunk[MAX_THREADS],
    threads_compress[MAX_THREADS],
    threads_send, threads_process;

  data_process_args.tid = 0;
  data_process_args.nqueues = nqueues;
  data_process_args.fd = fd;

#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_begin();
#endif

  struct timespec begin, end;
  clock_gettime(CLOCK_MONOTONIC, &begin);
  //thread for first pipeline stage (input)
  pthread_create(&threads_process, NULL, Fragment, &data_process_args);

  //Create 3 thread pools for the intermediate pipeline stages
  struct thread_args anchor_thread_args[conf->nthreads];
  for (i = 0; i < conf->nthreads; i ++) {
     anchor_thread_args[i].tid = i;
     pthread_create(&threads_anchor[i], NULL, FragmentRefine, &anchor_thread_args[i]);
  }

  struct thread_args chunk_thread_args[conf->nthreads];
  for (i = 0; i < conf->nthreads; i ++) {
    chunk_thread_args[i].tid = i;
    pthread_create(&threads_chunk[i], NULL, Deduplicate, &chunk_thread_args[i]);
  }

  struct thread_args compress_thread_args[conf->nthreads];
  for (i = 0; i < conf->nthreads; i ++) {
    compress_thread_args[i].tid = i;
    pthread_create(&threads_compress[i], NULL, Compress, &compress_thread_args[i]);
  }

  //thread for last pipeline stage (output)
  struct thread_args send_block_args;
  send_block_args.tid = 0;
  send_block_args.nqueues = nqueues;
  pthread_create(&threads_send, NULL, Reorder, &send_block_args);

  /// parallel phase

  //Return values of threads
  stats_t *threads_anchor_rv[conf->nthreads];
  stats_t *threads_chunk_rv[conf->nthreads];
  stats_t *threads_compress_rv[conf->nthreads];

  //join all threads 
  pthread_join(threads_process, NULL);
  for (i = 0; i < conf->nthreads; i ++)
    pthread_join(threads_anchor[i], (void **)&threads_anchor_rv[i]);
  for (i = 0; i < conf->nthreads; i ++)
    pthread_join(threads_chunk[i], (void **)&threads_chunk_rv[i]);
  for (i = 0; i < conf->nthreads; i ++)
    pthread_join(threads_compress[i], (void **)&threads_compress_rv[i]);
  pthread_join(threads_send, NULL);

#define BILLION 1000000000

  clock_gettime(CLOCK_MONOTONIC, &end);
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
  }
  
#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_end();
#endif

  // free queues 
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
  for(i=0; i<conf->nthreads; i++) {
    merge_stats(&stats, threads_anchor_rv[i]);
    free(threads_anchor_rv[i]);
  }
  for(i=0; i<conf->nthreads; i++) {
    merge_stats(&stats, threads_chunk_rv[i]);
    free(threads_chunk_rv[i]);
  }
  for(i=0; i<conf->nthreads; i++) {
    merge_stats(&stats, threads_compress_rv[i]);
    free(threads_compress_rv[i]);
  }
#endif //ENABLE_STATISTICS

#else //serial version

  generic_args.tid = 0;
  generic_args.nqueues = -1;
  generic_args.fd = fd;

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_begin();
#endif

  //Do the processing
  SerialIntegratedPipeline(&generic_args);

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_end();
#endif

#endif //ENABLE_PTHREADS

  //clean up after preloading
  if(conf->preloading) {
    free(preloading_buffer);
  }

  // clean up with the src file
  if (conf->infile != NULL)
    close(fd);

  int des_res = mbuffer_system_destroy();
  assert(!des_res);

  hashtable_destroy(cache, TRUE);

#ifdef ENABLE_STATISTICS
  // dest file stat 
  if (stat(conf->outfile, &filestat) < 0) 
      EXIT_TRACE("stat() %s failed: %s\n", conf->outfile, strerror(errno));
  stats.total_output = filestat.st_size;

  //Analyze and print statistics
  if(conf->verbose) print_stats(&stats);
#endif //ENABLE_STATISTICS
} */

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
        //      Only once the end of the file is reached do we get a genuine stub which will be enqueued right after the read operation.
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

  fd = create_output_file(_g_data->_output_filename->c_str());

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
  if (stat(data._input_filename->c_str(), &filestat) < 0) 
      EXIT_TRACE("stat() %s failed: %s\n", data._input_filename->c_str(), strerror(errno));

  if (!S_ISREG(filestat.st_mode)) 
    EXIT_TRACE("not a normal file: %s\n", data._input_filename->c_str());
#ifdef ENABLE_STATISTICS
  stats.total_input = filestat.st_size;
#endif //ENABLE_STATISTICS

  /* src file open */
  if((fd = open(data._input_filename->c_str(), O_RDONLY | O_LARGEFILE)) < 0) 
    EXIT_TRACE("%s file open error %s\n", data._input_filename->c_str(), strerror(errno));

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

#ifdef ENABLE_PTHREADS
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

#else //serial version

  generic_args.tid = 0;
  generic_args.nqueues = -1;
  generic_args.fd = fd;

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_begin();
#endif

  //Do the processing
  SerialIntegratedPipeline(&generic_args);

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_roi_end();
#endif

#endif //ENABLE_PTHREADS

  //clean up after preloading
  if(data._preloading) {
    free(preloading_buffer);
  }

  /* clean up with the src file */
  if (data._input_filename->c_str() != NULL)
    close(fd);

  res = mbuffer_system_destroy();
  assert(res == 0);

  hashtable_destroy(cache, TRUE);

#ifdef ENABLE_STATISTICS
  /* dest file stat */
  if (stat(data._output_filename->c_str(), &filestat) < 0) 
      EXIT_TRACE("stat() %s failed: %s\n", data._output_filename->c_str(), strerror(errno));
  stats.total_output = filestat.st_size;

  //Analyze and print statistics
  // if(conf->verbose) print_stats(&stats);
#endif //ENABLE_STATISTICS

  return (end.tv_sec * BILLION + end.tv_nsec) - (begin.tv_sec * BILLION + begin.tv_nsec);
}

