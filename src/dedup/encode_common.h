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

int write_file(int fd, u_char type, u_long len, u_char * content);
int create_output_file(const char *outfile); 
void write_chunk_to_file(int fd, chunk_t *chunk);

void sub_Compress(chunk_t *chunk);
int sub_Deduplicate(chunk_t *chunk);

extern int rf_win;
extern int rf_win_dataprocess;

#define INITIAL_SEARCH_TREE_SIZE 4096

#endif /* ENCODE_COMMON_H */
