#pragma once

#include "memory.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace zeroipc {

/**
 * Lock-free semaphore for cross-process synchronization.
 *
 * A semaphore maintains a non-negative integer count representing available
 * resources or permits. Processes can acquire() to decrement the count and
 * release() to increment it. If count is zero, acquire() blocks until
 * another process calls release().
 *
 * Supports three modes:
 * - Binary semaphore (max_count=1): Acts as a mutex
 * - Counting semaphore (max_count=N): Resource pool with N permits
 * - Unbounded semaphore (max_count=0): No upper limit on count
 *
 * Thread-safe and process-safe.
 */
class Semaphore {
public:
    struct Header {
        std::atomic<int32_t> count;      // Current count
        std::atomic<int32_t> waiting;    // Number of waiting processes
        int32_t max_count;               // Maximum count (0 = unbounded)
        int32_t _padding;                // Alignment padding
    };

    static_assert(sizeof(Header) == 16, "Header must be 16 bytes");

    /**
     * Create a new semaphore.
     *
     * @param memory Memory instance
     * @param name Unique identifier for this semaphore
     * @param initial_count Initial value for the semaphore count
     * @param max_count Maximum count (0 for unbounded, 1 for binary/mutex)
     */
    Semaphore(Memory& memory, std::string_view name,
              int32_t initial_count, int32_t max_count = 0)
        : memory_(memory), name_(name) {

        if (initial_count < 0) {
            throw std::invalid_argument("Initial count must be non-negative");
        }

        if (max_count < 0) {
            throw std::invalid_argument("Max count must be non-negative or 0 (unbounded)");
        }

        if (max_count > 0 && initial_count > max_count) {
            throw std::invalid_argument("Initial count cannot exceed max count");
        }

        size_t offset = memory.allocate(name, sizeof(Header));

        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);

        // Initialize header
        header_->count.store(initial_count, std::memory_order_relaxed);
        header_->waiting.store(0, std::memory_order_relaxed);
        header_->max_count = max_count;
        header_->_padding = 0;
    }

    /**
     * Open an existing semaphore.
     *
     * @param memory Memory instance
     * @param name Name of existing semaphore
     */
    Semaphore(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {

        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Semaphore not found: " + std::string(name));
        }

        if (size != sizeof(Header)) {
            throw std::runtime_error("Invalid semaphore size");
        }

        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
    }

    /**
     * Acquire one permit from the semaphore.
     * Blocks until a permit is available.
     *
     * Uses spin-waiting with exponential backoff to reduce CPU usage.
     */
    void acquire() {
        header_->waiting.fetch_add(1, std::memory_order_relaxed);

        int backoff = 1;
        const int max_backoff = 1000; // Max 1ms

        while (true) {
            int32_t current = header_->count.load(std::memory_order_acquire);

            if (current > 0) {
                // Try to decrement
                if (header_->count.compare_exchange_weak(
                        current, current - 1,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                    // Success!
                    header_->waiting.fetch_sub(1, std::memory_order_relaxed);
                    return;
                }
            }

            // Exponential backoff to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::microseconds(backoff));
            if (backoff < max_backoff) {
                backoff *= 2;
            }
        }
    }

    /**
     * Try to acquire one permit without blocking.
     *
     * @return true if permit was acquired, false if count was 0
     */
    [[nodiscard]] bool try_acquire() {
        int32_t current = header_->count.load(std::memory_order_acquire);

        while (current > 0) {
            if (header_->count.compare_exchange_weak(
                    current, current - 1,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                return true;
            }
            // CAS failed, current was updated, retry
        }

        return false;
    }

    /**
     * Try to acquire one permit with a timeout.
     *
     * @param timeout Maximum time to wait
     * @return true if permit was acquired, false if timed out
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool acquire_for(
            const std::chrono::duration<Rep, Period>& timeout) {

        auto start = std::chrono::steady_clock::now();
        header_->waiting.fetch_add(1, std::memory_order_relaxed);

        int backoff = 1;
        const int max_backoff = 1000;

        while (true) {
            int32_t current = header_->count.load(std::memory_order_acquire);

            if (current > 0) {
                if (header_->count.compare_exchange_weak(
                        current, current - 1,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                    header_->waiting.fetch_sub(1, std::memory_order_relaxed);
                    return true;
                }
            }

            // Check timeout
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout) {
                header_->waiting.fetch_sub(1, std::memory_order_relaxed);
                return false;
            }

            // Backoff
            std::this_thread::sleep_for(std::chrono::microseconds(backoff));
            if (backoff < max_backoff) {
                backoff *= 2;
            }
        }
    }

    /**
     * Release one permit back to the semaphore.
     * Increments the count, potentially waking a waiting process.
     *
     * @throws std::overflow_error if max_count would be exceeded
     */
    void release() {
        int32_t current = header_->count.load(std::memory_order_relaxed);
        int32_t max = header_->max_count;

        // Check if we would exceed max_count
        if (max > 0 && current >= max) {
            throw std::overflow_error("Semaphore count would exceed maximum");
        }

        header_->count.fetch_add(1, std::memory_order_release);
        // Waiting processes will see the incremented count and wake up
    }

    /**
     * Get current semaphore count.
     *
     * @return Current count value
     */
    [[nodiscard]] int32_t count() const {
        return header_->count.load(std::memory_order_acquire);
    }

    /**
     * Get number of processes currently waiting.
     *
     * @return Number of waiting processes
     */
    [[nodiscard]] int32_t waiting() const {
        return header_->waiting.load(std::memory_order_acquire);
    }

    /**
     * Get maximum count (0 = unbounded).
     *
     * @return Maximum count
     */
    [[nodiscard]] int32_t max_count() const {
        return header_->max_count;
    }

    /**
     * Get semaphore name.
     */
    [[nodiscard]] std::string_view name() const {
        return name_;
    }

private:
    Memory& memory_;
    std::string name_;
    Header* header_;
};

/**
 * RAII guard for automatic semaphore acquire/release.
 *
 * Acquires the semaphore on construction and releases on destruction.
 *
 * Example:
 *   Semaphore sem(mem, "mutex", 1, 1);
 *   {
 *       SemaphoreGuard guard(sem);
 *       // Critical section - automatically released on scope exit
 *   }
 */
class SemaphoreGuard {
public:
    explicit SemaphoreGuard(Semaphore& sem) : sem_(sem), acquired_(true) {
        sem_.acquire();
    }

    ~SemaphoreGuard() {
        if (acquired_) {
            sem_.release();
        }
    }

    // Non-copyable
    SemaphoreGuard(const SemaphoreGuard&) = delete;
    SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;

    // Movable
    SemaphoreGuard(SemaphoreGuard&& other) noexcept
        : sem_(other.sem_), acquired_(other.acquired_) {
        other.acquired_ = false;
    }

    // Move assignment not supported (due to reference member)
    SemaphoreGuard& operator=(SemaphoreGuard&&) = delete;

private:
    Semaphore& sem_;
    bool acquired_;
};

} // namespace zeroipc
