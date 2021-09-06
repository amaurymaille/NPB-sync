#include <cstdio>

#include <iostream>
#include <thread>
#include <utility>

#include "smart_fifo.h"

void producer(SmartFIFO<int>& fifo, int start, int end) {
    for (int i = start; i < end; ++i) {
        fifo.push(i);
    }
    fifo.terminate_producer();
}

void consumer(SmartFIFO<int>& fifo, int id) {
    // std::cout << "Consumer " << id << " start" << std::endl;
    printf("Consumer %d start\n", id);
    std::vector<int> values;
    while (true) {
        std::optional<int> value;
        fifo.pop_copy(value);
        if (!value) {
            break;
        }

        printf("Consumer %d read %d\n", id, *value);
        values.push_back(*value);
    }
    printf("Consumer %d done\n", id);
}

int main() {
    SmartFIFOImpl<int> fifo(20);
    std::vector<std::thread> threads;
    std::vector<SmartFIFO<int>*> prods;
    std::vector<SmartFIFO<int>*> cons;
    int nb_threads = 1;
    for (int i = 0; i < nb_threads; ++i) {
        fifo.add_producer();
        prods.push_back(new SmartFIFO<int>(&fifo, 1));
        cons.push_back(new SmartFIFO<int>(&fifo, 25));
    }

    for (int i = 0; i < nb_threads; ++i) {
        threads.push_back(std::thread(producer, std::ref(*(prods[i])), i * (100000 / nb_threads), (i + 1) * (100000 / nb_threads)));
        threads.push_back(std::thread(consumer, std::ref(*(cons[i])), i));
    }

    for (std::thread& t: threads) {
        t.join();
    }

    for (SmartFIFO<int>* fifo: prods) {
        delete fifo;
    }

    for (SmartFIFO<int>* fifo: cons) {
        delete fifo;
    }

    fifo.dump();
    return 0;
}
