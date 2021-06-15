#pragma once

#include <memory>

#include "fifo_plus.h"

namespace Basic {
    struct ThreadSpecificData {
        unsigned int _n;
        unsigned int _no_work;
        unsigned int _with_work;
        unsigned int _work_amount;
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
    FIFOPlus<T>* init_get_fifo(void* arg) {
        std::unique_ptr<ThreadInitData<T>> data((ThreadInitData<T>*)arg);

        FIFOPlus<T>* fifo = data->_fifo;
        fifo->set_role(data->_tss._role);
        fifo->set_n(data->_tss._n);
        fifo->set_thresholds(data->_tss._no_work, data->_tss._with_work, data->_tss._work_amount);

        return fifo;
    }
}
