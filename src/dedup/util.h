#ifndef _UTIL_H_
#define _UTIL_H_

#include "dedupdef.h"

/* File I/O with error checking */
int xread(int sd, void *buf, size_t len);
int xwrite(int sd, const void *buf, size_t len);

/* Process file header */
int read_header(int fd, byte *compress_type);
int write_header(int fd, byte compress_type);

/* When puhsing towards the refine queue / when pulling from the refine queue */
void update_refine_step(unsigned int* step, unsigned int it);
/* When pushing towards the deduplicate queue / pulling from the dedup queue */
void update_dedup_step(unsigned int* step, unsigned int it);
/* When pushing towards the compress queue / pulling from the compress queue */
void update_compress_step(unsigned int* step, unsigned int it);
/* When pushing towards the reorder queue / pulling from the reorder queue */
void update_reorder_step(unsigned int* step, unsigned int it);

#endif //_UTIL_H_

