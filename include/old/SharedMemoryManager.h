#ifndef SHARED_MEMORY_MANAGER_H
#define SHARED_MEMORY_MANAGER_H

#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

class SharedMemoryManager {
private:
    void* data_addr;
    void* metadata_addr;
    size_t data_size;
    size_t metadata_size;
    int data_fd;
    int metadata_fd;
    std::string data_name;
    std::string metadata_name;
    bool owner;

    void create_or_open_segment(const std::string& name, size_t size, void*& addr, int& fd, bool create) {
        printf("%s: Attempting to %s shared memory '%s'\n", 
               create ? "Parent" : "Child", create ? "create" : "open", name.c_str());

        fd = shm_open(name.c_str(), create ? (O_CREAT | O_RDWR) : O_RDWR, 0666);
        if (fd == -1) {
            perror(create ? "shm_open create failed" : "shm_open open failed");
            throw std::runtime_error("Failed to " + std::string(create ? "create" : "open") + " shared memory");
        }

        if (create && ftruncate(fd, size) == -1) {
            perror("ftruncate failed");
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("Failed to set size of shared memory");
        }

        addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
            perror("mmap failed");
            close(fd);
            if (create) shm_unlink(name.c_str());
            throw std::runtime_error("Failed to map shared memory");
        }

        printf("%s: Mapped shared memory '%s' at address %p\n", 
               create ? "Parent" : "Child", name.c_str(), addr);
    }

public:
    SharedMemoryManager(const std::string& name, size_t size, bool create = true)
        : data_size(size), metadata_size(1024), owner(create) {
        data_name = name;
        metadata_name = name + ".metadata";

        create_or_open_segment(data_name, data_size, data_addr, data_fd, create);
        create_or_open_segment(metadata_name, metadata_size, metadata_addr, metadata_fd, create);
    }

    ~SharedMemoryManager() {
        munmap(data_addr, data_size);
        munmap(metadata_addr, metadata_size);
        close(data_fd);
        close(metadata_fd);
        if (owner) {
            shm_unlink(data_name.c_str());
            shm_unlink(metadata_name.c_str());
        }
    }

    void* get_data_addr() const { return data_addr; }
    void* get_metadata_addr() const { return metadata_addr; }
    size_t get_data_size() const { return data_size; }
    size_t get_metadata_size() const { return metadata_size; }
    bool is_owner() const { return owner; }

    // Prevent copying
    SharedMemoryManager(const SharedMemoryManager&) = delete;
    SharedMemoryManager& operator=(const SharedMemoryManager&) = delete;
};

#endif // SHARED_MEMORY_MANAGER_H