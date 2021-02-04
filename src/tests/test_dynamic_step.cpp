#include <chrono>
#include <iostream>
#include <thread>

#include "promises/dynamic_step_promise.h"

template<DynamicStepPromiseMode mode>
void toto(DynamicStepPromise<int, mode>* promise) {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    promise->set(1, 42);
}

template<DynamicStepPromiseMode mode>
void test_dynamic_step_promise() {
    DynamicStepPromiseBuilder<int, mode> builder(12, 1, 8);
    DynamicStepPromise<int, mode>* promise = static_cast<DynamicStepPromise<int, mode>*>(builder.new_promise());
    promise->set(0, 1);
    std::cout << promise->get(0) << std::endl;
    
    std::thread t(toto<mode>, promise);
    std::cout << promise->get(1) << std::endl;
    
    promise->set_immediate(2, 84);
    std::cout << promise->get(2) << std::endl;

    t.join();
}

using D = DynamicStepPromiseMode;

int main() {
    /*test_dynamic_step_promise<D::SET_STEP_PRODUCER_ONLY_NO_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_PRODUCER_ONLY_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_CONSUMER_ONLY_NO_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_CONSUMER_ONLY_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_BOTH_NO_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_BOTH_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_TIMER>(); */

    test_dynamic_step_promise<D::SET_STEP_PRODUCER_ONLY>();
    test_dynamic_step_promise<D::SET_STEP_CONSUMER_ONLY>();
    test_dynamic_step_promise<D::SET_STEP_BOTH>();
    test_dynamic_step_promise<D::SET_STEP_PRODUCER_ONLY_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_CONSUMER_ONLY_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_BOTH_UNBLOCK>();
    test_dynamic_step_promise<D::SET_STEP_PRODUCER_TIMER>();
    test_dynamic_step_promise<D::SET_STEP_PRODUCER_UNBLOCK_TIMER>();
    return 0;
}
