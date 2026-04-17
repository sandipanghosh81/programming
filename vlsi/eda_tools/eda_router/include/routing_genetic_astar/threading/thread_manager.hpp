#pragma once
#include <thread>
#include <vector>
#include <functional>

namespace routing_genetic_astar {

class ThreadManager {
public:
    // Constructs the physical C++23 std::jthread worker queue
    void dispatch(int num_threads, std::function<void(int)> worker) {
        std::vector<std::jthread> pool;
        for (int i = 0; i < num_threads; ++i) {
            pool.emplace_back(std::jthread(worker, i));
        }
    }
};

} // namespace routing_genetic_astar
