#include <iostream>
#include <memory>
#include <sstream>

#include <unistd.h>

#include "fifo_plus.h"

#include "lua.hpp"

namespace Basic {
    struct ThreadSpecificData {
        unsigned int _no_work;
        unsigned int _with_work;
        unsigned int _work_amount;
        unsigned int _n;
        FIFORole _role;
    };

    struct ThreadCreateData {
        pthread_t _thread_id;
        ThreadSpecificData _tss;
    };

    template<typename T>
    struct ThreadInitData {
        ThreadSpecificData _tss;
        FIFOPlus<T>* _fifo;
    };

    template<typename T>
    std::unique_ptr<FIFOPlus<T>> create_fifo(FIFOPlusPopPolicy pop_policy,
                             unsigned int n,
                             unsigned int no_work,
                             unsigned int with_work,
                             unsigned int work_amount,
                             std::vector<ThreadCreateData>& threads,
                             void* (*prod_fn)(void*), 
                             void* (*cons_fn)(void*)) {
        PThreadThreadIdentifier* identifier = new PThreadThreadIdentifier();
        identifier->register_thread();

        FIFOPlus<T>* fifo = new FIFOPlus<T>(pop_policy, identifier, threads.size());
        fifo->set_role(FIFORole::PRODUCER);
        fifo->set_n(n);
        fifo->set_thresholds(no_work, with_work, work_amount);

        for (ThreadCreateData& data: threads) {
            ThreadInitData<T>* init_data = new ThreadInitData<T>;
            init_data->_tss = data._tss;
            init_data->_fifo = fifo;

            identifier->pthread_create(&data._thread_id, NULL, data._tss._role == FIFORole::CONSUMER ? cons_fn : prod_fn, init_data);
        }

        return std::unique_ptr<FIFOPlus<T>>(fifo);
    }

    template<typename T>
    FIFOPlus<T>* init_get_fifo(void* arg) {
        std::unique_ptr<ThreadInitData<T>> data((ThreadInitData<T>*)arg);

        FIFOPlus<T>* fifo = data->_fifo;
        fifo->set_role(data->_tss._role);
        fifo->set_n(data->_tss._n);
        fifo->set_thresholds(data->_tss._no_work, data->_tss._with_work, data->_tss._work_amount);

        return fifo;
    }
}

namespace SPSC {

void* test_spsc_nowait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    fifo->set_role(FIFORole::CONSUMER);
    fifo->set_n(1);
    fifo->set_thresholds(1, 1, 0);
    
    for (int i = 0; i < 100;) {
        std::optional<int> value;
        fifo->pop(value);

        if (!value) {
            std::cout << "No value yet" << std::endl;
            continue;
        }

        if (*value != i) {
            std::ostringstream err;
            err << "[Error - spsc_nowait] Exepcted value " << i << " got value " << *value << std::endl;
            throw std::runtime_error(err.str());
        }

        std::cout << "Got value " << *value << std::endl;

        ++i;
    }

    return nullptr;
}

void* test_spsc_wait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    fifo->set_role(FIFORole::CONSUMER);
    fifo->set_n(4);
    fifo->set_thresholds(1, 1, 0);
    
    for (int i = 0; i < 100;) {
        std::optional<int> value;
        fifo->pop(value);

        if (!value) {
            throw std::runtime_error("Optional should not be empty");
        }

        if (*value != i) {
            std::ostringstream err;
            err << "[Error - spsc_nowait] Exepcted value " << i << " got value " << *value << std::endl;
            throw std::runtime_error(err.str());
        }

        std::cout << "Got value " << *value << std::endl;

        ++i;
    }

    return nullptr;
}

void test() {
    {
        /* std::unique_ptr<FIFOPlus<int>> fifo = Basic::create_fifo(FIFOPlusPopPolicy::POP_NO_WAIT, identifier, 2);
        fifo.set_role(FIFORole::PRODUCER);
        fifo.set_thresholds(1, 1, 0);
        fifo.set_n(5);

        pthread_t thread;
        identifier->pthread_create(&thread, NULL, test_spsc_nowait, &fifo);

        for (int i = 0; i < 99; ++i) {
            fifo.push(i);
            usleep(100);
        }
        fifo.push_immediate(99);

        pthread_join(thread, nullptr);

        std::cout << "[OK] test_spsc_nowait" << std::endl; */
    }

    {
        /* PThreadThreadIdentifier* identifier = new PThreadThreadIdentifier;
        identifier->register_thread();

        FIFOPlus<int> fifo(FIFOPlusPopPolicy::POP_WAIT, );
        fifo.set_role(FIFORole::PRODUCER);
        fifo.set_thresholds(1, 1, 0);
        fifo.set_n(1);

        pthread_t thread;
        identifier->pthread_create(&thread, NULL, test_spsc_wait, &fifo);

        for (int i = 0; i < 99; ++i) {
            fifo.push(i);
            usleep(100);
        }
        fifo.push_immediate(99);

        pthread_join(thread, nullptr);

        std::cout << "[OK] test_spsc_wait" << std::endl; */
    }
}

}

void* test_spmc_nowait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    
    return nullptr;
}

void* test_spmc_wait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    
    return nullptr;
}

void test_spmc() {
    {
        
    }
}


void* test_mpsc_nowait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    
    return nullptr;
}

void* test_mpsc_wait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    
    return nullptr;
}

void test_mpsc() {

}


void* test_mpmc_nowait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    
    return nullptr;
}

void* test_mpmc_wait(void* arg) {
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    
    return nullptr;
}

void test_mpmc() {

}


lua_State* lua_init() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    return L;
}
int main(int argc, char** argv) {
    std::string filename;
    if (argc == 1) {
        filename = "default.lua";
    } else {
        filename = std::string(argv[1]);
    }

    lua_State* L = lua_init();
    luaL_dofile(L, filename.c_str());
    /* SPSC::test();
    test_spmc();
    test_mpsc();
    test_mpmc(); */
    lua_close(L);
    return 0;
}
