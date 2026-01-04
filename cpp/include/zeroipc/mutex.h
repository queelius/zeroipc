#pragma once

#include <string_view>
#include <chrono>
#include <stdexcept>
#include "memory.h"
#include "semaphore.h"

namespace zeroipc {

/**
 * @brief Mutex for mutual exclusion across processes
 *
 * Provides RAII-compatible mutual exclusion synchronization. Works across
 * processes sharing the same memory region. Compatible with std::lock_guard
 * and std::unique_lock.
 *
 * Internally implemented as a binary semaphore (initial count = 1).
 *
 * Example:
 * @code
 * zeroipc::Memory mem("/data", 1024 * 1024);
 * zeroipc::Mutex mtx(mem, "data_mutex");
 *
 * // Manual locking
 * mtx.lock();
 * // ...critical section...
 * mtx.unlock();
 *
 * // RAII locking
 * {
 *     std::lock_guard<zeroipc::Mutex> lock(mtx);
 *     // ...critical section...
 * }  // Automatically unlocked
 * @endcode
 */
class Mutex {
public:
    /**
     * @brief Create or open a Mutex
     * @param mem Memory region
     * @param name Unique name for this mutex
     * @throws std::runtime_error if allocation fails
     */
    Mutex(Memory& mem, std::string_view name) {
        auto entry = mem.table()->find(name);

        if (entry) {
            // Open existing semaphore
            sem_ = std::make_unique<Semaphore>(mem, name);
        } else {
            // Create new binary semaphore (initial_count=1, max_count=1 for mutex)
            sem_ = std::make_unique<Semaphore>(mem, name, 1, 1);
        }
    }

    /**
     * @brief Lock the mutex
     *
     * Blocks until the mutex is acquired. If another thread/process holds
     * the lock, this will wait until it's released.
     */
    void lock() {
        sem_->acquire();
    }

    /**
     * @brief Try to lock the mutex without blocking
     * @return true if lock acquired, false if already locked
     */
    [[nodiscard]] bool try_lock() {
        return sem_->try_acquire();
    }

    /**
     * @brief Try to lock with timeout
     * @param timeout Maximum time to wait
     * @return true if lock acquired, false if timeout
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout) {
        return sem_->acquire_for(timeout);
    }

    /**
     * @brief Unlock the mutex
     *
     * Must be called by the same thread/process that locked it.
     */
    void unlock() {
        sem_->release();
    }

    // Prevent copying
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    // Allow moving
    Mutex(Mutex&&) = default;
    Mutex& operator=(Mutex&&) = default;

private:
    std::unique_ptr<Semaphore> sem_;
};

} // namespace zeroipc
