#pragma once
#include "posix_shm.h"
#include "shm_table.h"
#include <atomic>
#include <span>
#include <algorithm>

/**
 * @brief Lock-free ring buffer for high-throughput streaming data
 * 
 * Optimized for bulk operations and sensor data streams.
 * Supports multiple readers that can read without consuming.
 * 
 * Better than queue when you need:
 * - Bulk read/write operations
 * - Non-consuming reads (peeking at data)
 * - Historical data access (last N samples)
 * 
 * @tparam T Element type
 * @tparam TableType Metadata table type
 */
template<typename T, typename TableType = shm_table>
    requires std::is_trivially_copyable_v<T>
class shm_ring_buffer {
private:
    struct RingHeader {
        std::atomic<uint64_t> write_pos{0};  // Total elements written (never wraps)
        std::atomic<uint64_t> read_pos{0};   // Total elements read (never wraps)
        uint32_t capacity;
        uint32_t _padding;  // Align to 8 bytes
        // Data follows immediately after header
    };

    RingHeader* header_{nullptr};
    T* data_{nullptr};
    const typename TableType::entry* table_entry_{nullptr};

    // Convert absolute position to ring buffer index
    [[nodiscard]] uint32_t to_index(uint64_t pos) const noexcept {
        return pos % header_->capacity;
    }

public:
    using value_type = T;
    using size_type = size_t;

    template<typename ShmType>
    shm_ring_buffer(ShmType& shm, std::string_view name, size_t capacity = 0) {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match ring buffer table type");

        auto* table = static_cast<TableType*>(shm.get_base_addr());
        
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto* entry = table->find(name_buf);
        
        if (entry) {
            // Open existing ring buffer
            header_ = reinterpret_cast<RingHeader*>(
                static_cast<char*>(shm.get_base_addr()) + entry->offset
            );
            data_ = reinterpret_cast<T*>(header_ + 1);
            table_entry_ = entry;
        } else if (capacity > 0) {
            // Create new ring buffer
            size_t total_size = sizeof(RingHeader) + sizeof(T) * capacity;
            size_t table_size = sizeof(TableType);
            size_t current_used = table->get_total_allocated_size();
            size_t offset = table_size + current_used;
            
            header_ = reinterpret_cast<RingHeader*>(
                static_cast<char*>(shm.get_base_addr()) + offset
            );
            
            // Initialize header
            new (header_) RingHeader();
            header_->capacity = static_cast<uint32_t>(capacity);
            
            data_ = reinterpret_cast<T*>(header_ + 1);
            
            // Register in table
            if (!table->add(name_buf, offset, total_size, sizeof(T), capacity)) {
                throw std::runtime_error("Failed to add ring buffer to table");
            }
            table_entry_ = table->find(name_buf);
        } else {
            throw std::runtime_error("Ring buffer not found and capacity not specified");
        }
    }

    /**
     * @brief Push a single element
     * @return true if successful, false if buffer is full
     */
    [[nodiscard]] bool push(const T& value) noexcept {
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        
        if (write - read >= header_->capacity) {
            return false;  // Buffer full
        }
        
        data_[to_index(write)] = value;
        header_->write_pos.store(write + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Push multiple elements efficiently
     * @return Number of elements actually pushed
     */
    size_t push_bulk(std::span<const T> values) noexcept {
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        
        size_t available = header_->capacity - (write - read);
        size_t to_write = std::min(values.size(), available);
        
        for (size_t i = 0; i < to_write; ++i) {
            data_[to_index(write + i)] = values[i];
        }
        
        header_->write_pos.store(write + to_write, std::memory_order_release);
        return to_write;
    }

    /**
     * @brief Pop a single element
     * @return Optional containing value if available
     */
    [[nodiscard]] std::optional<T> pop() noexcept {
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        
        if (read >= write) {
            return std::nullopt;  // Empty
        }
        
        T value = data_[to_index(read)];
        header_->read_pos.store(read + 1, std::memory_order_release);
        return value;
    }

    /**
     * @brief Pop multiple elements efficiently
     * @param[out] values Buffer to store popped elements
     * @return Number of elements actually popped
     */
    size_t pop_bulk(std::span<T> values) noexcept {
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        
        size_t available = write - read;
        size_t to_read = std::min(values.size(), available);
        
        for (size_t i = 0; i < to_read; ++i) {
            values[i] = data_[to_index(read + i)];
        }
        
        header_->read_pos.store(read + to_read, std::memory_order_release);
        return to_read;
    }

    /**
     * @brief Peek at elements without consuming them
     * @param offset How many elements to skip from the front
     * @param[out] values Buffer to store peeked elements
     * @return Number of elements actually peeked
     */
    size_t peek_bulk(size_t offset, std::span<T> values) const noexcept {
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        
        if (read + offset >= write) {
            return 0;  // Offset beyond available data
        }
        
        size_t available = write - (read + offset);
        size_t to_peek = std::min(values.size(), available);
        
        for (size_t i = 0; i < to_peek; ++i) {
            values[i] = data_[to_index(read + offset + i)];
        }
        
        return to_peek;
    }

    /**
     * @brief Get the last N elements (most recent)
     * Useful for getting trailing sensor data
     */
    size_t get_last_n(size_t n, std::span<T> values) const noexcept {
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        
        size_t available = write - read;
        size_t to_get = std::min({n, values.size(), available});
        
        uint64_t start = write - to_get;
        for (size_t i = 0; i < to_get; ++i) {
            values[i] = data_[to_index(start + i)];
        }
        
        return to_get;
    }

    /**
     * @brief Skip elements without reading them
     */
    void skip(size_t count) noexcept {
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        
        size_t available = write - read;
        size_t to_skip = std::min(count, available);
        
        header_->read_pos.store(read + to_skip, std::memory_order_release);
    }

    /**
     * @brief Clear the buffer (reset read/write positions)
     */
    void clear() noexcept {
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        header_->read_pos.store(write, std::memory_order_release);
    }

    // Capacity and size queries
    [[nodiscard]] size_t capacity() const noexcept {
        return header_->capacity;
    }

    [[nodiscard]] size_t size() const noexcept {
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        return write - read;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] bool full() const noexcept {
        return size() == capacity();
    }

    [[nodiscard]] size_t available_space() const noexcept {
        return capacity() - size();
    }

    /**
     * @brief Get total elements written (useful for monitoring data rate)
     */
    [[nodiscard]] uint64_t total_written() const noexcept {
        return header_->write_pos.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t total_read() const noexcept {
        return header_->read_pos.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return table_entry_ ? std::string_view(table_entry_->name.data()) : std::string_view{};
    }

    /**
     * @brief Force overwrite when full (converts to circular overwrite mode)
     * Useful for continuous sensor streams where latest data is most important
     */
    void push_overwrite(const T& value) noexcept {
        uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        uint64_t read = header_->read_pos.load(std::memory_order_acquire);
        
        data_[to_index(write)] = value;
        
        // If full, advance read pointer (lose oldest data)
        if (write - read >= header_->capacity) {
            header_->read_pos.store(read + 1, std::memory_order_release);
        }
        
        header_->write_pos.store(write + 1, std::memory_order_release);
    }
};