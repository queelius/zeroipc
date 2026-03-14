#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include <chrono>
#include <stdexcept>
#include "memory.h"
#include "semaphore.h"
#include "detail/spin_wait.h"

namespace zeroipc {

/**
 * @brief Event synchronization mode
 */
enum class EventMode : uint32_t {
    AutoReset = 0,   ///< Signal wakes one waiter, auto-resets
    ManualReset = 1  ///< Signal wakes all waiters, stays signaled
};

/**
 * @brief Event state structure for shared memory
 *
 * Binary layout:
 * - 4 bytes: signaled flag (0 = not signaled, 1 = signaled)
 * - 4 bytes: mode (AutoReset or ManualReset)
 * - 4 bytes: waiting count
 * - Variable: Semaphore (for blocking)
 */
struct EventState {
    std::atomic<uint32_t> signaled{0};
    uint32_t mode;
    std::atomic<uint32_t> waiting{0};

    EventState(EventMode m) : signaled(0), mode(static_cast<uint32_t>(m)), waiting(0) {}
};

static_assert(std::is_trivially_copyable_v<EventState>,
              "EventState must be trivially copyable");

/**
 * @brief Event synchronization primitive for shared memory
 *
 * Provides manual-reset and auto-reset event semantics similar to
 * Win32 Events or POSIX condition variables.
 *
 * - **AutoReset**: signal() wakes one waiter, then auto-resets
 * - **ManualReset**: signal() wakes all waiters, stays signaled until reset()
 *
 * Example:
 * @code
 * zeroipc::Memory mem("/sync", 1024 * 1024);
 * zeroipc::Event ready(mem, "ready", zeroipc::EventMode::ManualReset);
 *
 * // Process A - waits for ready signal
 * ready.wait();
 * process_data();
 *
 * // Process B - signals when ready
 * prepare_data();
 * ready.signal();
 * @endcode
 */
class Event {
public:
    /**
     * @brief Create or open an Event
     * @param mem Memory region
     * @param name Unique name for this event
     * @param mode Event mode (only used when creating new event)
     * @throws std::runtime_error if allocation fails
     */
    Event(Memory& mem, std::string_view name, EventMode mode = EventMode::AutoReset) {
        auto entry = mem.table()->find(name);

        if (entry) {
            // Open existing
            state_ = mem.ptr_at<EventState>(entry->offset);

            // Find the semaphore
            std::string sem_name = std::string(name) + "_sem";
            sem_ = std::make_unique<Semaphore>(mem, sem_name);
        } else {
            // Create new
            size_t total_size = sizeof(EventState);
            size_t offset = mem.allocate(name, total_size);

            state_ = mem.ptr_at<EventState>(offset);

            // Initialize state
            new (state_) EventState(mode);

            // Create semaphore for blocking (initially locked)
            std::string sem_name = std::string(name) + "_sem";
            sem_ = std::make_unique<Semaphore>(mem, sem_name, 0);
        }
    }

    /**
     * @brief Signal the event
     *
     * - AutoReset: Wakes one waiting thread (semaphore acts as the event)
     * - ManualReset: Wakes all waiting threads, stays signaled
     */
    void signal() {
        state_->signaled.store(1, std::memory_order_release);

        if (!is_manual_reset()) {
            // AutoReset: also release semaphore to wake one waiter
            sem_->release();
        }
    }

    /**
     * @brief Wait for the event to be signaled
     *
     * Blocks until signal() is called. For AutoReset events, consuming
     * the signal is automatic (semaphore). For ManualReset events, the
     * event stays signaled until reset() is called.
     */
    void wait() {
        if (is_manual_reset()) {
            state_->waiting.fetch_add(1, std::memory_order_relaxed);
            detail::spin_wait([this] {
                return state_->signaled.load(std::memory_order_acquire) != 0;
            });
            state_->waiting.fetch_sub(1, std::memory_order_release);
        } else {
            sem_->acquire();
            state_->signaled.store(0, std::memory_order_release);
        }
    }

    /**
     * @brief Wait with timeout
     * @param timeout Maximum time to wait
     * @return true if signaled, false if timeout
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool wait_for(const std::chrono::duration<Rep, Period>& timeout) {
        if (is_manual_reset()) {
            state_->waiting.fetch_add(1, std::memory_order_relaxed);
            bool signaled = detail::spin_wait_for([this] {
                return state_->signaled.load(std::memory_order_acquire) != 0;
            }, timeout);
            state_->waiting.fetch_sub(1, std::memory_order_release);
            return signaled;
        } else {
            if (sem_->acquire_for(timeout)) {
                state_->signaled.store(0, std::memory_order_release);
                return true;
            }
            return false;
        }
    }

    /**
     * @brief Reset the event to non-signaled state
     *
     * Only meaningful for ManualReset events. AutoReset events
     * reset automatically.
     */
    void reset() {
        state_->signaled.store(0, std::memory_order_release);
    }

    /**
     * @brief Pulse the event (signal then reset after all waiters wake)
     *
     * Wakes all waiting threads then resets once they've all seen the signal.
     * Only meaningful for ManualReset events.
     */
    void pulse() {
        signal();
        // Wait for all registered waiters to see the signal and unregister
        detail::spin_wait([this] {
            return state_->waiting.load(std::memory_order_acquire) == 0;
        });
        reset();
    }

    /**
     * @brief Check if event is currently signaled
     * @return true if signaled, false otherwise
     */
    [[nodiscard]] bool is_signaled() const {
        return state_->signaled.load(std::memory_order_acquire) == 1;
    }

private:
    EventState* state_;
    std::unique_ptr<Semaphore> sem_;

    bool is_manual_reset() const {
        return state_->mode == static_cast<uint32_t>(EventMode::ManualReset);
    }
};

} // namespace zeroipc
