#include <string.h>

#include "queue.h"
#include "script_mgr.h"

script_mgr_t script_mgr;

void init(script_mgr_t* mgr) {
    memset(mgr->queue_push_callbacks, 0, sizeof(void*) * MAX_CALLBACKS);
    memset(mgr->queue_pop_callbacks, 0, sizeof(void*) * MAX_CALLBACKS);

    mgr->nb_push_callbacks = mgr->nb_pop_callbacks = 0;
}

void add_callback(script_mgr_t* mgr, callbacks_t callback_id, void* callback) {
    switch (callback_id) {
        case PUSH:
            mgr->queue_push_callbacks[mgr->nb_push_callbacks++] = callback;
            break;

        case POP:
            mgr->queue_pop_callbacks[mgr->nb_pop_callbacks++] = callback;
            break;

        default:
            break;
    }
}

void call_push_callbacks(script_mgr_t* mgr, queue_t* q, ringbuffer_t* r, int p) {
    for (int i = 0; i < mgr->nb_push_callbacks; ++i)
        mgr->queue_push_callbacks[i](q, r, p);
}

void call_pop_callbacks(script_mgr_t* mgr, queue_t* q, ringbuffer_t* r, int p) {
    for (int i = 0; i < mgr->nb_pop_callbacks; ++i)
        mgr->queue_pop_callbacks[i](q, r, p);
}
