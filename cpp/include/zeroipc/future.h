#pragma once

#include "memory.h"
#include <atomic>
#include <optional>
#include <chrono>
#include <cstring>
#include <thread>

namespace zeroipc {

/**
 * @brief Shared memory Future for asynchronous computation results
 * 
 * Future represents a value that will be available at some point in the future.
 * It enables asynchronous computation patterns where one process can compute
 * a value while others wait for the result. This is the shared memory equivalent
 * of std::future but works across process boundaries.
 * 
 * @motivation
 * In distributed simulations and parallel processing, we often need to:
 * - Decouple producers and consumers of computation results
 * - Allow multiple processes to wait on the same result
 * - Chain computations without blocking (continuation-passing style)
 * - Handle timeouts and error conditions gracefully
 * 
 * @example
 * ```cpp
 * // Process A: Producer
 * Memory mem("/simulation", 10*1024*1024);
 * Future<double> energy_future(mem, "total_energy");
 * 
 * // Compute expensive result
 * double energy = compute_system_energy();
 * energy_future.set_value(energy);
 * 
 * // Process B: Consumer
 * Memory mem("/simulation");
 * Future<double> energy_future(mem, "total_energy", true);  // open existing
 * 
 * // Wait for result (blocks)
 * double energy = energy_future.get();
 * 
 * // Or try with timeout
 * if (auto energy = energy_future.get_for(100ms)) {
 *     process_energy(*energy);
 * }
 * ```
 * 
 * @tparam T Type of the future value (must be trivially copyable)
 */
template<typename T>
class Future {
public:
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for shared memory");
    
    enum State : uint32_t {
        PENDING = 0,
        COMPUTING = 1,
        READY = 2,
        ERROR = 3
    };
    
    struct Header {
        std::atomic<State> state;
        std::atomic<uint32_t> waiters;
        std::atomic<uint64_t> completion_time;  // For timeout support
        T value;
        char error_msg[256];
    };
    
    // Create new future
    Future(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t total_size = sizeof(Header);
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->state.store(PENDING, std::memory_order_relaxed);
        header_->waiters.store(0, std::memory_order_relaxed);
        header_->completion_time.store(0, std::memory_order_relaxed);
        std::memset(header_->error_msg, 0, sizeof(header_->error_msg));
    }
    
    // Open existing future
    Future(Memory& memory, std::string_view name, bool)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Future not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
    }
    
    // Set the value (can only be called once)
    [[nodiscard]] bool set_value(const T& value) {
        State expected = PENDING;
        if (!header_->state.compare_exchange_strong(expected, COMPUTING,
                                                    std::memory_order_acquire,
                                                    std::memory_order_relaxed)) {
            return false;  // Already set or in error
        }
        
        header_->value = value;
        header_->completion_time.store(
            std::chrono::steady_clock::now().time_since_epoch().count(),
            std::memory_order_relaxed);
        
        header_->state.store(READY, std::memory_order_release);
        
        // Wake any waiters (in real impl, would use futex or condvar)
        wake_waiters();
        return true;
    }
    
    // Set error state
    [[nodiscard]] bool set_error(const std::string& error) {
        State expected = PENDING;
        if (!header_->state.compare_exchange_strong(expected, ERROR,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_relaxed)) {
            return false;
        }
        
        std::strncpy(header_->error_msg, error.c_str(), 
                     sizeof(header_->error_msg) - 1);
        header_->completion_time.store(
            std::chrono::steady_clock::now().time_since_epoch().count(),
            std::memory_order_relaxed);
        
        wake_waiters();
        return true;
    }
    
    // Get the value (blocks until ready)
    [[nodiscard]] T get() {
        wait_ready();
        
        State state = header_->state.load(std::memory_order_acquire);
        if (state == ERROR) {
            throw std::runtime_error(header_->error_msg);
        }
        return header_->value;
    }
    
    // Try to get value without blocking
    [[nodiscard]] std::optional<T> try_get() {
        State state = header_->state.load(std::memory_order_acquire);
        if (state == READY) {
            return header_->value;
        }
        if (state == ERROR) {
            throw std::runtime_error(header_->error_msg);
        }
        return std::nullopt;
    }
    
    // Wait with timeout
    [[nodiscard]] std::optional<T> get_for(std::chrono::milliseconds timeout) {
        if (wait_ready_for(timeout)) {
            State state = header_->state.load(std::memory_order_acquire);
            if (state == ERROR) {
                throw std::runtime_error(header_->error_msg);
            }
            return header_->value;
        }
        return std::nullopt;
    }
    
    // Check if ready
    [[nodiscard]] bool is_ready() const {
        State state = header_->state.load(std::memory_order_acquire);
        return state == READY || state == ERROR;
    }
    
    // Get current state
    [[nodiscard]] State state() const {
        return header_->state.load(std::memory_order_acquire);
    }
    
    // Then combinator for chaining (simplified version)
    template<typename F>
    Future<std::invoke_result_t<F, T>> then(Memory& mem, const std::string& next_name, F&& func) {
        using U = std::invoke_result_t<F, T>;
        Future<U> next(mem, next_name);
        
        // In a real implementation, this would register a continuation
        // For now, we'll do a simple synchronous version
        if (is_ready()) {
            try {
                U result = func(get());
                next.set_value(result);
            } catch (const std::exception& e) {
                next.set_error(e.what());
            }
        }
        
        return next;
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_ = nullptr;
    
    void wait_ready() {
        header_->waiters.fetch_add(1, std::memory_order_relaxed);
        
        while (!is_ready()) {
            // In real impl: use futex_wait or condition variable
            std::this_thread::yield();
        }
        
        header_->waiters.fetch_sub(1, std::memory_order_relaxed);
    }
    
    bool wait_ready_for(std::chrono::milliseconds timeout) {
        auto start = std::chrono::steady_clock::now();
        header_->waiters.fetch_add(1, std::memory_order_relaxed);
        
        while (!is_ready()) {
            if (std::chrono::steady_clock::now() - start > timeout) {
                header_->waiters.fetch_sub(1, std::memory_order_relaxed);
                return false;
            }
            std::this_thread::yield();
        }
        
        header_->waiters.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    
    void wake_waiters() {
        // In real impl: use futex_wake or condition variable notify_all
        // For now, waiters will notice state change on next check
    }
};

} // namespace zeroipc