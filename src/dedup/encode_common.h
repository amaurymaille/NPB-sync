#ifndef ENCODE_COMMON_H
#define ENCODE_COMMON_H

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

#include <memory>
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

unsigned int hash_from_key_fn(void* k);
int keys_equal_fn(void* key1, void* key2);

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

void init_stats(stats_t* s);
void merge_stats(stats_t* s1, stats_t* s2);
void print_stats(stats_t* s);

extern stats_t stats;

#endif // ENABLE_STATISTICS


int write_file(int fd, u_char type, u_long len, u_char * content);
int create_output_file(const char *outfile); 
void write_chunk_to_file(int fd, chunk_t *chunk);

void sub_Compress(chunk_t *chunk);
int sub_Deduplicate(chunk_t *chunk);

using sc = std::chrono::steady_clock
using tp = std::chrono::time_point<sc>;

unsigned long long EncodeBase(DedupData& data, std::function<void(DedupData&, size_t, void*, tp&, tp1)&& fn);

extern int rf_win;
extern int rf_win_dataprocess;

#define INITIAL_SEARCH_TREE_SIZE 4096

#endif /* ENCODE_COMMON_H */
