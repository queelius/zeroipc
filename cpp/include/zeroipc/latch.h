#pragma once

#include "memory.h"
#include "detail/spin_wait.h"
#include <atomic>
#include <chrono>
#include <stdexcept>

namespace zeroipc {

/**
 * Lock-free latch for one-time countdown synchronization.
 *
 * A latch counts down from an initial value to zero. Once it reaches zero,
 * it stays at zero (one-time use). Any number of processes can wait() for
 * the count to reach zero, and any process can count_down() to decrement it.
 *
 * Unlike Barrier which resets and cycles through generations, Latch is
 * single-use and cannot be reset.
 *
 * Thread-safe and process-safe.
 *
 * Common use cases:
 * - Start gate: Initialize with count=1, workers wait(), coordinator counts down
 * - Completion detection: Initialize with count=N, each worker counts down when done
 *
 * Example:
 *   // Wait for all workers to initialize
 *   Latch ready_latch(mem, "workers_ready", num_workers);
 *
 *   // Each worker thread:
 *   initialize();
 *   ready_latch.count_down();  // Signal ready
 *
 *   // Coordinator:
 *   ready_latch.wait();  // Wait for all workers to be ready
 *   start_work();
 */
class Latch {
public:
    struct Header {
        std::atomic<int32_t> count;      // Current count (counts down to zero)
        int32_t initial_count;           // Initial count value (immutable)
        int32_t _padding[2];             // Alignment padding
    };

    static_assert(sizeof(Header) == 16, "Header must be 16 bytes");

    /**
     * Create a new latch.
     *
     * @param memory Memory instance
     * @param name Unique identifier for this latch
     * @param count Initial count value (must be >= 0)
     */
    Latch(Memory& memory, std::string_view name, int32_t count)
        : memory_(memory), name_(name) {

        if (count < 0) {
            throw std::invalid_argument("Count must be non-negative");
        }

        size_t offset = memory.allocate(name, sizeof(Header));

        header_ = memory.ptr_at<Header>(offset);

        // Initialize header
        header_->count.store(count, std::memory_order_relaxed);
        header_->initial_count = count;
        header_->_padding[0] = 0;
        header_->_padding[1] = 0;
    }

    /**
     * Open an existing latch.
     *
     * @param memory Memory instance
     * @param name Name of existing latch
     */
    Latch(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {

        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Latch not found: " + std::string(name));
        }

        if (size != sizeof(Header)) {
            throw std::runtime_error("Invalid latch size");
        }

        header_ = memory.ptr_at<Header>(offset);
    }

    /**
     * Decrement the count by n (default 1).
     *
     * Atomically decrements the count, saturating at 0. If the count
     * reaches 0, all waiting processes are released.
     *
     * @param n Amount to decrement (default 1, must be > 0)
     */
    void count_down(int32_t n = 1) {
        if (n <= 0) {
            throw std::invalid_argument("count_down amount must be positive");
        }

        int32_t current = header_->count.load(std::memory_order_acquire);

        while (current > 0) {
            int32_t new_count = (current >= n) ? (current - n) : 0;

            if (header_->count.compare_exchange_weak(
                    current, new_count,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                return;
            }
            // CAS failed, current was updated, retry
        }
    }

    /**
     * Wait for the count to reach zero.
     *
     * Blocks until the latch count reaches 0. If the count is already 0,
     * returns immediately.
     *
     * Uses spin-waiting with exponential backoff to reduce CPU usage.
     */
    void wait() {
        detail::spin_wait([this] {
            return header_->count.load(std::memory_order_acquire) <= 0;
        });
    }

    /**
     * Try to wait without blocking.
     *
     * @return true if count is 0, false if still counting down
     */
    [[nodiscard]] bool try_wait() const {
        return header_->count.load(std::memory_order_acquire) == 0;
    }

    /**
     * Wait for the count to reach zero with a timeout.
     *
     * @param timeout Maximum time to wait
     * @return true if count reached 0, false if timed out
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool wait_for(
            const std::chrono::duration<Rep, Period>& timeout) {

        return detail::spin_wait_for([this] {
            return header_->count.load(std::memory_order_acquire) <= 0;
        }, timeout);
    }

    /**
     * Get current count value.
     *
     * @return Current count (>= 0)
     */
    [[nodiscard]] int32_t count() const {
        return header_->count.load(std::memory_order_acquire);
    }

    /**
     * Get initial count value.
     *
     * @return Initial count that the latch was created with
     */
    [[nodiscard]] int32_t initial_count() const {
        return header_->initial_count;
    }

    /**
     * Get latch name.
     */
    [[nodiscard]] std::string_view name() const {
        return name_;
    }

    /**
     * Reset the latch to its initial count.
     *
     * WARNING: This is NOT thread-safe if other threads are waiting or
     * counting down. Only use when you have exclusive access or when
     * all other threads have completed their wait().
     *
     * This allows the latch to be reused for multiple synchronization phases.
     */
    void reset() {
        header_->count.store(header_->initial_count, std::memory_order_release);
    }

private:
    Memory& memory_;
    std::string name_;
    Header* header_;
};

} // namespace zeroipc
