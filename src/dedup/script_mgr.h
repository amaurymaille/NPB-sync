#ifndef SCRIPT_MGR_H
#define SCRIPT_MGR_H

#define MAX_CALLBACKS 100

#include "queue.h"

typedef struct script_mgr_s {
    void* (*queue_push_callbacks[MAX_CALLBACKS])(queue_t*, ringbuffer_t*, int);
    void* (*queue_pop_callbacks[MAX_CALLBACKS])(queue_t*, ringbuffer_t*, int);

    int nb_push_callbacks;
    int nb_pop_callbacks;
} script_mgr_t;

typedef enum callbacks_e {
    PUSH = 0,
    POP = 1
} callbacks_t;

void init(script_mgr_t* mgr);
void add_callback(script_mgr_t* mgr, callbacks_t callback_id, void* callback);

// Callbacks are called in critical section
void call_push_callbacks(script_mgr_t* mgr, queue_t*, ringbuffer_t*, int);
void call_pop_callbacks(script_mgr_t* mgr, queue_t*, ringbuffer_t*, int);

extern script_mgr_t script_mgr;

#endif // SCRIPT_MGR_H
