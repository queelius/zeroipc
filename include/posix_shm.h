#pragma once
#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <atomic>

class posix_shm {
private:
    struct header {
        std::atomic<int> ref_count;
    };

    void* base_addr;
    size_t total_size;
    std::string name;
    int fd;
    header* hdr;

    void inc_ref_count() {
        header->ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    int dec_ref_count() {
        return header->ref_count.fetch_sub(1, std::memory_order_release) - 1;
    }

public:
    posix_shm(const std::string& name, size_t size)
        : name(name), total_size(size + sizeof(header)) {

        bool created = false;
        fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd == -1) {
            if (errno == EEXIST) {
                fd = shm_open(name.c_str(), O_RDWR, 0666);
                if (fd == -1) {
                    throw std::runtime_error("Failed to open existing shared memory: " + std::string(strerror(errno)));
                }
            } else {
                throw std::runtime_error("Failed to create shared memory: " + std::string(strerror(errno)));
            }
        } else {
            created = true;
        }

        if (created && ftruncate(fd, total_size) == -1) {
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("Failed to set size of shared memory: " + std::string(strerror(errno)));
        }

        base_addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base_addr == MAP_FAILED) {
            close(fd);
            if (created) shm_unlink(name.c_str());
            throw std::runtime_error("Failed to map shared memory: " + std::string(strerror(errno)));
        }

        hdr = static_cast<SharedHeader*>(base_addr);
        if (created) {
            new (hdr) header{1};  // Initialize ref_count to 1
        } else {
            inc_ref_count();
        }
    }

    ~posix_shm() {
        if (dec_ref_count() == 0) {
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
        return header->ref_count.load(std::memory_order_acquire);
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

    posix_shm(const posix_shm&) = delete;
    posix_shm& operator=(const posix_shm&) = delete;
};
