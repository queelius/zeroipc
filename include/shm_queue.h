#pragma once
#include "shm_span.h"
#include "shm_table.h"

template<typename T>
class shm_queue : public shm_span<T> {
    size_t head;
    size_t tail;

public:
    /**
     * @brief Construct a new shm queue object in shared memory.
     * 
     * @param shm The shared memory object.
     * @param count Maximum number of elements in the queue.
     * @param name 
     */
    shm_queue(posix_shm& shm, size_t size, const std::string& name)
        : shm_span<T>(shm, 0, size), head(0), tail(0) {
        auto* table = static_cast<shm_table*>(shm.get_base_addr());
        auto* entry = table->find(name);
        if (entry) {
            if (entry->num_elements != count) {
                throw std::runtime_error("Mismatch in queue size");
            }
            this->offset = entry->offset;
        } else {
            this->offset = shm.get_total_size() - size() * sizeof(T);
            table->add_entry(name, this->offset, size() * sizeof(T), sizeof(T), size());
        }
    }

    shm_queue(posix_shm& shm, const std::string& name)
        : shm_span<T>(shm, 0, 0), head(0), tail(0) {
        auto* table = static_cast<shm_table*>(shm.get_base_addr());
        auto* entry = table->find_entry(name);
        if (!entry) {
            throw std::runtime_error("Queue not found");
        }
        this->offset = entry->offset;
        this->num_elem = entry->num_elem;
    }

    /**
     * @brief Push an element into the queue.
     * 
     * @param elem Element to be pushed.
     * @throw std::runtime_error if the queue is full.
     */
    void push(const T& elem) {
        if (full()) {
            throw std::runtime_error("Queue is full");
        }
        (*this)[tail] = elem;
        if (++tail == this->num_elem) {
            tail = 0;
        }
    }

    /**
     * @brief Pop an element from the queue.
     * 
     * @return T Element popped from the queue.
     * @throw std::runtime_error if the queue is empty.
     */
    T pop() {
        if (empty()) {
            throw std::runtime_error("Queue is empty");
        }
        T elem = (*this)[head];
        if (++head == capacity()) {
            head = 0;
        }
        return elem;
    }

    /**
     * @brief Check if the queue is empty.
     * 
     * @return true if the queue is empty.
     */
    bool empty() const {
        return head == tail;
    }

    /**
     * @brief Check if the queue is full.
     * 
     * @return true if the queue is full.
     */
    bool full() const {
        return (tail + 1) % capacity() == head;
    }

    /**
     * @brief Get the number of elements in the queue.
     * 
     * @return size_t Number of elements in the queue.
     */
    size_t size() const {
        return (tail + capacity() - head) % capacity();
};
