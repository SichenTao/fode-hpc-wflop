#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace fode {

// One optimization owns one executor.  The calling thread participates in
// every parallel_for and the remaining threads stay alive until destruction,
// so algorithm and evaluator stages do not repeatedly create thread teams.
class PersistentExecutor {
public:
    explicit PersistentExecutor(int total_threads);
    ~PersistentExecutor();

    PersistentExecutor(const PersistentExecutor&) = delete;
    PersistentExecutor& operator=(const PersistentExecutor&) = delete;

    [[nodiscard]] int thread_count() const noexcept {
        return total_threads_;
    }

    void parallel_for(int begin, int end, const std::function<void(int)>& task);

private:
    void worker_loop();
    void consume_current_job();

    int total_threads_ = 1;
    std::vector<std::thread> workers_;
    std::function<void(int)> task_;
    std::atomic<int> next_{0};
    int end_ = 0;
    std::atomic<int> completed_workers_{0};
    std::atomic<int> started_workers_{0};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<bool> stopping_{false};
};

}  // namespace fode
