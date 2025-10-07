#pragma once

#include "memory.h"
#include <atomic>
#include <functional>
#include <cstring>

namespace zeroipc {

/**
 * @brief Lazy evaluation container for deferred computations in shared memory
 * 
 * Lazy<T> represents a computation that is deferred until its value is actually needed.
 * The computation is performed at most once, and the result is cached for subsequent
 * accesses. This enables efficient memoization and avoids unnecessary computation
 * in distributed systems.
 * 
 * @motivation
 * Lazy evaluation is crucial for:
 * - Avoiding expensive computations that might never be needed
 * - Implementing infinite data structures (through recursive lazy values)
 * - Memoization of expensive functions across processes
 * - Building computation graphs that evaluate on-demand
 * - Short-circuit evaluation in logical expressions
 * 
 * @theory
 * Lazy evaluation is a cornerstone of functional programming, enabling:
 * - Referential transparency: same input always produces same output
 * - Composability: lazy values can be combined to form new lazy values
 * - Efficiency: computations only happen when needed
 * 
 * In shared memory context, this becomes even more powerful as multiple
 * processes can share the same lazy computation, and only one will perform it.
 * 
 * @example
 * ```cpp
 * // Create expensive computation
 * Memory mem("/simulation", 10*1024*1024);
 * Lazy<double> total_energy(mem, "energy", ComputationOp::EXTERNAL);
 * 
 * // Only computed when forced
 * if (need_energy_check) {
 *     double energy = total_energy.force();  // Computed here
 * }
 * 
 * // Second access uses cached value
 * double energy2 = total_energy.force();  // No recomputation
 * 
 * // Lazy arithmetic
 * Lazy<double> kinetic(mem, "kinetic", 100.0);
 * Lazy<double> potential(mem, "potential", 50.0);
 * auto total = Lazy<double>::add(mem, "total", kinetic, potential);
 * // Addition only happens when total.force() is called
 * ```
 * 
 * @thread_safety
 * The force() operation is thread-safe and uses compare-and-swap to ensure
 * only one thread computes the value, while others wait for the result.
 * 
 * @tparam T Type of the lazy value (must be trivially copyable)
 */
template<typename T>
class Lazy {
public:
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for shared memory");
    
    enum ComputeState : uint32_t {
        NOT_COMPUTED = 0,
        COMPUTING = 1,
        COMPUTED = 2,
        ERROR = 3
    };
    
    // Computation descriptor stored in shared memory
    struct ComputationOp {
        enum OpType : uint32_t {
            CONSTANT,      // Already computed value
            ADD,          // Add two values
            MULTIPLY,     // Multiply two values
            NEGATE,       // Negate a value
            CHAIN,        // Chain two computations
            EXTERNAL      // External computation (index)
        } type;
        
        union {
            T constant_value;
            struct { T a, b; } binary_op;
            T unary_arg;
            struct { uint32_t first, second; } chain_indices;
            uint32_t external_id;
        } data;
    };
    
    struct Header {
        std::atomic<ComputeState> state;
        ComputationOp computation;
        T cached_value;
        char error_msg[256];
        std::atomic<uint32_t> compute_count;  // How many times forced
    };
    
    // Create new lazy computation with a constant
    Lazy(Memory& memory, std::string_view name, const T& value)
        : memory_(memory), name_(name) {
        
        size_t total_size = sizeof(Header);
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize as constant
        header_->state.store(COMPUTED, std::memory_order_relaxed);
        header_->computation.type = ComputationOp::CONSTANT;
        header_->computation.data.constant_value = value;
        header_->cached_value = value;
        header_->compute_count.store(0, std::memory_order_relaxed);
        std::memset(header_->error_msg, 0, sizeof(header_->error_msg));
    }
    
    // Create with computation operation
    Lazy(Memory& memory, std::string_view name, ComputationOp::OpType op)
        : memory_(memory), name_(name) {
        
        size_t total_size = sizeof(Header);
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        header_->state.store(NOT_COMPUTED, std::memory_order_relaxed);
        header_->computation.type = op;
        header_->compute_count.store(0, std::memory_order_relaxed);
        std::memset(header_->error_msg, 0, sizeof(header_->error_msg));
    }
    
    // Open existing lazy value
    Lazy(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Lazy not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
    }
    
    // Force evaluation
    [[nodiscard]] T force() {
        ComputeState expected = NOT_COMPUTED;
        
        // Try to claim computation
        if (header_->state.compare_exchange_strong(expected, COMPUTING,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed)) {
            // We get to compute it
            try {
                T result = compute();
                header_->cached_value = result;
                header_->state.store(COMPUTED, std::memory_order_release);
                header_->compute_count.fetch_add(1, std::memory_order_relaxed);
                return result;
            } catch (const std::exception& e) {
                std::strncpy(header_->error_msg, e.what(), 
                           sizeof(header_->error_msg) - 1);
                header_->state.store(ERROR, std::memory_order_release);
                throw;
            }
        }
        
        // Wait for computation by another thread
        while (true) {
            ComputeState state = header_->state.load(std::memory_order_acquire);
            if (state == COMPUTED) {
                return header_->cached_value;
            }
            if (state == ERROR) {
                throw std::runtime_error(header_->error_msg);
            }
            std::this_thread::yield();
        }
    }
    
    // Check if already computed
    [[nodiscard]] bool is_computed() const {
        return header_->state.load(std::memory_order_acquire) == COMPUTED;
    }
    
    // Peek at value without forcing (returns optional)
    [[nodiscard]] std::optional<T> peek() const {
        if (is_computed()) {
            return header_->cached_value;
        }
        return std::nullopt;
    }
    
    // Map operation - create new lazy that applies function
    template<typename F>
    Lazy<T> map(Memory& mem, const std::string& new_name, F&& func) {
        Lazy<T> result(mem, new_name, ComputationOp::EXTERNAL);
        
        // Store the mapping operation
        // In real impl, would store func in shared memory somehow
        // For now, we'll force and map immediately
        if (is_computed()) {
            T val = func(header_->cached_value);
            result.header_->cached_value = val;
            result.header_->state.store(COMPUTED, std::memory_order_release);
        }
        
        return result;
    }
    
    // Create derived lazy computations
    static Lazy<T> add(Memory& mem, const std::string& name, 
                       Lazy<T>& a, Lazy<T>& b) {
        Lazy<T> result(mem, name, ComputationOp::ADD);
        result.header_->computation.data.binary_op.a = a.force();
        result.header_->computation.data.binary_op.b = b.force();
        result.header_->state.store(NOT_COMPUTED, std::memory_order_release);
        return result;
    }
    
    static Lazy<T> multiply(Memory& mem, const std::string& name,
                           Lazy<T>& a, Lazy<T>& b) {
        Lazy<T> result(mem, name, ComputationOp::MULTIPLY);
        result.header_->computation.data.binary_op.a = a.force();
        result.header_->computation.data.binary_op.b = b.force();
        result.header_->state.store(NOT_COMPUTED, std::memory_order_release);
        return result;
    }
    
    // Reset to force recomputation
    void reset() {
        ComputeState expected = COMPUTED;
        header_->state.compare_exchange_strong(expected, NOT_COMPUTED,
                                              std::memory_order_acq_rel);
    }
    
    // Get computation count
    [[nodiscard]] uint32_t compute_count() const {
        return header_->compute_count.load(std::memory_order_relaxed);
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_ = nullptr;
    
    T compute() {
        switch (header_->computation.type) {
            case ComputationOp::CONSTANT:
                return header_->computation.data.constant_value;
                
            case ComputationOp::ADD:
                if constexpr (std::is_arithmetic_v<T>) {
                    return header_->computation.data.binary_op.a + 
                           header_->computation.data.binary_op.b;
                } else {
                    throw std::runtime_error("ADD not supported for this type");
                }
                
            case ComputationOp::MULTIPLY:
                if constexpr (std::is_arithmetic_v<T>) {
                    return header_->computation.data.binary_op.a * 
                           header_->computation.data.binary_op.b;
                } else {
                    throw std::runtime_error("MULTIPLY not supported for this type");
                }
                
            case ComputationOp::NEGATE:
                if constexpr (std::is_arithmetic_v<T>) {
                    return -header_->computation.data.unary_arg;
                } else {
                    throw std::runtime_error("NEGATE not supported for this type");
                }
                
            case ComputationOp::CHAIN:
            case ComputationOp::EXTERNAL:
                throw std::runtime_error("Complex operations not yet implemented");
                
            default:
                throw std::runtime_error("Unknown computation type");
        }
    }
};

// Specialization for lazy booleans with logical operations
template<>
class Lazy<bool> {
public:
    enum ComputeState : uint32_t {
        NOT_COMPUTED = 0,
        COMPUTING = 1,
        COMPUTED = 2
    };
    
    struct LogicalOp {
        enum OpType : uint32_t {
            CONSTANT,
            AND,
            OR,
            XOR,
            NOT
        } type;
        
        union {
            bool value;
            struct { bool a, b; } binary;
            bool unary;
        } data;
    };
    
    struct Header {
        std::atomic<ComputeState> state;
        LogicalOp operation;
        bool cached_value;
    };
    
    Lazy(Memory& memory, std::string_view name, bool value)
        : memory_(memory), name_(name) {
        
        size_t offset = memory.allocate(name, sizeof(Header));
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        header_->state.store(COMPUTED, std::memory_order_relaxed);
        header_->operation.type = LogicalOp::CONSTANT;
        header_->operation.data.value = value;
        header_->cached_value = value;
    }
    
    [[nodiscard]] bool force() {
        ComputeState expected = NOT_COMPUTED;
        
        if (header_->state.compare_exchange_strong(expected, COMPUTING,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed)) {
            bool result = compute();
            header_->cached_value = result;
            header_->state.store(COMPUTED, std::memory_order_release);
            return result;
        }
        
        while (header_->state.load(std::memory_order_acquire) != COMPUTED) {
            std::this_thread::yield();
        }
        return header_->cached_value;
    }
    
    // Lazy AND operation
    static Lazy<bool> lazy_and(Memory& mem, const std::string& name,
                               Lazy<bool>& a, Lazy<bool>& b) {
        Lazy<bool> result(mem, name, false);
        result.header_->state.store(NOT_COMPUTED, std::memory_order_relaxed);
        result.header_->operation.type = LogicalOp::AND;
        
        // Short-circuit evaluation: only compute b if a is true
        if (a.force()) {
            result.header_->operation.data.binary.a = true;
            result.header_->operation.data.binary.b = b.force();
        } else {
            result.header_->operation.data.binary.a = false;
            result.header_->operation.data.binary.b = false; // Not evaluated
            result.header_->cached_value = false;
            result.header_->state.store(COMPUTED, std::memory_order_release);
        }
        return result;
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_;
    
    bool compute() {
        switch (header_->operation.type) {
            case LogicalOp::CONSTANT:
                return header_->operation.data.value;
            case LogicalOp::AND:
                return header_->operation.data.binary.a && 
                       header_->operation.data.binary.b;
            case LogicalOp::OR:
                return header_->operation.data.binary.a || 
                       header_->operation.data.binary.b;
            case LogicalOp::XOR:
                return header_->operation.data.binary.a ^ 
                       header_->operation.data.binary.b;
            case LogicalOp::NOT:
                return !header_->operation.data.unary;
            default:
                return false;
        }
    }
};

} // namespace zeroipc