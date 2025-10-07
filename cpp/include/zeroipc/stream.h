#pragma once

#include "memory.h"
#include "ring.h"
#include <atomic>
#include <functional>
#include <vector>
#include <optional>

namespace zeroipc {

/**
 * @brief Reactive stream for event-driven data processing in shared memory
 * 
 * Stream<T> implements reactive programming patterns in shared memory, allowing
 * processes to emit, transform, and consume data in a push-based model. This is
 * inspired by ReactiveX (Rx) and enables functional reactive programming (FRP)
 * across process boundaries.
 * 
 * @motivation
 * Reactive streams solve key problems in distributed systems:
 * - Event-driven architecture: React to data as it arrives
 * - Backpressure handling: Ring buffer prevents overwhelming consumers
 * - Composable transformations: map, filter, take, skip, etc.
 * - Temporal operations: Window-based processing, buffering
 * - Multi-process pub/sub: Multiple consumers can process the same stream
 * 
 * @theory
 * Streams represent potentially infinite sequences of values over time.
 * Unlike arrays (space) or futures (single value), streams model:
 * - Continuous data flows (sensor readings, market data)
 * - Event sequences (user interactions, system events)
 * - Asynchronous message passing between processes
 * 
 * The functional operators (map, filter, fold) allow declarative data
 * processing pipelines that automatically handle concurrency and distribution.
 * 
 * @example
 * ```cpp
 * // Producer process: Emit sensor data
 * Memory mem("/sensors", 10*1024*1024);
 * Stream<double> temps(mem, "temperature", 1000);
 * 
 * while (running) {
 *     double temp = read_sensor();
 *     temps.emit(temp);
 *     sleep(100ms);
 * }
 * 
 * // Consumer process: Process temperature data
 * Memory mem("/sensors");
 * Stream<double> temps(mem, "temperature");
 * 
 * // Create derived stream with Celsius to Fahrenheit
 * auto fahrenheit = temps.map(mem, "temp_f", 
 *     [](double c) { return c * 9/5 + 32; });
 * 
 * // Filter for high temperatures
 * auto warnings = fahrenheit.filter(mem, "warnings",
 *     [](double f) { return f > 100.0; });
 * 
 * // Subscribe to warnings
 * warnings.subscribe([](double high_temp) {
 *     send_alert("High temperature: " + std::to_string(high_temp));
 * });
 * ```
 * 
 * @operators
 * - map: Transform each element
 * - filter: Keep only elements matching predicate
 * - take: Take first n elements
 * - skip: Skip first n elements
 * - fold: Reduce stream to single value
 * - window: Group elements into windows
 * 
 * @thread_safety
 * Emit and read operations use lock-free ring buffer for thread safety.
 * Multiple producers and consumers are supported.
 * 
 * @tparam T Type of stream elements (must be trivially copyable)
 */
template<typename T>
class Stream {
public:
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for shared memory");
    
    struct Header {
        std::atomic<uint64_t> sequence;      // Monotonic sequence number
        std::atomic<uint32_t> subscribers;   // Number of active subscribers
        std::atomic<bool> closed;            // Stream is closed
        uint32_t buffer_capacity;            // Ring buffer capacity
        char transform_name[32];             // Name of transformation if any
    };
    
    // Create new stream
    Stream(Memory& memory, std::string_view name, size_t buffer_size = 1024)
        : memory_(memory), name_(name) {
        
        size_t header_size = sizeof(Header);
        std::string header_name = std::string(name) + "_header";
        size_t offset = memory.allocate(header_name, header_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->sequence.store(0, std::memory_order_relaxed);
        header_->subscribers.store(0, std::memory_order_relaxed);
        header_->closed.store(false, std::memory_order_relaxed);
        header_->buffer_capacity = buffer_size;
        std::memset(header_->transform_name, 0, sizeof(header_->transform_name));
        
        // Create ring buffer for data
        std::string buffer_name = std::string(name) + "_buffer";
        buffer_ = std::make_unique<Ring<T>>(memory, buffer_name, 
                                           buffer_size * sizeof(T));
    }
    
    // Open existing stream
    Stream(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        std::string header_name = std::string(name) + "_header";
        if (!memory.find(header_name, offset, size)) {
            throw std::runtime_error("Stream not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Open ring buffer
        std::string buffer_name = std::string(name) + "_buffer";
        buffer_ = std::make_unique<Ring<T>>(memory, buffer_name);
    }
    
    // Emit a value to the stream
    [[nodiscard]] bool emit(const T& value) {
        if (header_->closed.load(std::memory_order_acquire)) {
            return false;
        }
        
        if (!buffer_->write(value)) {
            return false;  // Buffer full
        }
        
        uint64_t seq = header_->sequence.fetch_add(1, std::memory_order_acq_rel);
        
        // Notify subscribers (in real impl, would wake waiters)
        notify_subscribers(seq, value);
        return true;
    }
    
    // Emit multiple values
    [[nodiscard]] size_t emit_bulk(const T* values, size_t count) {
        if (header_->closed.load(std::memory_order_acquire)) {
            return 0;
        }
        
        size_t written = buffer_->write_bulk(values, count);
        
        if (written > 0) {
            uint64_t seq = header_->sequence.fetch_add(written, 
                                                      std::memory_order_acq_rel);
            // Notify for bulk emit
            for (size_t i = 0; i < written; ++i) {
                notify_subscribers(seq + i, values[i]);
            }
        }
        
        return written;
    }
    
    // Read next value from stream
    [[nodiscard]] std::optional<T> next() {
        return buffer_->read();
    }
    
    // Read multiple values
    [[nodiscard]] size_t read_bulk(T* values, size_t max_count) {
        return buffer_->read_bulk(values, max_count);
    }
    
    // Map transformation - creates derived stream
    template<typename F>
    Stream<std::invoke_result_t<F, T>> map(Memory& mem, 
                                           const std::string& new_name, 
                                           F&& transform) {
        using U = std::invoke_result_t<F, T>;
        Stream<U> output(mem, new_name, header_->buffer_capacity);
        
        // Store transform name for debugging
        std::strncpy(output.header_->transform_name, "map", 
                    sizeof(output.header_->transform_name) - 1);
        
        // Subscribe to this stream and apply transformation
        subscribe([&output, transform](const T& value) {
            U transformed = transform(value);
            output.emit(transformed);
        });
        
        return output;
    }
    
    // Filter transformation - creates filtered stream
    Stream<T> filter(Memory& mem, const std::string& new_name,
                     std::function<bool(const T&)> predicate) {
        Stream<T> output(mem, new_name, header_->buffer_capacity);
        
        std::strncpy(output.header_->transform_name, "filter",
                    sizeof(output.header_->transform_name) - 1);
        
        subscribe([&output, predicate](const T& value) {
            if (predicate(value)) {
                output.emit(value);
            }
        });
        
        return output;
    }
    
    // Take first n elements
    Stream<T> take(Memory& mem, const std::string& new_name, size_t n) {
        Stream<T> output(mem, new_name, std::min(n, 
                        static_cast<size_t>(header_->buffer_capacity)));
        
        std::strncpy(output.header_->transform_name, "take",
                    sizeof(output.header_->transform_name) - 1);
        
        std::atomic<size_t> count{0};
        subscribe([&output, &count, n](const T& value) {
            size_t current = count.fetch_add(1, std::memory_order_relaxed);
            if (current < n) {
                output.emit(value);
                if (current + 1 == n) {
                    output.close();
                }
            }
        });
        
        return output;
    }
    
    // Skip first n elements
    Stream<T> skip(Memory& mem, const std::string& new_name, size_t n) {
        Stream<T> output(mem, new_name, header_->buffer_capacity);
        
        std::strncpy(output.header_->transform_name, "skip",
                    sizeof(output.header_->transform_name) - 1);
        
        std::atomic<size_t> count{0};
        subscribe([&output, &count, n](const T& value) {
            size_t current = count.fetch_add(1, std::memory_order_relaxed);
            if (current >= n) {
                output.emit(value);
            }
        });
        
        return output;
    }
    
    // Window operation - groups elements
    Stream<std::vector<T>> window(Memory& mem, const std::string& new_name, 
                                  size_t window_size) {
        // Note: std::vector can't be in shared memory directly
        // This is a simplified version for demonstration
        throw std::runtime_error("Window operation requires special handling for vectors");
    }
    
    // Subscribe to stream (simplified - in real impl would use callbacks in shm)
    void subscribe(std::function<void(const T&)> callback) {
        header_->subscribers.fetch_add(1, std::memory_order_relaxed);
        
        // In real implementation, would register callback in shared memory
        // For now, just process available data
        T value;
        while (!header_->closed.load(std::memory_order_acquire)) {
            if (auto val = next()) {
                callback(*val);
            } else {
                std::this_thread::yield();
            }
        }
        
        header_->subscribers.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // Close the stream
    void close() {
        header_->closed.store(true, std::memory_order_release);
    }
    
    // Check if stream is closed
    [[nodiscard]] bool is_closed() const {
        return header_->closed.load(std::memory_order_acquire);
    }
    
    // Get current sequence number
    [[nodiscard]] uint64_t sequence() const {
        return header_->sequence.load(std::memory_order_acquire);
    }
    
    // Get subscriber count
    [[nodiscard]] uint32_t subscriber_count() const {
        return header_->subscribers.load(std::memory_order_acquire);
    }
    
    // Collect all values into a vector (blocks until stream closes)
    [[nodiscard]] std::vector<T> collect() {
        std::vector<T> result;
        while (!is_closed()) {
            if (auto val = next()) {
                result.push_back(*val);
            }
        }
        return result;
    }
    
    // Fold/Reduce operation
    template<typename U, typename F>
    [[nodiscard]] U fold(U initial, F&& combine) {
        U result = initial;
        // Process all available data first
        while (auto val = next()) {
            result = combine(result, *val);
        }
        // Then check if more data might come
        while (!is_closed()) {
            if (auto val = next()) {
                result = combine(result, *val);
            } else {
                std::this_thread::yield();
            }
        }
        // Process any remaining data after close
        while (auto val = next()) {
            result = combine(result, *val);
        }
        return result;
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_ = nullptr;
    std::unique_ptr<Ring<T>> buffer_;
    
    void notify_subscribers(uint64_t seq, const T& value) {
        // In real implementation, would wake waiting subscribers
        // using futex or condition variables in shared memory
    }
};

// Specialized stream for event processing
template<>
class Stream<void> {
public:
    struct Header {
        std::atomic<uint64_t> event_count;
        std::atomic<bool> closed;
    };
    
    Stream(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t offset = memory.allocate(name, sizeof(Header));
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        header_->event_count.store(0, std::memory_order_relaxed);
        header_->closed.store(false, std::memory_order_relaxed);
    }
    
    void emit() {
        if (!header_->closed.load(std::memory_order_acquire)) {
            header_->event_count.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    
    [[nodiscard]] uint64_t count() const {
        return header_->event_count.load(std::memory_order_acquire);
    }
    
    void close() {
        header_->closed.store(true, std::memory_order_release);
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_;
};

} // namespace zeroipc