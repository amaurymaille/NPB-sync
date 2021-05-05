#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <stdlib.h>

#ifdef ENABLE_PTHREADS
#include <pthread.h>
#endif //ENABLE_PTHREADS

//A simple ring buffer that can store a certain number of elements.
//This is used for two purposes:
// 1. To manage the elements inside a queue
// 2. To allow queue users aggregate queue operations
struct _ringbuffer_t {
  int head, tail;
  void **data;
  size_t size;
  size_t n_elements;
};

typedef struct _ringbuffer_t ringbuffer_t;

//A synchronized queue.
//Basically just a ring buffer with some synchronization added
struct _queue_t {
  ringbuffer_t buf;
  int nProducers;
  int nTerminated;
#ifdef ENABLE_PTHREADS
  pthread_mutex_t mutex;
  pthread_cond_t notEmpty, notFull;
#endif //ENABLE_PTHREADS
};

typedef struct _queue_t queue_t;



/*
 * Some simple inline functions to work with ring buffers
 */

//Initialize a ring buffer
static inline int ringbuffer_init(ringbuffer_t *buf, size_t size) {
  //NOTE: We have to allocate one extra element because one element will be unusable (we need to distinguish between full and empty).
  buf->data = (void **)malloc(sizeof(void*) * (size+1));
  buf->size = (size+1);
  buf->head = 0;
  buf->tail = 0;
  buf->n_elements = 0;

  return (buf->data==NULL);
}

//Destroy a ring buffer
static inline int ringbuffer_destroy(ringbuffer_t *buf) {
  free(buf->data);
  return 0;
}

//Returns true if and only if the ring buffer is empty
static inline int ringbuffer_isEmpty(ringbuffer_t *buf) {
  return (buf->tail == buf->head);
}

//Returns true if and only if the ring buffer is full
static inline int ringbuffer_isFull(ringbuffer_t *buf) {
  return (buf->head == (buf->tail-1+buf->size)%buf->size);
}

//Get an element from a ringbuffer
//Returns NULL if buffer is empty
static inline void *ringbuffer_remove(ringbuffer_t *buf) {
  void *ptr;

  if(ringbuffer_isEmpty(buf)) {
    ptr = NULL;
  } else {
    ptr = buf->data[buf->tail];
    buf->tail++;
    if(buf->tail >= buf->size) buf->tail = 0;
  }

  --buf->n_elements;
  assert(buf->n_elements >= 0);
  return ptr;
}

//Put an element into a ringbuffer
//Returns 0 if the operation succeeded
static inline int ringbuffer_insert(ringbuffer_t *buf, void *ptr) {
  if(ringbuffer_isFull(buf)) return -1;
  buf->data[buf->head] = ptr;
  buf->head++;
  if(buf->head == buf->size) buf->head = 0;

  ++buf->n_elements;
  return 0;
}

// Return the number of elements in the buffer
static inline int ringbuffer_nb_elements(ringbuffer_t const* buf) {
  return buf->n_elements;
}

/* Resize the ringbuffer.
 *
 * If the new size would result in content loss, the function does nothing.
 * Content loss happens when the new size is lower than the amont of elements
 * already stored in the buffer.
 *
 * Returns 0 on success, -1 on failure. In case of failure, the ringbuffer is 
 * not modified.
 */
/* static inline int ringbuffer_resize(ringbuffer_t* buf, unsigned int new_size) {
  if (new_size == buf->size) {
    return 0;
  }

  if (new_size > buf->size) {
    buf->size = new_size;
  } else {
    if (buf->n_elements > new_size) {
      return -1;
    } else {
      buf->size = new_size;
    }
  }

  return 0;
} */

/* Reinit a ringbuffer.
 * All elements are discarded and the size if set to new_size.
 */
static inline void ringbuffer_reinit(ringbuffer_t* buf, unsigned int new_size) {
  ringbuffer_destroy(buf);
  ringbuffer_init(buf, new_size);
}

/*
 * Queue interface
 */

void queue_init(queue_t * que, size_t size, int nProducers);
void queue_destroy(queue_t * que);

void queue_terminate(queue_t * que);

int queue_dequeue(queue_t *que, ringbuffer_t *buf, int min, int limit);
int queue_enqueue(queue_t *que, ringbuffer_t *buf, int limit);

#endif //_QUEUE_H_

