#include <chrono>
#include <iostream>
#include <random>
#include <thread>

#include "promises/dynamic_step_promise.h"

using D = DynamicStepPromiseMode;

template<DynamicStepPromiseMode mode>
void toto(DynamicStepPromise<int, mode>* promise) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    promise->set(1, 42);
}

template<DynamicStepPromiseMode mode>
void test_dynamic_step_promise_base() {
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

template<DynamicStepPromiseMode mode>
void test_dynamic_step_promise_prod(DynamicStepPromise<int, mode>* promise);

template<DynamicStepPromiseMode mode>
void test_dynamic_step_promise_cons(DynamicStepPromise<int, mode>* promise);

template<DynamicStepPromiseMode mode>
void test_dynamic_step_promise(int nb_values, int start_step, int n_threads) {
    DynamicStepPromiseBuilder<int, mode> builder(nb_values, start_step, n_threads);
    DynamicStepPromise<int, mode>* prom = static_cast<DynamicStepPromise<int, mode>*>(builder.new_promise());
    
    std::thread t1(test_dynamic_step_promise_prod<mode>, prom);
    std::thread t2(test_dynamic_step_promise_cons<mode>, prom);

    t1.join();
    t2.join();

    delete prom;
}

template<DynamicStepPromiseMode mode>
static void display_value(DynamicStepPromise<int, mode>* promise, int n) {
    std::cout << promise->get(n) << std::endl;
}

#define DSP(MODE) DynamicStepPromise<int, MODE>* promise
#define DSP_TESTP(PM) template<> void test_dynamic_step_promise_prod<PM>(DSP(PM))
#define DSP_TESTC(PM) template<> void test_dynamic_step_promise_cons<PM>(DSP(PM))

DSP_TESTP(D::SET_STEP_PRODUCER_ONLY) {
    promise->set(0, 1);
    promise->set(1, 2);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set_step(3);
    promise->set(2, 3);
    promise->set(3, 4);
    promise->set(4, 5);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set_step(10);
    promise->set(5, 6);
    promise->set(6, 7);
    promise->set(7, 8);
    promise->set_step(3); // No unblock here !
    std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set(8, 9); // Unblock, four values available
}

DSP_TESTC(D::SET_STEP_PRODUCER_ONLY) {
    for (int i = 0; i < 9; ++i)
        std::cout << promise->get(i) << std::endl;
}

DSP_TESTP(D::SET_STEP_PRODUCER_ONLY_UNBLOCK) {
    promise->set(0, 1);
    promise->set(1, 2);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set_step(3);
    promise->set(2, 3);
    promise->set(3, 4);
    promise->set(4, 5);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set_step(10);
    promise->set(5, 6);
    promise->set(6, 7);
    promise->set(7, 8);
    promise->set_step(3); // This should unblock
    std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set(8, 9);
    promise->set(9, 10);
    promise->set(10, 11);
}

DSP_TESTC(D::SET_STEP_PRODUCER_ONLY_UNBLOCK) {
    for (int i = 0; i < 11; ++i)
        std::cout << promise->get(i) << std::endl;
}

DSP_TESTP(D::SET_STEP_CONSUMER_ONLY) {
    promise->set(0, 0);
    promise->set(1, 1);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    promise->set(2, 2);
    promise->set(3, 3);
    promise->set(4, 4);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set(5, 5);
    promise->set(6, 6);
    promise->set(7, 7);
    promise->set(8, 8);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    promise->set(9, 9);
    promise->set(10, 10);
}

DSP_TESTC(D::SET_STEP_CONSUMER_ONLY) {
    display_value(promise, 0);
    display_value(promise, 1);
    promise->set_step(3);
    display_value(promise, 2);
    display_value(promise, 3);
    display_value(promise, 4);
    promise->set_step(10);

    auto step_change_fn = [promise](int s, int n) { 
        std::this_thread::sleep_for(std::chrono::seconds(s));
        promise->set_step(n);
    };

    std::thread step_changer(step_change_fn, 1, 4);
    display_value(promise, 5);
    display_value(promise, 6);
    display_value(promise, 7);
    display_value(promise, 8);
    step_changer.join();
    promise->set_step(1);
    // If we don't wait long enough in the producer function, the
    // next line deadlocks because the change is performed too late
    // and we would need another set in the producer to realise 
    // something changed
    // step_changer = std::move(std::thread(step_change_fn, 2, 1));
    display_value(promise, 9);
    display_value(promise, 10);
}

DSP_TESTP(D::SET_STEP_CONSUMER_ONLY_UNBLOCK) {
    promise->set(0, 0);
    promise->set(1, 1);
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    promise->set(2, 2);
    promise->set(3, 3);
    promise->set(4, 4);
    // std::this_thread::sleep_for(std::chrono::seconds(2));
    promise->set(5, 5);
    promise->set(6, 6);
    promise->set(7, 7);
    promise->set(8, 8);
    //std::this_thread::sleep_for(std::chrono::seconds(1));
    promise->set(9, 9);
    promise->set(10, 10);

}

DSP_TESTC(D::SET_STEP_CONSUMER_ONLY_UNBLOCK) {
    display_value(promise, 0);
    display_value(promise, 1);
    promise->set_step(3);
    display_value(promise, 2);
    display_value(promise, 3);
    display_value(promise, 4);
    promise->set_step(10);

    auto step_change_fn = [promise](int s, int n) { 
        std::this_thread::sleep_for(std::chrono::seconds(s));
        promise->set_step(n);
    };

    std::thread step_changer(step_change_fn, 1, 4);
    display_value(promise, 5);
    display_value(promise, 6);
    display_value(promise, 7);
    display_value(promise, 8);
    step_changer.join();
    // promise->set_step(1);
    step_changer = std::move(std::thread(step_change_fn, 2, 1));
    display_value(promise, 9);
    display_value(promise, 10);
    step_changer.join();
}

DSP_TESTP(D::SET_STEP_PRODUCER_TIMER) {
    unsigned int old_step = promise->get_step();
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution dist(1, 19);
    for (int i = 0; i < 10000; ++i) {
        promise->set(i, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(mt)));
        unsigned int new_step = promise->get_step();
        if (new_step != old_step) {
            std::cout << "Step: " << old_step << " => " << new_step << "(i = " << i << ")" << std::endl;
            old_step = new_step;
        }
    }
}

DSP_TESTC(D::SET_STEP_PRODUCER_TIMER) {
}

int main() {
/*    test_dynamic_step_promise_base<D::SET_STEP_PRODUCER_ONLY>();
    test_dynamic_step_promise_base<D::SET_STEP_CONSUMER_ONLY>();
    test_dynamic_step_promise_base<D::SET_STEP_BOTH>();
    test_dynamic_step_promise_base<D::SET_STEP_PRODUCER_ONLY_UNBLOCK>();
    test_dynamic_step_promise_base<D::SET_STEP_CONSUMER_ONLY_UNBLOCK>();
    test_dynamic_step_promise_base<D::SET_STEP_BOTH_UNBLOCK>();
    test_dynamic_step_promise_base<D::SET_STEP_PRODUCER_TIMER>();
    test_dynamic_step_promise_base<D::SET_STEP_PRODUCER_UNBLOCK_TIMER>();

    std::cout << "Producer only" << std::endl;
    test_dynamic_step_promise<D::SET_STEP_PRODUCER_ONLY>(100, 2, 1);
    std::cout << "Producer only + unblock" << std::endl;
    test_dynamic_step_promise<D::SET_STEP_PRODUCER_ONLY_UNBLOCK>(100, 2, 1);
    std::cout << "Consumer only" << std::endl;
    test_dynamic_step_promise<D::SET_STEP_CONSUMER_ONLY>(100, 2, 1);
    std::cout << "Consumer only + unblock" << std::endl;
    test_dynamic_step_promise<D::SET_STEP_CONSUMER_ONLY_UNBLOCK>(100, 2, 1); */
    std::cout << "Producer only + timer" << std::endl;
    test_dynamic_step_promise<D::SET_STEP_PRODUCER_TIMER>(10000, 1, 1);

    return 0;
}
