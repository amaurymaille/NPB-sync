#ifndef DYNAMIC_STEP_PROMISE_H
#define DYNAMIC_STEP_PROMISE_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "promise_plus.h"

enum class DynamicStepPromiseMode {
    SET_STEP_PRODUCER_ONLY = 1 << 0, // Only the producer will call set_step. 
                                                // Changing the step doesn't unblock others
    SET_STEP_CONSUMER_ONLY = 1 << 1, // Only consumers will call set_step
                                                // Changing the step doesn't unblock others
    SET_STEP_BOTH          = 1 << 2, // Producers and consumers will call set_step
                                                // Changing the step doesn't unblock others
    SET_STEP_UNBLOCK       = 1 << 3, // Changing the step unblocks consumers
    SET_STEP_PRODUCER_ONLY_UNBLOCK = SET_STEP_PRODUCER_ONLY | SET_STEP_UNBLOCK,
    SET_STEP_CONSUMER_ONLY_UNBLOCK = SET_STEP_CONSUMER_ONLY | SET_STEP_UNBLOCK,
    SET_STEP_BOTH_UNBLOCK = SET_STEP_BOTH | SET_STEP_UNBLOCK,
    SET_STEP_TIMER         = 1 << 4, // Step is autotuned during the calls to set / set_immediate
    SET_STEP_PRODUCER_TIMER = SET_STEP_PRODUCER_ONLY | SET_STEP_TIMER,
    SET_STEP_UNBLOCK_TIMER = SET_STEP_TIMER | SET_STEP_UNBLOCK,
    SET_STEP_PRODUCER_UNBLOCK_TIMER = SET_STEP_PRODUCER_ONLY | SET_STEP_UNBLOCK_TIMER
};

template<DynamicStepPromiseMode mode>
struct IsConsumer {
    constexpr static bool value = int(mode) & int(DynamicStepPromiseMode::SET_STEP_CONSUMER_ONLY) != 0;
};

template<DynamicStepPromiseMode mode>
constexpr bool IsConsumerV = IsConsumer<mode>::value;

template<DynamicStepPromiseMode mode>
struct IsProducer {
    constexpr static bool value = int(mode) & int(DynamicStepPromiseMode::SET_STEP_PRODUCER_ONLY) != 0;
};

template<DynamicStepPromiseMode mode>
constexpr bool IsProducerV = IsProducer<mode>::value;

template<DynamicStepPromiseMode mode>
struct IsBoth {
    constexpr static bool value = int(mode) & int(DynamicStepPromiseMode::SET_STEP_BOTH) != 0;
};

template<DynamicStepPromiseMode mode>
constexpr bool IsBothV = IsBoth<mode>::value;

template<DynamicStepPromiseMode mode>
constexpr bool IsTimerV = int(mode) & int(DynamicStepPromiseMode::SET_STEP_TIMER) != 0;

template<DynamicStepPromiseMode mode>
struct Unblocks {
    constexpr static bool value = int(mode) & int(DynamicStepPromiseMode::SET_STEP_UNBLOCK) != 0;
};

template<DynamicStepPromiseMode mode>
constexpr bool UnblocksV = Unblocks<mode>::value;

template<DynamicStepPromiseMode mode>
constexpr bool NUnblocksV = !UnblocksV<mode> && !IsTimerV<mode>;

/* template<DynamicStepPromiseMode mode>
constexpr bool CanSetStepV = !IsTimerV<mode>; */

template<DynamicStepPromiseMode mode>
constexpr bool RequiresLockV = UnblocksV<mode> && (IsConsumerV<mode> || IsBothV<mode>);

template<DynamicStepPromiseMode mode>
constexpr bool RequiresStrongSyncV = IsProducerV<mode> || IsBothV<mode>;

template<typename T, DynamicStepPromiseMode mode>
class DynamicStepPromiseBuilder : public PromisePlusBuilder<T> {
public:
    DynamicStepPromiseBuilder(int, unsigned int, unsigned int);
    PromisePlus<T>* new_promise() const;

private:
    int _nb_values;
    unsigned int _start_step;
    unsigned int _n_threads;
};

template<typename T, DynamicStepPromiseMode mode>
class DynamicStepPromise : public PromisePlus<T> {
    static_assert(mode != DynamicStepPromiseMode::SET_STEP_UNBLOCK);
    static_assert(mode != DynamicStepPromiseMode::SET_STEP_TIMER);
    static_assert(mode != DynamicStepPromiseMode::SET_STEP_UNBLOCK_TIMER);
public:
    DynamicStepPromise(int nb_values, unsigned int start_step);

    NO_COPY_T2(DynamicStepPromise, T, mode);

    T& get(int index);

    void set(int index, const T& value);
    void set(int index, T&& value);

    void set_no_timer(int index, const T& value);
    void set_no_timer(int index, T&& value);

    void set_immediate(int index, const T& value);
    void set_immediate(int index, T&& value);

    void set_immediate_no_timer(int index, const T& value);
    void set_immediate_no_timer(int index, T&& value);

    friend PromisePlus<T>* DynamicStepPromiseBuilder<T, mode>::new_promise() const;

    void set_step(unsigned int new_step);

private:
    // Producer writes, consumers read
    std::atomic<int> _last_unblock_index_strong;
    // Consumers read and write
    std::vector<int> _last_unblock_index_weak;
    // Only used if (UnblocksV<mode>)
    // Stores the last index passed to set / set_immediate
    // 
    // Producer writes and may read (IsProducerV<mode> || IsBothV<mode>) 
    // Consumers may read (IsConsumerV<mode> || IsBothV<mode>)
    std::atomic<int> _current_index;
    // Only used if (RequiresLockV<mode>): changing the step unblocks consumers, and
    // consumers are allowed to change the step
    //
    // Prevents _current_index from moving back and forth
    std::mutex _step_m;
    // Producer reads and may write (IsProducerV<mode> || IsBothV<mode>), consumers may read or write
    std::atomic<unsigned int> _step;
    // Producer may read and may write (IsTimerV<mode>)
    std::vector<std::chrono::time_point<std::chrono::steady_clock>> _sets_times;

    inline void set_current_index(int index) {
        if constexpr (UnblocksV<mode>) {
            if constexpr (IsProducerV<mode>) {
                _current_index.store(index, std::memory_order_relaxed);
            } else {
                _current_index.store(index, std::memory_order_release);
            }
        } else {
            (void)index;
        }
    }

    inline unsigned int get_step() const {
        if constexpr (RequiresLockV<mode>) {
            return _step.load(std::memory_order_acquire);
        } else {
            return _step.load(std::memory_order_relaxed);
        }
    }
};

#include "dynamic_step_promise/dynamic_step_promise.tpp"

#endif // DYNAMIC_STEP_PROMISE_H
