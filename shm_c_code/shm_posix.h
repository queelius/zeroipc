#pragma once
#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <array>
#include <iostream>

class shm_posix {
private:
    static const size_t MAX_NAME_SIZE = 32;
    static const size_t MAX_ENTRIES = 100;

    struct header {
        std::atomic<int> ref_count;
        size_t num_entries;

        struct row {
            std::array<char, MAX_NAME_SIZE> name;
            size_t offset;
            size_t size;
            size_t elem_size;
            size_t num_elem;
        } entries[MAX_ENTRIES];

        void inc_ref_count() {
            ref_count.fetch_add(1, std::memory_order_relaxed);
        }

        int dec_ref_count() {
            return ref_count.fetch_sub(1, std::memory_order_release) - 1;
        }
    };

    void* base_addr;
    size_t total_size;
    std::string name;
    int fd;
    header* hdr;

public:
    // Constructor for opening existing shared memory
    shm_posix(const std::string& name)
        : name(name) {
        fd = shm_open(name.c_str(), O_RDWR, 0666);
        if (fd == -1) {
            throw std::runtime_error("Failed to open existing shared memory: " + std::string(strerror(errno)));
        }

        base_addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base_addr == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("Failed to map shared memory: " + std::string(strerror(errno)));
        }

        hdr = static_cast<header*>(base_addr);
        hdr->inc_ref_count();

        total_size = lseek(fd, 0, SEEK_END); // Get the size of the existing shared memory

        std::cout << "Shared memory opened: " << name << "\n";
        std::cout << "Reference count: " << hdr->ref_count.load() << "\n";
    }

    // Constructor for creating new shared memory
    shm_posix(const std::string& name, size_t size)
        : name(name), total_size(size + sizeof(header)) {
        fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd == -1) {
            if (errno == EEXIST) {
                throw std::runtime_error("Shared memory already exists: " + std::string(strerror(errno)));
            } else {
                throw std::runtime_error("Failed to create shared memory: " + std::string(strerror(errno)));
            }
        }

        if (ftruncate(fd, total_size) == -1) {
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("Failed to set size of shared memory: " + std::string(strerror(errno)));
        }

        base_addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base_addr == MAP_FAILED) {
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("Failed to map shared memory: " + std::string(strerror(errno)));
        }

        hdr = static_cast<header*>(base_addr);
        new (hdr) header{1, 0};  // Initialize ref_count to 1 and num_entries to 0

        std::cout << "Shared memory created: " << name << "\n";
        std::cout << "Reference count: " << hdr->ref_count.load() << "\n";
    }

    ~shm_posix() {
        if (hdr->dec_ref_count() == 0) {
            try_unlink();
        }
        munmap(base_addr, total_size);
        close(fd);
    }

    void* get_base_addr() const {
        return static_cast<char*>(base_addr) + sizeof(header);
    }

    size_t get_total_size() const {
        return total_size - sizeof(header);
    }

    int get_ref_count() const {
        return hdr->ref_count.load(std::memory_order_acquire);
    }

    bool try_unlink() {
        if (get_ref_count() == 0) {
            if (shm_unlink(name.c_str()) == -1) {
                throw std::runtime_error("Failed to unlink shared memory: " + std::string(strerror(errno)));
            }
            return true;
        }
        return false;
    }

    bool push_back(const std::string& name, size_t offset, size_t size, size_t elem_size, size_t num_elem) {
        if (hdr->num_entries >= MAX_ENTRIES) return false;
        auto& entry = hdr->entries[hdr->num_entries++];
        strncpy(entry.name.data(), name.c_str(), MAX_NAME_SIZE);
        entry.offset = offset;
        entry.size = size;
        entry.elem_size = elem_size;
        entry.num_elem = num_elem;
        std::cout << "Entry added: " << name << "\n";
        std::cout << "Number of entries: " << hdr->num_entries << "\n";
        return true;
    }

    size_t size() const {
        return hdr->num_entries;
    }

    typename header::row* find(const std::string& name) {
        for (size_t i = 0; i < hdr->num_entries; ++i) {
            if (strcmp(name.c_str(), hdr->entries[i].name.data()) == 0) return &hdr->entries[i];
        }
        return nullptr;
    }

    shm_posix(const shm_posix&) = delete;
    shm_posix& operator=(const shm_posix&) = delete;
};
