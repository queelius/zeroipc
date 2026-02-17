#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include <stdexcept>
#include <thread>
#include "memory.h"

namespace zeroipc {

/**
 * @brief Once flag for one-time initialization across processes
 *
 * Binary layout:
 * - 4 bytes: atomic state (0 = PENDING, 1 = EXECUTING, 2 = DONE)
 */
struct OnceFlag {
    std::atomic<uint32_t> state{0};

    OnceFlag() : state(0) {}
};

static_assert(std::is_trivially_copyable_v<OnceFlag>,
              "OnceFlag must be trivially copyable");

/**
 * @brief One-time initialization primitive for shared memory
 *
 * Ensures a function is executed exactly once across all processes,
 * similar to std::call_once. Thread-safe and process-safe.
 *
 * Uses 3-state protocol to ensure callers wait for completion:
 * - PENDING (0): No one has started execution yet
 * - EXECUTING (1): A thread/process is currently running the function
 * - DONE (2): The function has completed execution
 *
 * Binary layout: 4 bytes (OnceFlag)
 *
 * Example:
 * @code
 * zeroipc::Memory mem("/config", 1024);
 * zeroipc::Once init(mem, "initialize");
 *
 * // This will execute exactly once across all processes
 * init.call([]() {
 *     std::cout << "Initializing shared resources...\n";
 *     // Expensive initialization here
 * });
 * @endcode
 */
class Once {
public:
    // State constants
    static constexpr uint32_t STATE_PENDING   = 0;
    static constexpr uint32_t STATE_EXECUTING = 1;
    static constexpr uint32_t STATE_DONE      = 2;

    /**
     * @brief Create or open a Once flag
     * @param mem Memory region
     * @param name Unique name for this once flag
     * @throws std::runtime_error if allocation fails
     */
    Once(Memory& mem, std::string_view name) {
        auto entry = mem.table()->find(name);

        if (entry) {
            // Open existing
            flag_ = reinterpret_cast<OnceFlag*>(
                static_cast<char*>(mem.data()) + entry->offset
            );
        } else {
            // Create new
            size_t offset = mem.allocate(name, sizeof(OnceFlag));

            flag_ = reinterpret_cast<OnceFlag*>(
                static_cast<char*>(mem.data()) + offset
            );

            // Initialize to PENDING state
            new (flag_) OnceFlag();
        }
    }

    /**
     * @brief Execute function exactly once
     *
     * If this is the first call, executes func. Otherwise, blocks until
     * the first call completes, then returns without executing func.
     *
     * @param func Callable to execute once
     *
     * Note: If func throws an exception, the once flag is still marked
     * as done (matching std::call_once behavior).
     */
    template<typename F>
    void call(F&& func) {
        // Fast path: already done
        if (flag_->state.load(std::memory_order_acquire) == STATE_DONE) {
            return;
        }

        // Try to transition from PENDING to EXECUTING
        uint32_t expected = STATE_PENDING;
        if (flag_->state.compare_exchange_strong(
                expected, STATE_EXECUTING,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {

            // We won the race - execute the function
            try {
                func();
            } catch (...) {
                // Even if func throws, mark as done (std::call_once semantics)
                flag_->state.store(STATE_DONE, std::memory_order_release);
                throw;
            }

            // Transition from EXECUTING to DONE
            flag_->state.store(STATE_DONE, std::memory_order_release);
        } else {
            // We lost the race - spin-wait until the executor finishes
            while (flag_->state.load(std::memory_order_acquire) == STATE_EXECUTING) {
                std::this_thread::yield();
            }
        }
    }

    /**
     * @brief Check if the once flag has been called
     * @return true if call() has completed execution, false otherwise
     */
    [[nodiscard]] bool already_called() const {
        return flag_->state.load(std::memory_order_acquire) == STATE_DONE;
    }

    /**
     * @brief Reset the once flag (testing/debugging only!)
     *
     * WARNING: Not thread-safe. Only use when you know no other
     * threads/processes are accessing this once flag.
     */
    void reset_unsafe() {
        flag_->state.store(STATE_PENDING, std::memory_order_release);
    }

private:
    OnceFlag* flag_;
};

} // namespace zeroipc
