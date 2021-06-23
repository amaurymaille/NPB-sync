//#include <stdio.h>
// #include <stddef.h>

#include <iostream>
#include <sstream>

#include <unistd.h>

#include "test_fifo_plus.h"

extern "C" {

void* producer(void* arg) {
    FIFOPlus<int>* fifo = Basic::init_get_fifo<int>(arg);

    for (int i = 0; i < 99; ++i) {
        fifo->push(i, false);
        usleep(10000);
    }

    fifo->push_immediate(99);
}

void* consumer(void* arg) {
    FIFOPlus<int>* fifo = Basic::init_get_fifo<int>(arg);
    
    for (int i = 0; i < 100; ) {
        std::optional<int> value;
        fifo->pop(value, false);

        if (fifo->get_pop_policy() == FIFOPlusPopPolicy::POP_WAIT) {
            if (!value) {
                throw std::runtime_error("Wait policy cannot not produce a value!");
            }
        }

        if (!value) {
            std::cout << "No value available yet\n";
            continue;
        }

        std::cout << "Got " << *value << std::endl;

        if (value && *value != i) {
            std::ostringstream stream;
            stream << "Order was not preserved, expecting " << i << ", got " << *value << std::endl;
            throw std::runtime_error(stream.str());
        }

        ++i;
    }
}

}
