#include <iostream>

#include "fifo_plus.h"

void* f(void* arg) {
    printf("Calling f with arg = %p\n", arg);
    FIFOPlus<int>* fifo = (FIFOPlus<int>*)arg;
    fifo->set_n(12);
    
    if (fifo->get_n() != 12) {
        throw std::runtime_error("get_n() should return 12\n");
    } else {
        std::cout << "f OK\n" << std::endl;
    }

    return nullptr;
}

int main() {
    PThreadThreadIdentifier* identifier = new PThreadThreadIdentifier();
    FIFOPlus<int> fifo(FIFOPlusPopPolicy::POP_NO_WAIT, identifier, 2);
    identifier->register_thread();
    fifo.set_n(10);
    pthread_t thread;
    identifier->pthread_create(&thread, NULL, f, &fifo);
    std::vector<int> v;

    if (fifo.get_n() != 10) {
        throw std::runtime_error("get_n() should return 10\n");
    } else {
        std::cout << "Main OK\n";
    }

    fifo.push(12);
    fifo.pop(v, 10);
    fifo.empty(v);
    pthread_join(thread, nullptr);
    return 0;
}
