#include <sol/sol.hpp>
#include <pthread.h>

#include <list>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "naive_queue.hpp"

pthread_barrier_t barrier;

struct QueueObs {
    NaiveQueueImpl<uint64_t>* _queue = nullptr;
    Observer<uint64_t>* _observer = nullptr;
};

struct Pipeline {
    Pipeline(uint64_t coarse, uint64_t fine, float repartition) : _n_coarse(coarse), _n_fine(fine), _repartition(repartition) { }

    void set_work(size_t pos, uint64_t work) { _work[pos] = work; }

    void run();

    // Coarse per branch of the pipeline
    uint64_t _n_coarse;
    // Fine per coarse chunk
    uint64_t _n_fine;
    uint64_t _work[5];
    // The chance (between 0 and 100) that a refined chunk will go TO compress.
    // For instance, if this is sixty, then on AVERAGE 60% of all refined chunk
    // will go to compress.
    float _repartition;
};

void Stage1(std::vector<QueueObs*>& out, uint64_t n_coarse, uint64_t work) {
    pthread_barrier_wait(&barrier);
    size_t queue_id = 0;
    for (uint64_t i = 0; i < n_coarse * out.size(); ++i) {
        NaiveQueueImpl<uint64_t>* destination_queue = out[queue_id]->_queue;
        for (volatile uint64_t j = 0; j < work; ++j)
            ;
        destination_queue->push(i);

        queue_id++;
        queue_id %= out.size();
    }

    for (QueueObs* q: out) {
        q->_queue->terminate();
    }
}

void Stage2(QueueObs& in, QueueObs& out, uint64_t n_in, uint64_t n_fine, uint64_t work) {
    pthread_barrier_wait(&barrier);
    for (uint64_t i = 0; i < n_in; ++i) {
        auto value = in._queue->pop();
        if (!value)
            break;

        for (uint64_t j = 0; j < n_fine; ++j) {
            for (volatile uint64_t k = 0; k < work; ++k)
                ;
            out._queue->generic_push(out._observer, *value * n_fine + j);
        }
    }

    out._queue->terminate();
}

bool Stage3Helper(uint64_t source, uint64_t work) {
    return true;
}

void Stage3(QueueObs& in, QueueObs& first_out, QueueObs& second_out, uint64_t work) {
    pthread_barrier_wait(&barrier);
    while (true) {
        auto value = in._queue->pop();
        if (!value)
            break;

        if (Stage3Helper(*value, work)) {
            first_out._queue->generic_push(first_out._observer, *value);
        } else {
            second_out._queue->generic_push(second_out._observer, *value);
        }
    }

    first_out._queue->terminate();
    second_out._queue->terminate();
}

void Stage4(QueueObs& in, QueueObs& out, uint64_t work) {
    pthread_barrier_wait(&barrier);
    while (true) {
        auto value = in._queue->pop();
        if (!value)
            break;

        for (volatile uint64_t i = 0; i < work; ++i)
            ;

        out._queue->generic_push(out._observer, *value);
    }

    out._queue->terminate();
}

void Stage5(std::vector<QueueObs*>& in, uint64_t work) {
    pthread_barrier_wait(&barrier);
    std::set<uint64_t> values;
    std::set<uint64_t> waiting_values;

    size_t qid = 0;
    NaiveQueueImpl<uint64_t>* current_fifo;
    Observer<uint64_t>* current_observer;

    while (true) {
        std::optional<uint64_t> v;

        for (int i = 0; i < in.size(); ++i) {
            v = in[qid]->_queue->pop(); 
            current_fifo = in[qid]->_queue;
            current_observer = in[qid]->_observer;

            qid = (qid + 1) % in.size();

            if (v)
                break;
        }

        if (!v)
            break;

        uint64_t value = *v;

        if (value != 0) {
            if (values.find(value - 1) == values.end()) {
                waiting_values.insert(value); 
            } else {
                values.insert(value);

                uint64_t searching = value + 1;
                auto iter = waiting_values.find(searching);
                while (iter != waiting_values.end()) {
                    waiting_values.erase(iter);
                    values.insert(searching);
                    searching++;
                    iter = waiting_values.find(searching);
                }
            }
        } else {
            values.insert(value);

            uint64_t searching = value + 1;
            auto iter = waiting_values.find(searching);
            while (iter != waiting_values.end()) {
                waiting_values.erase(iter);
                values.insert(searching);
                searching++;
                iter = waiting_values.find(searching);
            }
        }
    }
}

void launch(Pipeline const& pipeline) {
    std::vector<std::thread> threads;
    std::vector<QueueObs*> fragment_to_refine;
    // Prod / Cons / Prod / Cons ...
    std::vector<QueueObs*> refine_to_deduplicate;
    // Prod / N Cons / Prod / N Cons
    std::vector<QueueObs*> deduplicate_to_compress;
    // Dedup / N Compress / Reorder / Dedup / N Compress / Reorder
    std::vector<QueueObs*> deduplicate_compress_to_reorder;

    constexpr size_t n_fragment = 1;
    constexpr size_t n_refine = 2;
    constexpr size_t n_deduplicate = 2;
    constexpr size_t n_compress = 8;
    constexpr size_t n_reorder = 1;

    constexpr size_t n_fragment_to_refine = 1;
    constexpr size_t n_refine_to_deduplicate = 1;
    constexpr size_t n_deduplicate_to_compress = 1;
    constexpr size_t n_compress_to_reorder = 4;
    constexpr size_t n_deduplicate_compress_to_reorder = n_deduplicate_to_compress + n_compress_to_reorder;

    size_t total_chunks = pipeline._n_coarse * n_refine * pipeline._n_fine;
    size_t compress_amount = total_chunks * pipeline._repartition / 100;

    std::list<std::pair<Observer<uint64_t>*, NaiveQueueMaster<uint64_t>*>> fragment_to_refine_src;
    std::list<std::pair<Observer<uint64_t>*, NaiveQueueMaster<uint64_t>*>> refine_to_deduplicate_src;
    std::list<std::pair<Observer<uint64_t>*, NaiveQueueMaster<uint64_t>*>> deduplicate_to_compress_src;
    std::list<std::pair<Observer<uint64_t>*, NaiveQueueMaster<uint64_t>*>> deduplicate_compress_to_reorder_src;

    constexpr size_t space = 1024 * 1024;

    std::set<Observer<uint64_t>*> created_observers;

    // Create masters
    for (int i = 0; i < 2; ++i) {
        NaiveQueueMaster<uint64_t>* f_to_r = new NaiveQueueMaster<uint64_t>(space, n_fragment_to_refine);
        fragment_to_refine_src.push_back(std::make_pair(nullptr, f_to_r));

        NaiveQueueMaster<uint64_t>* r_to_d = new NaiveQueueMaster<uint64_t>(space, n_refine_to_deduplicate);
        Observer<uint64_t>* r_to_d_obs = new Observer<uint64_t>(pipeline._n_fine, 2);
        refine_to_deduplicate_src.push_back(std::make_pair(r_to_d_obs, r_to_d));
        created_observers.insert(r_to_d_obs);

        NaiveQueueMaster<uint64_t>* d_to_c = new NaiveQueueMaster<uint64_t>(space, n_deduplicate_to_compress);
        Observer<uint64_t>* d_to_c_obs = new Observer<uint64_t>(compress_amount / 2, 5);
        deduplicate_to_compress_src.push_back(std::make_pair(d_to_c_obs, d_to_c));
        created_observers.insert(d_to_c_obs);

        NaiveQueueMaster<uint64_t>* dc_to_r = new NaiveQueueMaster<uint64_t>(space, n_deduplicate_compress_to_reorder);
        Observer<uint64_t>* dc_to_r_obs = new Observer<uint64_t>(total_chunks / 2, 6);
        deduplicate_compress_to_reorder_src.push_back(std::make_pair(dc_to_r_obs, dc_to_r));
        created_observers.insert(dc_to_r_obs);
    }


    // Create queues and observers
    for (auto& [_, queue]: fragment_to_refine_src) {
        NaiveQueueImpl<uint64_t>* producer_view = queue->view(1, false, 0, 0, 0, 0);
        NaiveQueueImpl<uint64_t>* consumer_view = queue->view(10000, false, 0, 0, 0, 0);

        QueueObs* prod_queue_obs = new QueueObs;
        prod_queue_obs->_queue = producer_view;
        prod_queue_obs->_observer = nullptr;

        QueueObs* cons_queue_obs = new QueueObs;
        cons_queue_obs->_queue = consumer_view;
        cons_queue_obs->_observer = nullptr;

        fragment_to_refine.push_back(prod_queue_obs);
        fragment_to_refine.push_back(cons_queue_obs);
    }

    for (auto& [obs, queue]: refine_to_deduplicate_src) {
        NaiveQueueImpl<uint64_t>* producer_view = queue->view(1, false, 0, 0, 100, 50);
        NaiveQueueImpl<uint64_t>* consumer_view = queue->view(1, false, 0, 0, 100, 50);

        QueueObs* prod_queue_obs = new QueueObs;
        prod_queue_obs->_queue = producer_view;
        prod_queue_obs->_observer = obs;

        QueueObs* cons_queue_obs = new QueueObs;
        cons_queue_obs->_queue = consumer_view;
        cons_queue_obs->_observer = obs;

        refine_to_deduplicate.push_back(prod_queue_obs);
        refine_to_deduplicate.push_back(cons_queue_obs);
    }

    for (auto& [obs, queue]: deduplicate_to_compress_src) {
        NaiveQueueImpl<uint64_t>* producer_view = queue->view(1, false, 0, 0, 100, 50);
        
        QueueObs* prod_queue_obs = new QueueObs;
        prod_queue_obs->_queue = producer_view;
        prod_queue_obs->_observer = obs;

        deduplicate_to_compress.push_back(prod_queue_obs);

        for (int i = 0; i < 4; ++i) {
            NaiveQueueImpl<uint64_t>* consumer_view = queue->view(1, false, 0, 0, 100, 50);

            QueueObs* cons_queue_obs = new QueueObs;
            cons_queue_obs->_queue = consumer_view;
            cons_queue_obs->_observer = obs;

            deduplicate_to_compress.push_back(cons_queue_obs);
        }
    }

    for (auto& [obs, queue]: deduplicate_compress_to_reorder_src) {
        NaiveQueueImpl<uint64_t>* consumer_view = queue->view(1, false, 0, 0, 100, 50);
        NaiveQueueImpl<uint64_t>* dedup_producer_view = queue->view(1, false, 0, 0, 100, 50);
        
        QueueObs* dedup_prod_queue_obs = new QueueObs;
        QueueObs* cons_queue_obs = new QueueObs;

        dedup_prod_queue_obs->_queue = dedup_producer_view;
        dedup_prod_queue_obs->_observer = obs;
            
        deduplicate_compress_to_reorder.push_back(dedup_prod_queue_obs);

        cons_queue_obs->_queue = consumer_view;
        cons_queue_obs->_observer = obs;

        for (int i = 0; i < 4; ++i) {
            NaiveQueueImpl<uint64_t>* compress_producer_view = queue->view(1, false, 0, 0, 100, 50);

            QueueObs* compress_prod_queue_obs = new QueueObs;
            compress_prod_queue_obs->_queue = compress_producer_view;
            compress_prod_queue_obs->_observer = obs;

            deduplicate_compress_to_reorder.push_back(compress_prod_queue_obs);
        }
        
        deduplicate_compress_to_reorder.push_back(cons_queue_obs);
    }

    // Populate observers 
    pthread_barrier_init(&barrier, nullptr, 1 + 2 + 2 + 8 + 1 + 1);
    
    std::set<Observer<uint64_t>*> observers;
    bool proceed = true;

    // 1) Refine to deduplicate
    for (int i = 0; i < 4; i += 2) {
        refine_to_deduplicate[i]->_observer->add_producer(refine_to_deduplicate[i]->_queue);
        refine_to_deduplicate[i]->_observer->add_consumer(refine_to_deduplicate[i + 1]->_queue);

        if (observers.find(refine_to_deduplicate[i]->_observer) != observers.end()) {
            std::cerr << "Duplicate observer found in refine to deduplicate" << std::endl;
            proceed = false;
        }

        observers.insert(refine_to_deduplicate[i]->_observer);
    }

    // 2) Deduplicate to compress
    for (int i = 0; i < 10; i += 5) {
        deduplicate_to_compress[i]->_observer->add_producer(deduplicate_to_compress[i]->_queue);
        for (int j = 0; j < 4; ++j) {
            deduplicate_to_compress[i]->_observer->add_consumer(deduplicate_to_compress[i + j + 1]->_queue);
        }

        if (observers.find(deduplicate_to_compress[i]->_observer) != observers.end()) {
            std::cerr << "Duplicate observer found in deduplicate to compress" << std::endl;
            proceed = false;
        }
        observers.insert(deduplicate_to_compress[i]->_observer);
    }

    // 3) Deduplicate and compress to reorder
    for (int i = 0; i < 12; i += 6) {
        deduplicate_compress_to_reorder[i]->_observer->add_producer(deduplicate_compress_to_reorder[i]->_queue);
        for (int j = 0; j < 4; ++j) {
            deduplicate_compress_to_reorder[i]->_observer->add_producer(deduplicate_compress_to_reorder[i + j + 1]->_queue);
        }
        deduplicate_compress_to_reorder[i]->_observer->add_consumer(deduplicate_compress_to_reorder[i + 5]->_queue);


        if (observers.find(deduplicate_compress_to_reorder[i]->_observer) != observers.end()) {
            std::cerr << "Deduplicate observer found in deduplicate and compress to reorder" << std::endl;
            proceed = false;
        }
        observers.insert(deduplicate_compress_to_reorder[i]->_observer);
    }

    if (!proceed) {
        std::cerr << "Aborting due to duplicate observer" << std::endl;
        exit(-1);
    }

    std::vector<Observer<uint64_t>*> diff;
    std::set_difference(created_observers.begin(), created_observers.end(), observers.begin(), observers.end(), std::back_inserter(diff));

    if (diff.size() != 0) {
        std::cerr << "Aborting because some observers were not initialized (" << diff.size() << ")" << std::endl;
        exit(-1);
    }

    for (Observer<uint64_t>* obs: observers) {
        obs->set_prod_size(100);
        obs->set_cons_size(100);
        obs->set_cost_p_cost_s_size(100);
        obs->set_cost_s_size(50);
    }

    // Launch threads
    
    // 1) Fragment
    threads.push_back(std::thread(Stage1, std::ref(fragment_to_refine), pipeline._n_coarse, pipeline._work[0]));

    // 2) Refine
    for (int i = 0; i < 2; ++i) {
        threads.push_back(std::thread(Stage2, std::ref(*(fragment_to_refine[2 * i + 1])), std::ref(*(refine_to_deduplicate[2 * i])), pipeline._n_coarse, pipeline._n_fine, pipeline._work[1]));
    }

    // 3) Deduplicate
    for (int i = 0; i < 2; ++i) {
        threads.push_back(std::thread(Stage3, std::ref(*(refine_to_deduplicate[2 * i + 1])), std::ref(*(deduplicate_to_compress[5 * i])), std::ref(*(deduplicate_compress_to_reorder[i * 6])), pipeline._work[2]));
    }

    // 4) Compress
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 4; ++j) {
            threads.push_back(std::thread(Stage4, std::ref(*(deduplicate_to_compress[5 * i + j + 1])), std::ref(*(deduplicate_compress_to_reorder[i * 6 + j + 1])), pipeline._work[3]));
        }
    }

    // 5) Reorder
    std::vector<QueueObs*> reorder_pull;
    reorder_pull.push_back(deduplicate_compress_to_reorder[5]);
    reorder_pull.push_back(deduplicate_compress_to_reorder[11]);

    threads.push_back(std::thread(Stage5, std::ref(reorder_pull), pipeline._work[4]));

    pthread_barrier_wait(&barrier);

    TP begin = SteadyClock::now();

    // End
    
    for (std::thread& thread: threads)
        thread.join();

    TP end = SteadyClock::now();

    auto deleter = [](std::vector<QueueObs*>& v) -> void {
        for (QueueObs* obs: v)
            delete obs;
    };
}

void init_lua(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::io, sol::lib::table, sol::lib::package, sol::lib::math);

    sol::usertype<Pipeline> pipeline_datatype = lua.new_usertype<Pipeline>("Pipeline", sol::constructors<Pipeline(uint64_t, uint64_t, float)>());
    pipeline_datatype["set_work"] = &Pipeline::set_work;
    pipeline_datatype["run"] = &Pipeline::run;
}

int main() {
    sol::state lua;
    init_lua(lua);
    lua.script_file("pipeline.lua");
    return 0;
}

void Pipeline::run() {
    launch(*this);
}
