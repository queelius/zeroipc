/**
 * @file shm_bitset.h
 * @brief Lock-free bitset implementation for POSIX shared memory
 * @author Alex Towell
 * 
 * @details
 * Provides a compact bit array that can be shared across processes.
 * Uses atomic operations for thread-safe bit manipulation.
 * Ideal for flags, bloom filters, and compact boolean arrays.
 * 
 * @par Thread Safety
 * Individual bit operations are atomic. Bulk operations may see inconsistent state.
 * 
 * @par Example
 * @code
 * posix_shm shm("simulation", 10 * 1024 * 1024);
 * shm_bitset<1000000> active_flags(shm, "active");
 * 
 * // Set bits
 * active_flags.set(42);
 * active_flags.set(100, false);  // Clear
 * 
 * // Test bits
 * if (active_flags.test(42)) {
 *     process_active(42);
 * }
 * @endcode
 */

#pragma once

#include "posix_shm.h"
#include "shm_table.h"
#include "shm_span.h"
#include <atomic>
#include <cstring>
#include <bit>
#include <immintrin.h>  // For popcnt

/**
 * @brief Lock-free bitset for shared memory
 * 
 * @tparam N Number of bits
 * @tparam TableType Table implementation for metadata
 * 
 * @details
 * Stores N bits compactly using atomic operations on 64-bit words.
 * Provides O(1) bit access and O(N/64) bulk operations.
 */
template<size_t N, typename TableType = shm_table>
class shm_bitset : public shm_span<uint64_t, posix_shm_impl<TableType>> {
private:
    static constexpr size_t BITS_PER_WORD = 64;
    static constexpr size_t NUM_WORDS = (N + BITS_PER_WORD - 1) / BITS_PER_WORD;
    
    struct BitsetHeader {
        size_t bit_count;
        std::atomic<size_t> set_count{0};  // Number of set bits (cached)
    };

    BitsetHeader* header() {
        return reinterpret_cast<BitsetHeader*>(
            static_cast<char*>(this->shm.get_base_addr()) + this->offset
        );
    }

    const BitsetHeader* header() const {
        return reinterpret_cast<const BitsetHeader*>(
            static_cast<const char*>(this->shm.get_base_addr()) + this->offset
        );
    }

    std::atomic<uint64_t>* words() {
        return reinterpret_cast<std::atomic<uint64_t>*>(
            reinterpret_cast<char*>(header()) + sizeof(BitsetHeader)
        );
    }

    const std::atomic<uint64_t>* words() const {
        return reinterpret_cast<const std::atomic<uint64_t>*>(
            reinterpret_cast<const char*>(header()) + sizeof(BitsetHeader)
        );
    }

    const typename TableType::entry* _table_entry{nullptr};

public:
    /**
     * @brief Check if a bitset exists in shared memory
     */
    template<typename ShmType>
    static bool exists(ShmType& shm, std::string_view name) {
        auto* table = static_cast<TableType*>(shm.get_base_addr());
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        return table->find(name_buf) != nullptr;
    }

    /**
     * @brief Create or open a shared memory bitset
     */
    template<typename ShmType>
    shm_bitset(ShmType& shm, std::string_view name)
        : shm_span<uint64_t, posix_shm_impl<TableType>>(shm, 0, 0) {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match bitset table type");
        
        auto* table = static_cast<TableType*>(shm.get_base_addr());
        
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto* entry = table->find(name_buf);
        
        if (entry) {
            // Open existing bitset
            this->offset = entry->offset;
            this->num_elem = entry->num_elem;
            _table_entry = entry;
        } else {
            // Create new bitset
            size_t required_size = sizeof(BitsetHeader) + NUM_WORDS * sizeof(uint64_t);
            size_t current_used = table->get_total_allocated_size();
            
            this->offset = sizeof(TableType) + current_used;
            this->num_elem = NUM_WORDS;
            
            // Initialize header
            auto* hdr = header();
            new (hdr) BitsetHeader{};
            hdr->bit_count = N;
            hdr->set_count.store(0, std::memory_order_relaxed);
            
            // Initialize words to zero
            auto* w = words();
            for (size_t i = 0; i < NUM_WORDS; ++i) {
                new (&w[i]) std::atomic<uint64_t>(0);
            }
            
            // Register in table
            table->add(name_buf, this->offset, required_size, sizeof(uint64_t), NUM_WORDS);
            _table_entry = table->find(name_buf);
        }
    }

    /**
     * @brief Set a bit to true
     */
    void set(size_t pos) noexcept {
        if (pos >= N) return;
        
        size_t word_idx = pos / BITS_PER_WORD;
        size_t bit_idx = pos % BITS_PER_WORD;
        uint64_t mask = uint64_t(1) << bit_idx;
        
        auto& word = words()[word_idx];
        uint64_t old_val = word.fetch_or(mask, std::memory_order_acq_rel);
        
        // Update count if bit was not already set
        if ((old_val & mask) == 0) {
            header()->set_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Set a bit to specified value
     */
    void set(size_t pos, bool value) noexcept {
        if (value) {
            set(pos);
        } else {
            reset(pos);
        }
    }

    /**
     * @brief Clear a bit (set to false)
     */
    void reset(size_t pos) noexcept {
        if (pos >= N) return;
        
        size_t word_idx = pos / BITS_PER_WORD;
        size_t bit_idx = pos % BITS_PER_WORD;
        uint64_t mask = uint64_t(1) << bit_idx;
        
        auto& word = words()[word_idx];
        uint64_t old_val = word.fetch_and(~mask, std::memory_order_acq_rel);
        
        // Update count if bit was set
        if ((old_val & mask) != 0) {
            header()->set_count.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Flip a bit
     */
    void flip(size_t pos) noexcept {
        if (pos >= N) return;
        
        size_t word_idx = pos / BITS_PER_WORD;
        size_t bit_idx = pos % BITS_PER_WORD;
        uint64_t mask = uint64_t(1) << bit_idx;
        
        auto& word = words()[word_idx];
        uint64_t old_val = word.fetch_xor(mask, std::memory_order_acq_rel);
        
        // Update count based on old value
        if ((old_val & mask) == 0) {
            header()->set_count.fetch_add(1, std::memory_order_relaxed);
        } else {
            header()->set_count.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Test if a bit is set
     */
    [[nodiscard]] bool test(size_t pos) const noexcept {
        if (pos >= N) return false;
        
        size_t word_idx = pos / BITS_PER_WORD;
        size_t bit_idx = pos % BITS_PER_WORD;
        uint64_t mask = uint64_t(1) << bit_idx;
        
        return (words()[word_idx].load(std::memory_order_acquire) & mask) != 0;
    }

    /**
     * @brief Set all bits to true
     */
    void set() noexcept {
        auto* w = words();
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            w[i].store(~uint64_t(0), std::memory_order_relaxed);
        }
        
        // Handle last word if N is not multiple of 64
        if (N % BITS_PER_WORD != 0) {
            uint64_t last_mask = (uint64_t(1) << (N % BITS_PER_WORD)) - 1;
            w[NUM_WORDS - 1].store(last_mask, std::memory_order_relaxed);
        }
        
        header()->set_count.store(N, std::memory_order_release);
    }

    /**
     * @brief Clear all bits (set to false)
     */
    void reset() noexcept {
        auto* w = words();
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            w[i].store(0, std::memory_order_relaxed);
        }
        header()->set_count.store(0, std::memory_order_release);
    }

    /**
     * @brief Flip all bits
     */
    void flip() noexcept {
        auto* w = words();
        size_t new_count = 0;
        
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            uint64_t old_val = w[i].load(std::memory_order_relaxed);
            uint64_t new_val = ~old_val;
            
            // Handle last word
            if (i == NUM_WORDS - 1 && N % BITS_PER_WORD != 0) {
                uint64_t last_mask = (uint64_t(1) << (N % BITS_PER_WORD)) - 1;
                new_val &= last_mask;
            }
            
            w[i].store(new_val, std::memory_order_relaxed);
            new_count += std::popcount(new_val);
        }
        
        header()->set_count.store(new_count, std::memory_order_release);
    }

    /**
     * @brief Count number of set bits
     */
    [[nodiscard]] size_t count() const noexcept {
        // Use cached count for approximate value
        return header()->set_count.load(std::memory_order_acquire);
    }

    /**
     * @brief Recount bits (accurate but not atomic with modifications)
     */
    [[nodiscard]] size_t count_accurate() const noexcept {
        auto* w = words();
        size_t count = 0;
        
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            uint64_t word = w[i].load(std::memory_order_relaxed);
            count += std::popcount(word);
        }
        
        return count;
    }

    /**
     * @brief Check if all bits are set
     */
    [[nodiscard]] bool all() const noexcept {
        return count() == N;
    }

    /**
     * @brief Check if any bit is set
     */
    [[nodiscard]] bool any() const noexcept {
        return count() > 0;
    }

    /**
     * @brief Check if no bits are set
     */
    [[nodiscard]] bool none() const noexcept {
        return count() == 0;
    }

    /**
     * @brief Get total number of bits
     */
    [[nodiscard]] constexpr size_t size() const noexcept {
        return N;
    }

    /**
     * @brief Find first set bit
     * @return Position of first set bit, or N if none
     */
    [[nodiscard]] size_t find_first() const noexcept {
        auto* w = words();
        
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            uint64_t word = w[i].load(std::memory_order_acquire);
            if (word != 0) {
                size_t bit_pos = __builtin_ctzll(word);  // Count trailing zeros
                size_t result = i * BITS_PER_WORD + bit_pos;
                return (result < N) ? result : N;
            }
        }
        
        return N;  // No bit set
    }

    /**
     * @brief Find next set bit after position
     */
    [[nodiscard]] size_t find_next(size_t pos) const noexcept {
        if (pos >= N) return N;
        
        pos++;  // Start from next position
        size_t word_idx = pos / BITS_PER_WORD;
        size_t bit_idx = pos % BITS_PER_WORD;
        
        auto* w = words();
        
        // Check remainder of current word
        if (bit_idx != 0) {
            uint64_t word = w[word_idx].load(std::memory_order_acquire);
            word &= ~((uint64_t(1) << bit_idx) - 1);  // Mask off lower bits
            
            if (word != 0) {
                size_t bit_pos = __builtin_ctzll(word);
                size_t result = word_idx * BITS_PER_WORD + bit_pos;
                return (result < N) ? result : N;
            }
            
            word_idx++;
        }
        
        // Check remaining words
        for (size_t i = word_idx; i < NUM_WORDS; ++i) {
            uint64_t word = w[i].load(std::memory_order_acquire);
            if (word != 0) {
                size_t bit_pos = __builtin_ctzll(word);
                size_t result = i * BITS_PER_WORD + bit_pos;
                return (result < N) ? result : N;
            }
        }
        
        return N;
    }

    /**
     * @brief Bitwise AND with another bitset
     */
    shm_bitset& operator&=(const shm_bitset& other) noexcept {
        auto* w = words();
        auto* other_w = other.words();
        size_t new_count = 0;
        
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            uint64_t result = w[i].load() & other_w[i].load();
            w[i].store(result, std::memory_order_relaxed);
            new_count += std::popcount(result);
        }
        
        header()->set_count.store(new_count, std::memory_order_release);
        return *this;
    }

    /**
     * @brief Bitwise OR with another bitset
     */
    shm_bitset& operator|=(const shm_bitset& other) noexcept {
        auto* w = words();
        auto* other_w = other.words();
        size_t new_count = 0;
        
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            uint64_t result = w[i].load() | other_w[i].load();
            w[i].store(result, std::memory_order_relaxed);
            new_count += std::popcount(result);
        }
        
        header()->set_count.store(new_count, std::memory_order_release);
        return *this;
    }

    /**
     * @brief Bitwise XOR with another bitset
     */
    shm_bitset& operator^=(const shm_bitset& other) noexcept {
        auto* w = words();
        auto* other_w = other.words();
        size_t new_count = 0;
        
        for (size_t i = 0; i < NUM_WORDS; ++i) {
            uint64_t result = w[i].load() ^ other_w[i].load();
            w[i].store(result, std::memory_order_relaxed);
            new_count += std::popcount(result);
        }
        
        header()->set_count.store(new_count, std::memory_order_release);
        return *this;
    }

    /**
     * @brief Get name of the bitset
     */
    std::string_view name() const {
        return _table_entry ? std::string_view(_table_entry->name.data()) : std::string_view{};
    }
};

// Common bitset sizes
using shm_bitset_64 = shm_bitset<64>;
using shm_bitset_256 = shm_bitset<256>;
using shm_bitset_1024 = shm_bitset<1024>;
using shm_bitset_4096 = shm_bitset<4096>;