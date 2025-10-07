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
        std::atomic<uint32_t> free_head;  // Head of free list
        std::atomic<uint32_t> allocated;  // Number of allocated items
        uint32_t capacity;
        uint32_t elem_size;
    };
    
    static constexpr uint32_t NULL_INDEX = 0xFFFFFFFF;
    
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
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->free_head.store(0, std::memory_order_relaxed);
        header_->allocated.store(0, std::memory_order_relaxed);
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
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }
        
        nodes_ = reinterpret_cast<Node*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
    }
    
    // Allocate an object from the pool (lock-free)
    [[nodiscard]] std::optional<T*> allocate() {
        uint32_t free_index;
        
        // Try to get a free node
        do {
            free_index = header_->free_head.load(std::memory_order_acquire);
            
            if (free_index == NULL_INDEX) {
                return std::nullopt;  // Pool is full
            }
            
            uint32_t next = nodes_[free_index].next.load(std::memory_order_relaxed);
            
            // Try to update the free head
            if (header_->free_head.compare_exchange_weak(
                    free_index, next,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                // Success - we got the node
                header_->allocated.fetch_add(1, std::memory_order_relaxed);
                return &nodes_[free_index].data;
            }
        } while (true);
    }
    
    // Deallocate an object back to the pool (lock-free)
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
        
        // Add the node back to the free list
        uint32_t old_head;
        do {
            old_head = header_->free_head.load(std::memory_order_acquire);
            node->next.store(old_head, std::memory_order_relaxed);
        } while (!header_->free_head.compare_exchange_weak(
                    old_head, node_index,
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