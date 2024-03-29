#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "util.h"
#include "queue.h"
#include "config.h"
#include "script_mgr.h"

#ifdef ENABLE_PTHREADS
#include <pthread.h>
#endif //ENABLE_PTHREADS

void queue_init(queue_t * que, size_t size, int nProducers) {
#ifdef ENABLE_PTHREADS
  pthread_mutex_init(&que->mutex, NULL);
  pthread_cond_init(&que->notEmpty, NULL);
  pthread_cond_init(&que->notFull, NULL);
#endif
  int init_res = ringbuffer_init(&(que->buf), size);
  assert(!init_res);
  que->nProducers = nProducers;
  que->nTerminated = 0;
}

void queue_destroy(queue_t * que) {
#ifdef ENABLE_PTHREADS
  pthread_mutex_destroy(&que->mutex);
  pthread_cond_destroy(&que->notEmpty);
  pthread_cond_destroy(&que->notFull);
#endif
  ringbuffer_destroy(&(que->buf));
}

/* Private function which requires synchronization */
static inline int queue_isTerminated(queue_t * que) {
  assert(que->nTerminated <= que->nProducers);
  return que->nTerminated == que->nProducers;
}

static inline bool queue_almost_finished(queue_t const* que) {
  return que->nTerminated == que->nProducers - 1;
}

void queue_terminate(queue_t * que) {
#ifdef ENABLE_PTHREADS
  pthread_mutex_lock(&que->mutex);
#endif
  que->nTerminated++;
  assert(que->nTerminated <= que->nProducers);
#ifdef ENABLE_PTHREADS
  if(queue_isTerminated(que)) { pthread_cond_broadcast(&que->notEmpty); }
  pthread_mutex_unlock(&que->mutex);
#endif
}

int queue_dequeue(queue_t *que, ringbuffer_t *buf, int limit) {
  int i;

#ifdef ENABLE_PTHREADS
  pthread_mutex_lock(&que->mutex);
  while (ringbuffer_isEmpty(&que->buf) && !queue_isTerminated(que)) {
    pthread_cond_wait(&que->notEmpty, &que->mutex);
  }
#endif
  if (ringbuffer_isEmpty(&que->buf) && queue_isTerminated(que)) {
#ifdef ENABLE_PTHREADS
    pthread_mutex_unlock(&que->mutex);
#endif
    return -1;
  }

  // sScriptMgr->call_pop_callbacks(que, buf, limit);
  // printf("[queue dequeue before] Ringbuffer %p has %d elements\n", &que->buf, ringbuffer_nb_elements(&que->buf));
  //NOTE: This can be optimized by copying whole segments of pointers with memcpy. However,
  //      `limit' is typically small so the performance benefit would be negligible.
  for(i=0; i<limit && !ringbuffer_isEmpty(&que->buf) && !ringbuffer_isFull(buf); i++) {
    void *temp;
    int rv;

    temp = ringbuffer_remove(&que->buf);
    assert(temp!=NULL);
    rv = ringbuffer_insert(buf, temp);
    assert(rv==0);
  }
  // printf("[queue dequeue after] Ringbuffer %p has %d elements\n", &que->buf, ringbuffer_nb_elements(&que->buf));

#ifdef ENABLE_PTHREADS
  if(i>0) pthread_cond_signal(&que->notFull);
  pthread_mutex_unlock(&que->mutex);
#endif
  return i;
}

int queue_enqueue(queue_t *que, ringbuffer_t *buf, int limit) {
  int i;

#ifdef ENABLE_PTHREADS
  pthread_mutex_lock(&que->mutex);
  assert(!queue_isTerminated(que));
  while (ringbuffer_isFull(&que->buf)) {
    pthread_cond_wait(&que->notFull, &que->mutex);
  }
#else
  assert(!queue_isTerminated(que));
#endif

  // sScriptMgr->call_push_callbacks(que, buf, limit);
  // printf("[queue enqueue before] Ringbuffer %p has %d elements\n", &que->buf, ringbuffer_nb_elements(&que->buf));
  //NOTE: This can be optimized by copying whole segments of pointers with memcpy. However,
  //      `limit' is typically small so the performance benefit would be negligible.
  for(i=0; i<limit && !ringbuffer_isFull(&que->buf) && !ringbuffer_isEmpty(buf); i++) {
    void *temp;
    int rv;

    temp = ringbuffer_remove(buf);
    assert(temp!=NULL);
    rv = ringbuffer_insert(&que->buf, temp);
    assert(rv==0);
  }
  // printf("[queue enqueue after] Ringbuffer %p has %d elements\n", &que->buf, ringbuffer_nb_elements(&que->buf));
#ifdef ENABLE_PTHREADS
  if(i>0) { pthread_cond_signal(&que->notEmpty); }
  pthread_mutex_unlock(&que->mutex);
#endif
  return i;
}
