#pragma once
#include "posix_shm.h"
#include "shm_span.h"
#include "shm_table.h"
#include <stdexcept>
#include <algorithm>

/**
 * @class shm_array
 * @brief A shared memory array implementation using POSIX shared memory.
 *
 * This class provides an array-like interface for data stored in POSIX shared memory.
 * It allows for dynamic creation and discovery of arrays in shared memory, enabling
 * efficient inter-process communication.
 *
 * @tparam T The type of elements stored in the array.
 */
template <typename T>
class shm_array : public shm_span<T>
{
public:
    /**
     * @brief Construct a new shared memory array or open an existing one.
     *
     * @param shared_mem Reference to the posix_shm object managing the shared memory.
     * @param name The name of the array for identification in shared memory.
     * @param count The number of elements in the array. If 0, attempts to open an existing array.
     * @throw std::runtime_error if the array cannot be created or opened.
     */
    shm_array(posix_shm &shared_mem, const std::string &name, size_t count = 0)
        : shm_span<T>(shm, 0, 0)
    {
        initialize(name, count);
    }

    void initialize(const std::string &name, size_t count = 0)
    {
        auto *table = static_cast<shm_table *>(this->shm.get_base_addr());
        auto *entry = table->find(name);

        if (entry)
        {
            if (count != 0 && entry->num_elem != count)
            {
                throw std::runtime_error("Mismatch in array size");
            }
            this->offset = entry->offset;
            this->num_elem = entry->num_elem;
        }
        else if (count > 0)
        {
            size_t required_size = count * sizeof(T);
            size_t available_size = this->shm.get_total_size() - sizeof(shm_table);
            if (required_size > available_size)
            {
                throw std::runtime_error("Not enough space in shared memory");
            }
            this->offset = sizeof(shm_table) + table->get_total_allocated_size();
            this->num_elem = count;
            table->add(name, this->offset, required_size, sizeof(T), count);
        }
        else
        {
            throw std::runtime_error("Array not found and size not specified");
        }
        _name = name;
    }

    T &at(size_t pos)
    {
        if (pos >= this->num_elem)
        {
            throw std::out_of_range("Array index out of range");
        }
        return (*this)[pos];
    }

    const T &at(size_t pos) const
    {
        if (pos >= this->num_elem)
        {
            throw std::out_of_range("Array index out of range");
        }
        return (*this)[pos];
    }

    T &front() { return *this->data(); }
    const T &front() const { return *this->data(); }

    T &back() { return *(this->data() + this->num_elem - 1); }
    const T &back() const { return *(this->data() + this->num_elem - 1); }

    bool empty() const { return this->num_elem == 0; }
    size_t size() const { return this->num_elem; }

    void fill(const T &value)
    {
        std::fill_n(this->data(), this->num_elem, value);
    }

    T *begin() { return this->data(); }
    const T *begin() const { return this->data(); }
    T *end() { return this->data() + this->num_elem; }
    const T *end() const { return this->data() + this->num_elem; }

    const std::string &name() const { return self._name; }

private:
    std::string _name;
};
