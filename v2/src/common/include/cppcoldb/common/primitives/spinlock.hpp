#pragma once
#include <atomic>

namespace cppcoldb::common {

// Minimal test-and-test-and-set spinlock; satisfies the Lockable named requirement.
class Spinlock {
public:
    void lock() {
        while (flag_.exchange(true, std::memory_order_acquire)) {
            while (flag_.load(std::memory_order_relaxed)) {
            }
        }
    }
    bool try_lock() { return !flag_.exchange(true, std::memory_order_acquire); }
    void unlock() { flag_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> flag_{false};
};

} // namespace cppcoldb::common
