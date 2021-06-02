#ifndef SCRIPT_MGR_H
#define SCRIPT_MGR_H

#define MAX_CALLBACKS 100

#include "queue.h"

#include <list>
#include <functional>

namespace Scripting {
    enum class callbacks {
        PUSH = 0,
        POP = 1
    };

    typedef std::function<void(queue_t*, ringbuffer_t*, int)> queue_push_function;
    typedef queue_push_function queue_pop_function;
}

class script_mgr {
private:
    script_mgr();
    std::list<Scripting::queue_push_function> _push_callbacks;
    std::list<Scripting::queue_pop_function> _pop_callbacks;

public:
    void add_push_callback(Scripting::queue_push_function&&);
    void add_pop_callback(Scripting::queue_pop_function&&);

    void call_push_callbacks(queue_t*, ringbuffer_t*, int);
    void call_pop_callbacks(queue_t*, ringbuffer_t*, int);

    static script_mgr& instance();
};

#define sScriptMgr (&script_mgr::instance())

#include "script_mgr.tpp"

#endif // SCRIPT_MGR_H
