#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace fode {

struct ExecutorWorkReceipt {
    int configured_threads = 1;
    std::uint64_t parallel_regions = 0;
    std::uint64_t task_items = 0;
    std::uint64_t participant_activations = 0;
    int distinct_participants = 0;
    int peak_region_participants = 0;
};

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
    void reset_work_receipt() noexcept;
    [[nodiscard]] ExecutorWorkReceipt work_receipt() const noexcept;

private:
    void worker_loop(int thread_slot);
    void consume_current_job(int thread_slot);
    void record_participation(int thread_slot) noexcept;
    void finish_region_receipt() noexcept;

    int total_threads_ = 1;
    std::vector<std::thread> workers_;
    std::function<void(int)> task_;
    std::atomic<int> next_{0};
    int end_ = 0;
    std::atomic<int> completed_workers_{0};
    std::atomic<int> started_workers_{0};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<bool> stopping_{false};
    std::unique_ptr<std::atomic<bool>[]> region_participated_;
    std::unique_ptr<std::atomic<bool>[]> window_participated_;
    std::atomic<std::uint64_t> window_parallel_regions_{0};
    std::atomic<std::uint64_t> window_task_items_{0};
    std::atomic<std::uint64_t> window_participant_activations_{0};
    std::atomic<int> window_peak_region_participants_{0};
};

}  // namespace fode
