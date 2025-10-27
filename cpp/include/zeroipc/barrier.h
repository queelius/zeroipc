#pragma once

#include "memory.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace zeroipc {

/**
 * Lock-free barrier for cross-process synchronization.
 *
 * A barrier synchronizes N processes at a checkpoint. All participants must
 * call wait() before any can proceed. Once all arrive, the barrier releases
 * all waiters simultaneously and automatically resets for the next cycle.
 *
 * The generation counter prevents early arrivals for the next cycle from
 * releasing the current cycle, making the barrier reusable.
 *
 * Thread-safe and process-safe.
 *
 * Example:
 *   // Phase-based parallel algorithm
 *   Barrier barrier(mem, "phase_sync", 4);  // 4 worker processes
 *
 *   while (work_remaining) {
 *       do_phase_1();
 *       barrier.wait();  // All workers must complete phase 1
 *       do_phase_2();
 *       barrier.wait();  // All workers must complete phase 2
 *   }
 */
class Barrier {
public:
    struct Header {
        std::atomic<int32_t> arrived;        // Number of processes that have arrived
        std::atomic<int32_t> generation;     // Generation counter (for reusability)
        int32_t num_participants;            // Total number of participants
        int32_t _padding;                    // Alignment padding
    };

    static_assert(sizeof(Header) == 16, "Header must be 16 bytes");

    /**
     * Create a new barrier.
     *
     * @param memory Memory instance
     * @param name Unique identifier for this barrier
     * @param num_participants Number of processes that must arrive before releasing
     */
    Barrier(Memory& memory, std::string_view name, int32_t num_participants)
        : memory_(memory), name_(name) {

        if (num_participants <= 0) {
            throw std::invalid_argument("Number of participants must be positive");
        }

        size_t offset = memory.allocate(name, sizeof(Header));

        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);

        // Initialize header
        header_->arrived.store(0, std::memory_order_relaxed);
        header_->generation.store(0, std::memory_order_relaxed);
        header_->num_participants = num_participants;
        header_->_padding = 0;
    }

    /**
     * Open an existing barrier.
     *
     * @param memory Memory instance
     * @param name Name of existing barrier
     */
    Barrier(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {

        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Barrier not found: " + std::string(name));
        }

        if (size != sizeof(Header)) {
            throw std::runtime_error("Invalid barrier size");
        }

        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
    }

    /**
     * Wait for all participants to arrive at the barrier.
     *
     * Blocks until all num_participants processes have called wait().
     * Once all arrive, all waiters are released simultaneously and the
     * barrier automatically resets for the next cycle.
     *
     * Uses spin-waiting with exponential backoff to reduce CPU usage.
     */
    void wait() {
        // Capture current generation before arriving
        int32_t my_generation = header_->generation.load(std::memory_order_acquire);

        // Atomically increment arrived counter
        int32_t arrived = header_->arrived.fetch_add(1, std::memory_order_acq_rel) + 1;

        if (arrived == header_->num_participants) {
            // Last to arrive - reset and release everyone
            header_->arrived.store(0, std::memory_order_relaxed);

            // Increment generation to release waiters
            // Use release ordering so other threads see the reset arrived count
            header_->generation.fetch_add(1, std::memory_order_release);
        } else {
            // Not last - wait for generation to change
            int backoff = 1;
            const int max_backoff = 1000; // Max 1ms

            while (header_->generation.load(std::memory_order_acquire) == my_generation) {
                // Spin-wait with exponential backoff
                std::this_thread::sleep_for(std::chrono::microseconds(backoff));
                if (backoff < max_backoff) {
                    backoff *= 2;
                }
            }
        }
    }

    /**
     * Wait for all participants with a timeout.
     *
     * @param timeout Maximum time to wait
     * @return true if barrier released, false if timed out
     *
     * WARNING: If a timeout occurs, the barrier state may be inconsistent.
     * The caller is responsible for coordinating recovery with other processes.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool wait_for(
            const std::chrono::duration<Rep, Period>& timeout) {

        auto start = std::chrono::steady_clock::now();

        // Capture current generation before arriving
        int32_t my_generation = header_->generation.load(std::memory_order_acquire);

        // Atomically increment arrived counter
        int32_t arrived = header_->arrived.fetch_add(1, std::memory_order_acq_rel) + 1;

        if (arrived == header_->num_participants) {
            // Last to arrive - reset and release everyone
            header_->arrived.store(0, std::memory_order_relaxed);
            header_->generation.fetch_add(1, std::memory_order_release);
            return true;
        } else {
            // Not last - wait for generation to change or timeout
            int backoff = 1;
            const int max_backoff = 1000;

            while (header_->generation.load(std::memory_order_acquire) == my_generation) {
                // Check timeout
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed >= timeout) {
                    // Timeout - decrement arrived count
                    // WARNING: This creates a race if the last participant arrives
                    // during this window. Use with caution.
                    header_->arrived.fetch_sub(1, std::memory_order_acq_rel);
                    return false;
                }

                // Backoff
                std::this_thread::sleep_for(std::chrono::microseconds(backoff));
                if (backoff < max_backoff) {
                    backoff *= 2;
                }
            }

            return true;
        }
    }

    /**
     * Get number of processes currently waiting at the barrier.
     *
     * @return Number of arrived processes
     */
    [[nodiscard]] int32_t arrived() const {
        return header_->arrived.load(std::memory_order_acquire);
    }

    /**
     * Get current generation number.
     *
     * The generation increments each time all participants pass through.
     *
     * @return Current generation
     */
    [[nodiscard]] int32_t generation() const {
        return header_->generation.load(std::memory_order_acquire);
    }

    /**
     * Get number of participants required.
     *
     * @return Number of participants
     */
    [[nodiscard]] int32_t num_participants() const {
        return header_->num_participants;
    }

    /**
     * Get barrier name.
     */
    [[nodiscard]] std::string_view name() const {
        return name_;
    }

private:
    Memory& memory_;
    std::string name_;
    Header* header_;
};

} // namespace zeroipc
