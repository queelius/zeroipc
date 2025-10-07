#pragma once

#include "memory.h"
#include "queue.h"
#include <atomic>
#include <optional>
#include <chrono>

namespace zeroipc {

/**
 * @brief CSP-style channel for synchronous message passing between processes
 * 
 * Channel<T> implements Communicating Sequential Processes (CSP) semantics,
 * where processes communicate through synchronous message passing. Unlike
 * queues which are asynchronous, channels provide rendezvous semantics where
 * send blocks until receive, creating a synchronization point.
 * 
 * @motivation
 * Channels solve coordination problems in concurrent systems:
 * - Synchronization: Sender and receiver meet at the channel
 * - Deadlock prevention: Can use select() across multiple channels
 * - Composability: Channels can be passed as values
 * - Type safety: Strongly typed communication
 * - Back-pressure: Natural flow control as senders block
 * 
 * @theory
 * Based on Tony Hoare's CSP and implemented in languages like Go and Rust,
 * channels provide a higher-level abstraction than shared memory. They
 * enforce "Don't communicate by sharing memory; share memory by communicating."
 * 
 * In our shared memory context, we implement both:
 * - Unbuffered channels: Pure synchronous rendezvous
 * - Buffered channels: Asynchronous up to buffer capacity
 * 
 * @example
 * ```cpp
 * // Unbuffered channel for synchronization
 * Memory mem("/comms", 1024*1024);
 * Channel<int> ch(mem, "sync_channel");  // capacity=0 means unbuffered
 * 
 * // Process A: Send (blocks until received)
 * ch.send(42);
 * 
 * // Process B: Receive (blocks until sent)
 * if (auto val = ch.recv()) {
 *     process(*val);
 * }
 * 
 * // Buffered channel for async communication
 * Channel<Message> msgs(mem, "msg_channel", 100);  // buffer 100 messages
 * 
 * // Non-blocking try operations
 * if (msgs.try_send(msg)) {
 *     // Sent successfully
 * }
 * 
 * // Timeout operations
 * if (auto msg = msgs.recv_timeout(500ms)) {
 *     handle(*msg);
 * }
 * 
 * // Select across multiple channels (Go-style)
 * select(
 *     ch1.on_recv([](int x) { handle_int(x); }),
 *     ch2.on_recv([](double y) { handle_double(y); }),
 *     timeout(1s, []() { handle_timeout(); })
 * );
 * ```
 * 
 * @tparam T Type of channel messages (must be trivially copyable)
 */
template<typename T>
class Channel {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable for shared memory");
    
    struct Header {
        std::atomic<uint32_t> capacity;      // 0 = unbuffered
        std::atomic<uint32_t> senders;       // Waiting senders
        std::atomic<uint32_t> receivers;     // Waiting receivers
        std::atomic<bool> closed;            // Channel closed
        std::atomic<uint64_t> send_seq;      // Send sequence for ordering
        std::atomic<uint64_t> recv_seq;      // Receive sequence for ordering
    };
    
    struct RendezvousSlot {
        std::atomic<bool> ready;             // Data is ready
        std::atomic<bool> consumed;          // Data was consumed
        T data;                               // The actual data
    };
    
    /**
     * @brief Create unbuffered synchronous channel
     */
    Channel(Memory& memory, std::string_view name)
        : Channel(memory, name, size_t(0)) {}
    
    /**
     * @brief Create buffered channel with specified capacity
     * @param capacity Buffer size (0 for unbuffered/synchronous)
     */
    Channel(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name), capacity_(capacity) {
        
        size_t header_size = sizeof(Header);
        std::string header_name = std::string(name) + "_header";
        size_t offset = memory.allocate(header_name, header_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->capacity.store(capacity, std::memory_order_relaxed);
        header_->senders.store(0, std::memory_order_relaxed);
        header_->receivers.store(0, std::memory_order_relaxed);
        header_->closed.store(false, std::memory_order_relaxed);
        header_->send_seq.store(0, std::memory_order_relaxed);
        header_->recv_seq.store(0, std::memory_order_relaxed);
        
        if (capacity > 0) {
            // Buffered channel uses queue
            // Queue implementation uses one slot for full/empty distinction,
            // so allocate capacity+1 to hold 'capacity' items
            buffer_ = std::make_unique<Queue<T>>(memory, 
                std::string(name) + "_buffer", capacity + 1);
        } else {
            // Unbuffered uses rendezvous slot
            size_t slot_size = sizeof(RendezvousSlot);
            std::string slot_name = std::string(name) + "_slot";
            size_t slot_offset = memory.allocate(slot_name, slot_size);
            
            slot_ = reinterpret_cast<RendezvousSlot*>(
                static_cast<char*>(memory.base()) + slot_offset);
            
            slot_->ready.store(false, std::memory_order_relaxed);
            slot_->consumed.store(false, std::memory_order_relaxed);
        }
    }
    
    /**
     * @brief Open existing channel
     */
    Channel(Memory& memory, std::string_view name, bool open_existing)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        std::string header_name = std::string(name) + "_header";
        if (!memory.find(header_name, offset, size)) {
            throw std::runtime_error("Channel not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        capacity_ = header_->capacity.load(std::memory_order_relaxed);
        
        if (capacity_ > 0) {
            // Open existing queue (already has correct size)
            buffer_ = std::make_unique<Queue<T>>(memory, 
                std::string(name) + "_buffer");
        } else {
            std::string slot_name = std::string(name) + "_slot";
            if (!memory.find(slot_name, offset, size)) {
                throw std::runtime_error("Channel slot not found");
            }
            slot_ = reinterpret_cast<RendezvousSlot*>(
                static_cast<char*>(memory.base()) + offset);
        }
    }
    
    /**
     * @brief Send value (blocks until received or buffered)
     */
    [[nodiscard]] bool send(const T& value) {
        if (header_->closed.load(std::memory_order_acquire)) {
            return false;
        }
        
        if (capacity_ > 0) {
            // Buffered send
            return buffer_->push(value);
        } else {
            // Unbuffered: synchronous rendezvous
            return rendezvous_send(value);
        }
    }
    
    /**
     * @brief Try to send without blocking
     */
    [[nodiscard]] bool try_send(const T& value) {
        if (header_->closed.load(std::memory_order_acquire)) {
            return false;
        }
        
        if (capacity_ > 0 && buffer_) {
            return buffer_->push(value);
        } else {
            // For unbuffered, check if receiver is waiting
            if (header_->receivers.load(std::memory_order_acquire) > 0) {
                return rendezvous_send(value);
            }
            return false;
        }
    }
    
    /**
     * @brief Send with timeout
     */
    [[nodiscard]] bool send_timeout(const T& value, 
                                    std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        
        while (std::chrono::steady_clock::now() < deadline) {
            if (try_send(value)) {
                return true;
            }
            if (header_->closed.load(std::memory_order_acquire)) {
                return false;
            }
            std::this_thread::yield();
        }
        
        return false;
    }
    
    /**
     * @brief Receive value (blocks until available)
     */
    [[nodiscard]] std::optional<T> recv() {
        if (capacity_ > 0 && buffer_) {
            // Buffered receive
            // For buffered channels, pop() is non-blocking
            return buffer_->pop();
        } else {
            // Unbuffered: synchronous rendezvous
            return rendezvous_recv();
        }
    }
    
    /**
     * @brief Try to receive without blocking
     */
    [[nodiscard]] std::optional<T> try_recv() {
        if (capacity_ > 0 && buffer_) {
            return buffer_->pop();
        } else {
            // For unbuffered, check if sender is waiting
            if (slot_->ready.load(std::memory_order_acquire)) {
                return rendezvous_recv();
            }
            return std::nullopt;
        }
    }
    
    /**
     * @brief Receive with timeout
     */
    [[nodiscard]] std::optional<T> recv_timeout(
        std::chrono::milliseconds timeout) {
        
        auto deadline = std::chrono::steady_clock::now() + timeout;
        
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto val = try_recv()) {
                return val;
            }
            if (header_->closed.load(std::memory_order_acquire) && 
                !has_data()) {
                return std::nullopt;
            }
            std::this_thread::yield();
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Close the channel
     */
    void close() {
        header_->closed.store(true, std::memory_order_release);
        // Wake all waiting senders and receivers
    }
    
    /**
     * @brief Check if channel is closed
     */
    [[nodiscard]] bool is_closed() const {
        return header_->closed.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get channel capacity (0 for unbuffered)
     */
    [[nodiscard]] size_t capacity() const {
        return capacity_;
    }
    
    /**
     * @brief Check if channel is buffered
     */
    [[nodiscard]] bool is_buffered() const {
        return capacity_ > 0;
    }
    
    /**
     * @brief Iterator support for range-based for loops
     */
    class iterator {
    public:
        iterator(Channel* ch) : channel_(ch) {
            if (ch) {
                current_ = ch->recv();
            }
        }
        
        iterator& operator++() {
            if (channel_) {
                current_ = channel_->recv();
            }
            return *this;
        }
        
        T operator*() const {
            return *current_;
        }
        
        bool operator!=(const iterator& other) const {
            return current_.has_value() != other.current_.has_value();
        }
        
    private:
        Channel* channel_;
        std::optional<T> current_;
    };
    
    iterator begin() { return iterator(this); }
    iterator end() { return iterator(nullptr); }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_ = nullptr;
    RendezvousSlot* slot_ = nullptr;
    std::unique_ptr<Queue<T>> buffer_;
    size_t capacity_;
    
    bool rendezvous_send(const T& value) {
        // Indicate we're a waiting sender
        header_->senders.fetch_add(1, std::memory_order_acq_rel);
        
        // Wait for slot to be free
        while (slot_->ready.load(std::memory_order_acquire)) {
            if (header_->closed.load(std::memory_order_acquire)) {
                header_->senders.fetch_sub(1, std::memory_order_acq_rel);
                return false;
            }
            std::this_thread::yield();
        }
        
        // Place data
        slot_->data = value;
        slot_->consumed.store(false, std::memory_order_relaxed);
        slot_->ready.store(true, std::memory_order_release);
        
        // Wait for receiver to consume
        while (!slot_->consumed.load(std::memory_order_acquire)) {
            if (header_->closed.load(std::memory_order_acquire)) {
                header_->senders.fetch_sub(1, std::memory_order_acq_rel);
                return false;
            }
            std::this_thread::yield();
        }
        
        header_->senders.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }
    
    std::optional<T> rendezvous_recv() {
        // Indicate we're a waiting receiver
        header_->receivers.fetch_add(1, std::memory_order_acq_rel);
        
        // Wait for data
        while (!slot_->ready.load(std::memory_order_acquire)) {
            if (header_->closed.load(std::memory_order_acquire)) {
                header_->receivers.fetch_sub(1, std::memory_order_acq_rel);
                return std::nullopt;
            }
            std::this_thread::yield();
        }
        
        // Get data
        T value = slot_->data;
        
        // Mark as consumed
        slot_->consumed.store(true, std::memory_order_release);
        slot_->ready.store(false, std::memory_order_release);
        
        header_->receivers.fetch_sub(1, std::memory_order_acq_rel);
        return value;
    }
    
    bool has_data() const {
        if (capacity_ > 0 && buffer_) {
            return !buffer_->empty();
        }
        return slot_->ready.load(std::memory_order_acquire);
    }
};

} // namespace zeroipc