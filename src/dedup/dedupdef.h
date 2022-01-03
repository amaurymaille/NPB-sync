#ifndef _DEDUPDEF_H_
#define _DEDUPDEF_H_

#include <sys/types.h>
#include <stdint.h>
#include <assert.h>

#include <cstring>

#include <chrono>
#include <vector>

#include <map>
#include <mutex>

// #include "fifo_plus.h"
#include "config.h"
#include "mbuffer.h"
#include "sha.h"
// #include "smart_fifo.h"

using std::chrono::steady_clock;

#define CHECKBIT 123456

#define MAX_THREADS 1024


/*-----------------------------------------------------------------------*/
/* type definition */
/*-----------------------------------------------------------------------*/

typedef uint8_t  u_char;
typedef uint64_t u_long;
typedef uint64_t ulong;
typedef uint32_t u_int;

typedef uint8_t  byte;
typedef byte     u_int8;
typedef uint16_t u_int16;
typedef uint32_t u_int32;
typedef uint64_t u_int64;

typedef uint64_t u64int;
typedef uint32_t u32int;
typedef uint8_t  uchar;
typedef uint16_t u16int;

typedef int8_t   int8;
typedef int16_t  int16;
typedef int32_t  int32;
typedef int64_t  int64;

/*-----------------------------------------------------------------------*/
/* useful macros */
/*-----------------------------------------------------------------------*/
#ifndef NELEM
#define NELEM(x) (sizeof(x)/sizeof(x[0]))
#endif

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef O_LARGEFILE
#define O_LARGEFILE  0100000
#endif

#define EXT       ".ddp"           /* extension */ 
#define EXT_LEN   (sizeof(EXT)-1)  /* extention length */

/*----------------------------------------------------------------------*/

//The possible states of a data chunk
typedef enum {
  CHUNK_STATE_UNCOMPRESSED=0,  //only uncompressed data available
  CHUNK_STATE_COMPRESSED=1,    //compressed data available, but nothing else
  CHUNK_STATE_FLUSHED=2        //no data available because chunk has already been flushed
} chunk_state_t;

#ifdef ENABLE_PTHREADS

//Definition and basic functions for a two-level sequence number
typedef u_int32 sequence_number_t;


typedef struct {
    int thread_id;
    char fname[100];
} thread_data_t;
// extern pthread_key_t thread_data_key;

typedef struct _sequence_t {
  sequence_number_t l1num; //first level id
  sequence_number_t l2num; //second level id
} sequence_t;

//Returns TRUE if and only if s1 == s2
static inline int sequence_eq(sequence_t s1, sequence_t s2) {
  return (s1.l1num == s2.l1num) && (s1.l2num == s2.l2num);
}

//Returns TRUE if and only if s1 < s2
static inline int sequence_lt(sequence_t s1, sequence_t s2) {
  if(s1.l1num < s2.l1num) {
    return TRUE;
  } else {
    return (s1.l1num == s2.l1num) && (s1.l2num < s2.l2num);
  }
}

//Returns TRUE if and only if s1 > s2
static inline int sequence_gt(sequence_t s1, sequence_t s2) {
  if(s1.l1num > s2.l1num) {
    return TRUE;
  } else {
    return (s1.l1num == s2.l1num) && (s1.l2num > s2.l2num);
  }
}

//Increments a sequence number. The upper bound for the 2nd level number must be specified
static inline void sequence_inc(sequence_t *s, sequence_number_t ubound) {
  assert(s!=NULL);
  s->l2num++;
  if(s->l2num >= ubound) {
  s->l1num++;
    s->l2num=0;
  }
}

//Increments L1 level of a sequence number, resetting L2
static inline void sequence_inc_l1(sequence_t *s) {
  assert(s!=NULL);
  s->l1num++;
  s->l2num=0;
}

//Increments L2 level of a sequence number
static inline void sequence_inc_l2(sequence_t *s) {
  assert(s!=NULL);
  s->l2num++;
}

//Reset a sequence number.
static inline void sequence_reset(sequence_t *s) {
  assert(s!=NULL);
  s->l1num=0;
  s->l2num=0;
}
#endif //ENABLE_PTHREADS


//The data type of a chunk, the basic work unit of dedup
//A chunk will flow through all the pipeline stages where it'll get increasingly refined
typedef struct _chunk_t {
  struct {
    int isDuplicate;        //whether this is an original chunk or a duplicate
    chunk_state_t state;    //which type of data this chunk contains
#ifdef ENABLE_PTHREADS
    //once a chunk has been added to the global database accesses
    //to the state require synchronization b/c the chunk is globally viewable
    pthread_mutex_t lock;
    pthread_cond_t update;
#endif //ENABLE_PTHREADS
  } header;
  //The SHA1 sum of the chunk, computed by SHA1/Routing stage from the uncompressed chunk data
  unsigned int sha1[SHA1_LEN/sizeof(unsigned int)]; //NOTE:: Force integer-alignment for hashtable, SHA1_LEN must be multiple of unsigned int
  //FIXME: This can be put into a union to save space.
  //The uncompressed version of the chunk, created by chunking stage(s)
  mbuffer_t uncompressed_data;
  //The compressed version of the chunk, created by compression stage
  //based on uncompressed version (only if !isDuplicate)
  mbuffer_t compressed_data;
  //reference to original chunk with compressed data (only if isDuplicate)
  struct _chunk_t *compressed_data_ref;
#ifdef ENABLE_PTHREADS
  //Original location of the chunk in input stream (for reordering)
  sequence_t sequence;
  //whether this is the last L2 chunk for the given L1 number
  int isLastL2Chunk;
#endif //ENABLE_PTHREADS
} chunk_t;
 
static std::mutex _hashmutex;
static std::map<chunk_t*, void*> _chunks_dumps;

static inline void dump_chunk(chunk_t* chunk) {
    return;
    void* buffer = malloc(sizeof(chunk_t));
    memcpy(buffer, chunk, sizeof(chunk_t));
    std::unique_lock<std::mutex> lck(_hashmutex);
    _chunks_dumps[chunk] = buffer;
}

static inline void check_chunk(chunk_t* chunk) {
    return;
    _hashmutex.lock();
    void* buffer = _chunks_dumps[chunk];
    _hashmutex.unlock();

    for (char* i = (char*)buffer, *j = (char*)chunk; i != (char*)(buffer) + sizeof(chunk_t); ++i, ++j) {
        if (*i != *j) {
            if (j == (char*)&(chunk->header.state)) { 
                printf("State discrepancy: expected %d, got %d\n", ((chunk_t*)buffer)->header.state, chunk->header.state);
            } else if (j == (char*)&(chunk->compressed_data.mcb)) {
                printf("MCB discrepancy: expected %p, got %p\n", ((chunk_t*)buffer)->compressed_data.mcb, chunk->compressed_data.mcb);
            } else {
                throw std::runtime_error("Representation of chunk has changed.");
            }
        }
    }
}

#define LEN_FILENAME 256

#define TYPE_FINGERPRINT 0
#define TYPE_COMPRESS 1
#define TYPE_ORIGINAL 2

#define QUEUE_SIZE 1024UL*1024


#define MAXBUF (128*1024*1024)     /* 128 MB for buffers */
#define ANCHOR_JUMP (2*1024*1024) //best for all 2*1024*1024

// #include "defines.h"

#define MAX_PER_FETCH 10000

#define ITEM_PER_FETCH 20
#define ITEM_PER_INSERT 20

#define CHUNK_ANCHOR_PER_FETCH 20
#define CHUNK_ANCHOR_PER_INSERT 20

#define ANCHOR_DATA_PER_INSERT 1


typedef struct {
  char infile[LEN_FILENAME];
  char outfile[LEN_FILENAME];
  int compress_type;
  int preloading;
  int nthreads;
  int verbose;
} config_t;

#define COMPRESS_GZIP 0
#define COMPRESS_BZIP2 1
#define COMPRESS_NONE 2

#define UNCOMPRESS_BOUND 10000000

extern config_t* conf;
extern struct hashtable* cache;

struct ReorderData {
    unsigned long long time;
    int l1;
    int l2;
};

extern ReorderData* reorder_data;
extern size_t reorder_data_n;

struct ReorderTreeData {
    unsigned long long time;
    long long int nb_elements;
};

extern ReorderTreeData* reorder_tree_data;
extern size_t reorder_tree_data_n;

/* struct DedupCompressData {
    int l1 = -1;
    int l2 = -1;
    bool compress;
};

extern DedupCompressData* dedupcompress_data;
extern size_t dedupcompress_data_n; */

namespace Globals {
    enum class Action {
        POP,
        PUSH
    };

    typedef std::chrono::time_point<std::chrono::steady_clock> SteadyTP;
    // typedef std::tuple<SteadyTP, SmartFIFO<chunk_t*>*, SmartFIFOImpl<chunk_t*>*, Action, size_t> SmartFIFOTS;
    // typedef std::vector<SmartFIFOTS> SmartFIFOTSV;

    // extern std::vector<std::vector<SmartFIFOTS>> _smart_fifo_ts_data;
    // extern std::mutex _smart_fifo_ts_m;
    extern SteadyTP _start_time;

    inline SteadyTP now() {
        return std::chrono::steady_clock::now();
    }
}


#endif //_DEDUPDEF_H_

