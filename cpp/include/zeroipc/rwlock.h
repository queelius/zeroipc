#pragma once

#include <atomic>
#include <string_view>
#include <stdexcept>
#include "memory.h"
#include "mutex.h"

namespace zeroipc {

/**
 * @brief Read-Write Lock state for shared memory
 *
 * Binary layout:
 * - 4 bytes: active readers count
 * - 4 bytes: writer waiting flag
 * - Mutex for reader coordination
 * - Mutex for writer exclusion
 */
struct RWLockState {
    std::atomic<int32_t> readers{0};      // Number of active readers
    std::atomic<int32_t> writer_active{0}; // 1 if writer active, 0 otherwise

    RWLockState() : readers(0), writer_active(0) {}
};

static_assert(std::is_trivially_copyable_v<RWLockState>,
              "RWLockState must be trivially copyable");

/**
 * @brief Read-Write Lock for shared memory
 *
 * Allows multiple concurrent readers OR one exclusive writer.
 * Optimized for read-heavy workloads where reads vastly outnumber writes.
 *
 * Features:
 * - Multiple readers can hold lock simultaneously
 * - Writer gets exclusive access (no readers, no other writers)
 * - RAII-compatible with SharedLock and UniqueLock
 *
 * Performance characteristics:
 * - reader_lock(): Fast when no writers (just atomic increment)
 * - writer_lock(): Waits for all readers to finish
 *
 * Example:
 * @code
 * zeroipc::Memory mem("/data", 1024 * 1024);
 * zeroipc::RWLock rwlock(mem, "data_lock");
 * zeroipc::Array<int> data(mem, "data", 1000);
 *
 * // Many readers (concurrent)
 * rwlock.reader_lock();
 * int value = data[0];
 * rwlock.reader_unlock();
 *
 * // Single writer (exclusive)
 * rwlock.writer_lock();
 * data[0] = 42;
 * rwlock.writer_unlock();
 *
 * // RAII style
 * {
 *     SharedLock read_guard(rwlock);  // For reading
 *     int value = data[0];
 * }
 * {
 *     UniqueLock write_guard(rwlock);  // For writing
 *     data[0] = 99;
 * }
 * @endcode
 */
class RWLock {
public:
    /**
     * @brief Create or open a RWLock
     * @param mem Memory region
     * @param name Unique name for this RWLock
     * @throws std::runtime_error if allocation fails
     */
    RWLock(Memory& mem, std::string_view name) : mem_(mem) {
        std::string state_name = std::string(name) + "_state";
        std::string reader_mtx_name = std::string(name) + "_rmtx";
        std::string writer_mtx_name = std::string(name) + "_wmtx";

        auto entry = mem.table()->find(name);

        if (entry) {
            // Open existing
            auto state_entry = mem.table()->find(state_name);
            if (!state_entry) {
                throw std::runtime_error("RWLock state not found");
            }
            state_ = mem.ptr_at<RWLockState>(state_entry->offset);

            reader_mutex_ = std::make_unique<Mutex>(mem, reader_mtx_name);
            writer_mutex_ = std::make_unique<Mutex>(mem, writer_mtx_name);
        } else {
            // Create new
            size_t state_offset = mem.allocate(state_name, sizeof(RWLockState));
            state_ = mem.ptr_at<RWLockState>(state_offset);
            new (state_) RWLockState();

            reader_mutex_ = std::make_unique<Mutex>(mem, reader_mtx_name);
            writer_mutex_ = std::make_unique<Mutex>(mem, writer_mtx_name);

            // Add marker entry
            size_t marker_offset = mem.allocate(name, 1);
            (void)marker_offset;
        }
    }

    /**
     * @brief Acquire read lock (shared access)
     *
     * Multiple threads can hold read locks simultaneously.
     * Blocks if a writer currently holds the lock.
     */
    void reader_lock() {
        reader_mutex_->lock();

        // Wait for any active writer to finish
        while (state_->writer_active.load(std::memory_order_acquire) != 0) {
            reader_mutex_->unlock();
            std::this_thread::yield();
            reader_mutex_->lock();
        }

        state_->readers.fetch_add(1, std::memory_order_release);
        reader_mutex_->unlock();
    }

    /**
     * @brief Try to acquire read lock without blocking
     * @return true if lock acquired, false otherwise
     */
    [[nodiscard]] bool try_reader_lock() {
        if (!reader_mutex_->try_lock()) {
            return false;
        }

        if (state_->writer_active.load(std::memory_order_acquire) != 0) {
            reader_mutex_->unlock();
            return false;
        }

        state_->readers.fetch_add(1, std::memory_order_release);
        reader_mutex_->unlock();
        return true;
    }

    /**
     * @brief Release read lock
     */
    void reader_unlock() {
        state_->readers.fetch_sub(1, std::memory_order_release);
    }

    /**
     * @brief Acquire write lock (exclusive access)
     *
     * Only one writer can hold the lock. Blocks until all readers
     * release their locks and no other writer holds the lock.
     */
    void writer_lock() {
        writer_mutex_->lock();

        // Mark writer as active
        state_->writer_active.store(1, std::memory_order_release);

        // Wait for all readers to finish
        while (state_->readers.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }
    }

    /**
     * @brief Try to acquire write lock without blocking
     * @return true if lock acquired, false otherwise
     */
    [[nodiscard]] bool try_writer_lock() {
        if (!writer_mutex_->try_lock()) {
            return false;
        }

        // Check if any readers are active
        if (state_->readers.load(std::memory_order_acquire) > 0) {
            writer_mutex_->unlock();
            return false;
        }

        state_->writer_active.store(1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Release write lock
     */
    void writer_unlock() {
        state_->writer_active.store(0, std::memory_order_release);
        writer_mutex_->unlock();
    }

    // Prevent copying
    RWLock(const RWLock&) = delete;
    RWLock& operator=(const RWLock&) = delete;

    // Allow moving
    RWLock(RWLock&&) = default;
    RWLock& operator=(RWLock&&) = default;

private:
    Memory& mem_;
    RWLockState* state_;
    std::unique_ptr<Mutex> reader_mutex_;
    std::unique_ptr<Mutex> writer_mutex_;
};

/**
 * @brief RAII wrapper for read lock (shared access)
 *
 * Automatically acquires reader lock on construction and releases on destruction.
 */
class SharedLock {
public:
    explicit SharedLock(RWLock& rwlock) : rwlock_(rwlock) {
        rwlock_.reader_lock();
    }

    ~SharedLock() {
        rwlock_.reader_unlock();
    }

    // Prevent copying
    SharedLock(const SharedLock&) = delete;
    SharedLock& operator=(const SharedLock&) = delete;

private:
    RWLock& rwlock_;
};

/**
 * @brief RAII wrapper for write lock (exclusive access)
 *
 * Automatically acquires writer lock on construction and releases on destruction.
 */
class UniqueLock {
public:
    explicit UniqueLock(RWLock& rwlock) : rwlock_(rwlock) {
        rwlock_.writer_lock();
    }

    ~UniqueLock() {
        rwlock_.writer_unlock();
    }

    // Prevent copying
    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

private:
    RWLock& rwlock_;
};

} // namespace zeroipc
