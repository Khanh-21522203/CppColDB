#include "parallel/task_scheduler.hpp"
#include "common/exception.hpp"
#include <stdexcept>
#include <iostream>

namespace cppcoldb {

TaskScheduler::TaskScheduler(size_t num_threads)
    : num_threads_(num_threads) {}

TaskScheduler::~TaskScheduler() {
    if (!IsShutdown()) Shutdown();
}

void TaskScheduler::Initialize() {
    if (initialized_) return;
    initialized_ = true;
    workers_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

void TaskScheduler::Submit(std::unique_ptr<Task> task) {
    if (IsShutdown()) {
        throw RuntimeError("TaskScheduler::Submit called after Shutdown()");
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void TaskScheduler::Shutdown() {
    shutdown_.store(true, std::memory_order_release);
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

void TaskScheduler::WorkerLoop() {
    while (true) {
        std::unique_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] {
                return shutdown_.load(std::memory_order_acquire) || !queue_.empty();
            });
            if (shutdown_.load(std::memory_order_acquire) && queue_.empty()) return;
            if (queue_.empty()) continue;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        try {
            task->Execute();
        } catch (const std::exception& e) {
            std::cerr << "[TaskScheduler] task threw: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[TaskScheduler] task threw unknown exception\n";
        }
    }
}

} // namespace cppcoldb
