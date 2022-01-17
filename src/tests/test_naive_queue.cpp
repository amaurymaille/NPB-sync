#include <iostream>

#include "naive_queue.hpp"

int main() {
    {
        NaiveQueueImpl<int> queue(nullptr, 10, false, 0, 0);
        for (int i = 0; i < 10; ++i) {
            queue.push_local(i);
            queue.dump();
            std::cout << "------------" << std::endl;
        }

        std::cout << queue.push_local(10) << std::endl;
        queue.dump();
        queue.pop_local();
        queue.push_local(10);
        queue.dump();
        queue.push_local(11);
        queue.dump();
        queue.pop_local();
        queue.dump();
        queue.push_local(11);
        queue.dump();
        for (int i = 0; i < 5; ++i) {
            queue.pop_local();
        }
        queue.dump();
        for (int i = 0; i < 5; ++i) {
            queue.push_local(i);
        }
        queue.dump();
        queue.pop_local();
        queue.pop_local();
        queue.dump();
        std::cout << queue.resize(15) << std::endl;
        queue.dump();
        queue.resize(10);
        queue.dump();
    }

    {
        NaiveQueueImpl<int> queue(nullptr, 10, false, 0, 0);
        for (int i = 0; i < 5; ++i) {
            queue.push_local(i);
        }

        queue.dump();
        std::cout << "Resize 6: " << queue.resize(6) << std::endl;
        queue.dump();
        queue.pop_local();
        queue.pop_local();
        queue.push_local(5);
        queue.dump();
        std::cout << "Resize 5: " << queue.resize(5) << std::endl;
        queue.dump();
    }
    return 0;
}
