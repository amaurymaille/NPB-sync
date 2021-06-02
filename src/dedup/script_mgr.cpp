#include <utility>

#include "queue.h"
#include "script_mgr.h"

script_mgr& script_mgr::instance() {
    static script_mgr instance;
    return instance;
}

script_mgr::script_mgr() {

}

void script_mgr::add_push_callback(Scripting::queue_push_function&& f) {
    this->_push_callbacks.push_back(std::move(f));
}

void script_mgr::add_pop_callback(Scripting::queue_pop_function&& f) {
    this->_pop_callbacks.push_back(std::move(f));
}

void script_mgr::call_push_callbacks(queue_t* q, ringbuffer_t* r, int i) {
    for (Scripting::queue_push_function& f: _push_callbacks) {
        f(q, r, i);
    }
}

void script_mgr::call_pop_callbacks(queue_t* q, ringbuffer_t* r, int i) { 
    for (Scripting::queue_pop_function& f: _pop_callbacks) {
        f(q, r, i);
    }
}
