#include "naive_promise.h"

NaivePromiseBase::NaivePromiseBase(int nb_values, PromisePlusWaitMode wait_mode) {
    if (wait_mode == PromisePlusWaitMode::ACTIVE) {
        _ready_strong = std::make_unique<std::atomic<bool>[]>(nb_values);
    } else {
        _wait_m = std::make_unique<std::pair<std::mutex, std::condition_variable>[]>(nb_values);
        _ready_weak = std::make_unique<bool[]>(nb_values);
    }

    _set_m = std::make_unique<SetMutex[]>(nb_values);
}