#include "fode/executor.hpp"

#include <algorithm>
#include <stdexcept>

namespace fode {
namespace {

constexpr unsigned kActiveSpinCount = 4096;

void processor_pause() noexcept {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield");
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    std::this_thread::yield();
#endif
}

}  // namespace

PersistentExecutor::PersistentExecutor(int total_threads)
    : total_threads_(total_threads),
      region_participated_(
          std::make_unique<std::atomic<bool>[]>(
              static_cast<std::size_t>(std::max(total_threads, 1))
          )
      ),
      window_participated_(
          std::make_unique<std::atomic<bool>[]>(
              static_cast<std::size_t>(std::max(total_threads, 1))
          )
      ) {
    if (total_threads_ <= 0) {
        throw std::invalid_argument("persistent executor needs positive threads");
    }
    reset_work_receipt();
    workers_.reserve(static_cast<std::size_t>(total_threads_ - 1));
    for (int index = 1; index < total_threads_; ++index) {
        workers_.emplace_back(
            [this, index]() { worker_loop(index); }
        );
    }
    while (started_workers_.load(std::memory_order_acquire)
           != static_cast<int>(workers_.size())) {
        processor_pause();
    }
}

PersistentExecutor::~PersistentExecutor() {
    stopping_.store(true, std::memory_order_release);
    epoch_.fetch_add(1, std::memory_order_release);
    epoch_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void PersistentExecutor::record_participation(int thread_slot) noexcept {
    const std::size_t slot = static_cast<std::size_t>(thread_slot);
    region_participated_[slot].store(true, std::memory_order_relaxed);
    window_participated_[slot].store(true, std::memory_order_relaxed);
    window_participant_activations_.fetch_add(
        1,
        std::memory_order_relaxed
    );
}

void PersistentExecutor::consume_current_job(int thread_slot) {
    bool recorded = false;
    while (true) {
        const int index = next_.fetch_add(1, std::memory_order_relaxed);
        if (index >= end_) {
            return;
        }
        if (!recorded) {
            record_participation(thread_slot);
            recorded = true;
        }
        task_(index);
    }
}

void PersistentExecutor::finish_region_receipt() noexcept {
    int participants = 0;
    for (int thread = 0; thread < total_threads_; ++thread) {
        participants += region_participated_[
            static_cast<std::size_t>(thread)
        ].load(std::memory_order_relaxed) ? 1 : 0;
    }
    int current =
        window_peak_region_participants_.load(std::memory_order_relaxed);
    while (
        participants > current &&
        !window_peak_region_participants_.compare_exchange_weak(
            current,
            participants,
            std::memory_order_relaxed
        )
    ) {}
}

void PersistentExecutor::parallel_for(
    int begin,
    int end,
    const std::function<void(int)>& task
) {
    if (end <= begin) {
        return;
    }
    if (!task) {
        throw std::invalid_argument("parallel_for task is empty");
    }
    for (int thread = 0; thread < total_threads_; ++thread) {
        region_participated_[static_cast<std::size_t>(thread)].store(
            false,
            std::memory_order_relaxed
        );
    }
    window_parallel_regions_.fetch_add(1, std::memory_order_relaxed);
    window_task_items_.fetch_add(
        static_cast<std::uint64_t>(end - begin),
        std::memory_order_relaxed
    );
    if (total_threads_ == 1) {
        record_participation(0);
        for (int index = begin; index < end; ++index) {
            task(index);
        }
        finish_region_receipt();
        return;
    }
    task_ = task;
    next_.store(begin, std::memory_order_relaxed);
    end_ = end;
    completed_workers_.store(0, std::memory_order_relaxed);
    epoch_.fetch_add(1, std::memory_order_release);
    epoch_.notify_all();
    consume_current_job(0);
    while (completed_workers_.load(std::memory_order_acquire)
           != static_cast<int>(workers_.size())) {
        processor_pause();
    }
    finish_region_receipt();
    task_ = {};
}

void PersistentExecutor::reset_work_receipt() noexcept {
    window_parallel_regions_.store(0, std::memory_order_relaxed);
    window_task_items_.store(0, std::memory_order_relaxed);
    window_participant_activations_.store(0, std::memory_order_relaxed);
    window_peak_region_participants_.store(0, std::memory_order_relaxed);
    for (int thread = 0; thread < total_threads_; ++thread) {
        region_participated_[static_cast<std::size_t>(thread)].store(
            false,
            std::memory_order_relaxed
        );
        window_participated_[static_cast<std::size_t>(thread)].store(
            false,
            std::memory_order_relaxed
        );
    }
}

ExecutorWorkReceipt PersistentExecutor::work_receipt() const noexcept {
    ExecutorWorkReceipt receipt;
    receipt.configured_threads = total_threads_;
    receipt.parallel_regions =
        window_parallel_regions_.load(std::memory_order_relaxed);
    receipt.task_items =
        window_task_items_.load(std::memory_order_relaxed);
    receipt.participant_activations =
        window_participant_activations_.load(std::memory_order_relaxed);
    receipt.peak_region_participants =
        window_peak_region_participants_.load(std::memory_order_relaxed);
    for (int thread = 0; thread < total_threads_; ++thread) {
        receipt.distinct_participants += window_participated_[
            static_cast<std::size_t>(thread)
        ].load(std::memory_order_relaxed) ? 1 : 0;
    }
    return receipt;
}

void PersistentExecutor::worker_loop(int thread_slot) {
    std::uint64_t observed_epoch = epoch_.load(std::memory_order_acquire);
    started_workers_.fetch_add(1, std::memory_order_release);
    while (true) {
        unsigned spins = 0;
        std::uint64_t current_epoch =
            epoch_.load(std::memory_order_acquire);
        while (current_epoch == observed_epoch
               && !stopping_.load(std::memory_order_acquire)) {
            if (spins < kActiveSpinCount) {
                ++spins;
                processor_pause();
            } else {
                epoch_.wait(observed_epoch, std::memory_order_relaxed);
            }
            current_epoch = epoch_.load(std::memory_order_acquire);
        }
        if (stopping_.load(std::memory_order_acquire)) {
            return;
        }
        observed_epoch = current_epoch;
        consume_current_job(thread_slot);
        completed_workers_.fetch_add(1, std::memory_order_release);
    }
}

}  // namespace fode
