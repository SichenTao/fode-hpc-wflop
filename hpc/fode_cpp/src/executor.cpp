#include "fode/executor.hpp"

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
    : total_threads_(total_threads) {
    if (total_threads_ <= 0) {
        throw std::invalid_argument("persistent executor needs positive threads");
    }
    workers_.reserve(static_cast<std::size_t>(total_threads_ - 1));
    for (int index = 1; index < total_threads_; ++index) {
        workers_.emplace_back([this]() { worker_loop(); });
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

void PersistentExecutor::consume_current_job() {
    while (true) {
        const int index = next_.fetch_add(1, std::memory_order_relaxed);
        if (index >= end_) {
            return;
        }
        task_(index);
    }
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
    if (total_threads_ == 1) {
        for (int index = begin; index < end; ++index) {
            task(index);
        }
        return;
    }
    task_ = task;
    next_.store(begin, std::memory_order_relaxed);
    end_ = end;
    completed_workers_.store(0, std::memory_order_relaxed);
    epoch_.fetch_add(1, std::memory_order_release);
    epoch_.notify_all();
    consume_current_job();
    while (completed_workers_.load(std::memory_order_acquire)
           != static_cast<int>(workers_.size())) {
        processor_pause();
    }
    task_ = {};
}

void PersistentExecutor::worker_loop() {
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
        consume_current_job();
        completed_workers_.fetch_add(1, std::memory_order_release);
    }
}

}  // namespace fode
