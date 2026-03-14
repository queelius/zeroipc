#pragma once

#include <string_view>
#include <chrono>
#include <functional>
#include <stdexcept>
#include "memory.h"
#include "mutex.h"
#include "semaphore.h"

namespace zeroipc {

/**
 * @brief Monitor for condition variable synchronization across processes
 *
 * A Monitor combines a mutex with condition variable semantics, allowing
 * threads/processes to wait for conditions to become true. Similar to
 * std::condition_variable but works across shared memory.
 *
 * Provides:
 * - Mutual exclusion via embedded mutex
 * - wait() to atomically release lock and block until signaled
 * - notify_one() to wake one waiter
 * - notify_all() to wake all waiters
 * - Predicate-based waiting (handles spurious wakeups)
 *
 * Binary layout:
 * - Mutex (via Semaphore)
 * - Semaphore for condition signaling
 * - Atomic counter for waiting threads
 *
 * Example:
 * @code
 * zeroipc::Memory mem("/sync", 1024 * 1024);
 * zeroipc::Monitor mon(mem, "producer_consumer");
 * zeroipc::Array<int> buffer(mem, "buffer", 100);
 * zeroipc::Array<int> count(mem, "count", 1);
 *
 * // Producer
 * mon.lock();
 * mon.wait([&]() { return count[0] < 100; });  // Wait for space
 * buffer[count[0]++] = item;
 * mon.notify_one();
 * mon.unlock();
 *
 * // Consumer
 * mon.lock();
 * mon.wait([&]() { return count[0] > 0; });  // Wait for data
 * int item = buffer[--count[0]];
 * mon.notify_one();
 * mon.unlock();
 * @endcode
 */
class Monitor {
public:
    /**
     * @brief Create or open a Monitor
     * @param mem Memory region
     * @param name Unique name for this monitor
     * @throws std::runtime_error if allocation fails
     */
    Monitor(Memory& mem, std::string_view name) : mem_(mem) {
        std::string mutex_name = std::string(name) + "_mtx";
        std::string cond_name = std::string(name) + "_cond";
        std::string counter_name = std::string(name) + "_count";

        auto entry = mem.table()->find(name);

        if (entry) {
            // Open existing
            mutex_ = std::make_unique<Mutex>(mem, mutex_name);
            cond_sem_ = std::make_unique<Semaphore>(mem, cond_name);

            auto counter_entry = mem.table()->find(counter_name);
            if (!counter_entry) {
                throw std::runtime_error("Monitor counter not found");
            }
            waiting_count_ = mem.ptr_at<std::atomic<uint32_t>>(counter_entry->offset);
        } else {
            // Create new
            mutex_ = std::make_unique<Mutex>(mem, mutex_name);
            cond_sem_ = std::make_unique<Semaphore>(mem, cond_name, 0);  // Initially blocked

            // Create waiting counter
            size_t counter_offset = mem.allocate(counter_name, sizeof(std::atomic<uint32_t>));
            waiting_count_ = mem.ptr_at<std::atomic<uint32_t>>(counter_offset);
            new (waiting_count_) std::atomic<uint32_t>(0);

            // Add monitor entry to table (just a marker)
            size_t marker_offset = mem.allocate(name, 1);
            (void)marker_offset;
        }
    }

    /**
     * @brief Lock the monitor's mutex
     *
     * Must be called before wait() or accessing shared data.
     */
    void lock() {
        mutex_->lock();
    }

    /**
     * @brief Unlock the monitor's mutex
     */
    void unlock() {
        mutex_->unlock();
    }

    /**
     * @brief Try to lock without blocking
     * @return true if lock acquired, false otherwise
     */
    [[nodiscard]] bool try_lock() {
        return mutex_->try_lock();
    }

    /**
     * @brief Wait for notification
     *
     * Atomically releases the lock and blocks until notify_one() or
     * notify_all() is called. When woken, reacquires the lock.
     *
     * WARNING: Must hold the lock before calling wait().
     * WARNING: Spurious wakeups can occur - use predicate version for safety.
     */
    void wait() {
        waiting_count_->fetch_add(1, std::memory_order_release);

        // Release lock and wait
        mutex_->unlock();
        cond_sem_->acquire();

        // Reacquire lock
        mutex_->lock();

        // Decrement AFTER reacquiring lock to avoid race with notify
        waiting_count_->fetch_sub(1, std::memory_order_release);
    }

    /**
     * @brief Wait with predicate (handles spurious wakeups)
     *
     * Repeatedly waits until the predicate returns true. This is the
     * recommended way to use wait() as it handles spurious wakeups.
     *
     * @param pred Callable that returns bool
     *
     * Example:
     * @code
     * mon.lock();
     * mon.wait([&]() { return buffer_count > 0; });
     * // Now buffer_count is guaranteed > 0
     * mon.unlock();
     * @endcode
     */
    template<typename Predicate>
    void wait(Predicate pred) {
        while (!pred()) {
            wait();
        }
    }

    /**
     * @brief Wait with timeout and predicate
     *
     * @param timeout Maximum time to wait
     * @param pred Callable that returns bool
     * @return true if predicate became true, false if timeout
     */
    template<typename Rep, typename Period, typename Predicate>
    [[nodiscard]] bool wait_for(
            const std::chrono::duration<Rep, Period>& timeout,
            Predicate pred) {

        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!pred()) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;  // Timeout
            }

            auto remaining = deadline - now;
            if (!wait_for_impl(remaining)) {
                // Timeout, but check predicate one last time
                return pred();
            }
        }

        return true;
    }

    /**
     * @brief Wake one waiting thread/process
     *
     * If multiple threads are waiting, wakes one arbitrarily.
     * If no threads are waiting, the signal is ignored (not queued).
     * This prevents permit accumulation that causes CPU spinning.
     */
    void notify_one() {
        // Only release if someone is actually waiting on the semaphore
        // This prevents permit accumulation while the predicate loop handles spurious wakeups
        if (cond_sem_->waiting() > 0 || waiting_count_->load(std::memory_order_acquire) > 0) {
            cond_sem_->release();
        }
    }

    /**
     * @brief Wake all waiting threads/processes
     *
     * Wakes all currently waiting threads. New waiters are not affected.
     */
    void notify_all() {
        uint32_t count = waiting_count_->load(std::memory_order_acquire);
        for (uint32_t i = 0; i < count; i++) {
            cond_sem_->release();
        }
    }

private:
    template<typename Rep, typename Period>
    bool wait_for_impl(const std::chrono::duration<Rep, Period>& timeout) {
        waiting_count_->fetch_add(1, std::memory_order_release);

        // Release lock and wait with timeout
        mutex_->unlock();

        auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        bool acquired = cond_sem_->acquire_for(timeout_ms);

        // Reacquire lock
        mutex_->lock();

        // Decrement AFTER reacquiring lock
        waiting_count_->fetch_sub(1, std::memory_order_release);

        return acquired;
    }

    Memory& mem_;
    std::unique_ptr<Mutex> mutex_;
    std::unique_ptr<Semaphore> cond_sem_;
    std::atomic<uint32_t>* waiting_count_;
};

} // namespace zeroipc
