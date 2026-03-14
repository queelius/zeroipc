#pragma once

#include "memory.h"
#include <atomic>
#include <optional>

namespace zeroipc {

template<typename T>
class Pool {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable for shared memory");

    struct Node {
        T data;
        std::atomic<uint32_t> next;  // Index of next free node
    };

    struct Header {
        std::atomic<uint64_t> free_head;  // Tagged pointer: [generation:32][index:32]
        std::atomic<uint32_t> allocated;  // Number of allocated items
        uint32_t padding_;  // Alignment padding (was part of old free_head slot)
        uint32_t capacity;
        uint32_t elem_size;
    };

    static constexpr uint32_t NULL_INDEX = 0xFFFFFFFF;

    // Tagged pointer helpers to prevent ABA problem
    static uint64_t pack_tagged(uint32_t index, uint32_t generation) {
        return (static_cast<uint64_t>(generation) << 32) | index;
    }

    static uint32_t unpack_index(uint64_t tagged) {
        return static_cast<uint32_t>(tagged & 0xFFFFFFFF);
    }

    static uint32_t unpack_generation(uint64_t tagged) {
        return static_cast<uint32_t>(tagged >> 32);
    }

    // Create new pool
    Pool(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {

        if (capacity == 0) {
            throw std::invalid_argument("Pool capacity must be greater than 0");
        }

        // Check for overflow
        if (capacity > (SIZE_MAX - sizeof(Header)) / sizeof(Node)) {
            throw std::overflow_error("Pool capacity too large");
        }

        size_t total_size = sizeof(Header) + sizeof(Node) * capacity;
        size_t offset = memory.allocate(name, total_size);

        header_ = memory.ptr_at<Header>(offset);

        // Initialize header with tagged pointer (generation=0, index=0)
        header_->free_head.store(pack_tagged(0, 0), std::memory_order_relaxed);
        header_->allocated.store(0, std::memory_order_relaxed);
        header_->padding_ = 0;
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);

        nodes_ = reinterpret_cast<Node*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));

        // Initialize free list - all nodes are free
        for (uint32_t i = 0; i < capacity - 1; ++i) {
            nodes_[i].next.store(i + 1, std::memory_order_relaxed);
        }
        nodes_[capacity - 1].next.store(NULL_INDEX, std::memory_order_relaxed);
    }

    // Open existing pool
    Pool(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {

        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Pool not found: " + std::string(name));
        }

        header_ = memory.ptr_at<Header>(offset);

        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }

        nodes_ = reinterpret_cast<Node*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
    }

    // Allocate an object from the pool (lock-free, ABA-safe)
    [[nodiscard]] std::optional<T*> allocate() {
        uint64_t old_head;
        uint64_t new_head;

        // Try to get a free node using tagged pointer CAS
        do {
            old_head = header_->free_head.load(std::memory_order_acquire);
            uint32_t free_index = unpack_index(old_head);
            uint32_t generation = unpack_generation(old_head);

            if (free_index == NULL_INDEX) {
                return std::nullopt;  // Pool is full
            }

            uint32_t next = nodes_[free_index].next.load(std::memory_order_relaxed);

            // Pack new head with bumped generation to prevent ABA
            new_head = pack_tagged(next, generation + 1);

            // Try to update the free head (tagged CAS prevents ABA)
            if (header_->free_head.compare_exchange_weak(
                    old_head, new_head,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                // Success - we got the node
                header_->allocated.fetch_add(1, std::memory_order_relaxed);
                return &nodes_[free_index].data;
            }
        } while (true);
    }

    // Deallocate an object back to the pool (lock-free, ABA-safe)
    void deallocate(T* ptr) {
        if (!ptr) return;

        // Calculate the node index from the pointer
        Node* node = reinterpret_cast<Node*>(
            reinterpret_cast<char*>(ptr) - offsetof(Node, data));
        uint32_t node_index = node - nodes_;

        // Validate the index
        if (node_index >= header_->capacity) {
            throw std::invalid_argument("Invalid pointer to deallocate");
        }

        // Add the node back to the free list using tagged CAS
        uint64_t old_head;
        uint64_t new_head;
        do {
            old_head = header_->free_head.load(std::memory_order_acquire);
            uint32_t old_index = unpack_index(old_head);
            uint32_t generation = unpack_generation(old_head);

            node->next.store(old_index, std::memory_order_relaxed);

            // Pack new head with bumped generation to prevent ABA
            new_head = pack_tagged(node_index, generation + 1);
        } while (!header_->free_head.compare_exchange_weak(
                    old_head, new_head,
                    std::memory_order_release,
                    std::memory_order_acquire));

        header_->allocated.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // Construct an object in the pool
    template<typename... Args>
    [[nodiscard]] std::optional<T*> construct(Args&&... args) {
        auto ptr = allocate();
        if (ptr) {
            new (*ptr) T(std::forward<Args>(args)...);
        }
        return ptr;
    }
    
    // Destroy an object and return it to the pool
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();
            deallocate(ptr);
        }
    }
    
    // Get number of allocated objects
    [[nodiscard]] size_t allocated() const {
        return header_->allocated.load(std::memory_order_relaxed);
    }
    
    // Get number of free objects
    [[nodiscard]] size_t available() const {
        return header_->capacity - allocated();
    }
    
    // Get pool capacity
    [[nodiscard]] size_t capacity() const {
        return header_->capacity;
    }
    
    // Check if pool is empty (all objects are free)
    [[nodiscard]] bool empty() const {
        return allocated() == 0;
    }
    
    // Check if pool is full (no free objects)
    [[nodiscard]] bool full() const {
        return allocated() == header_->capacity;
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_ = nullptr;
    Node* nodes_ = nullptr;
};

} // namespace zeroipc