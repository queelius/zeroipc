#pragma once

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <string>

const char* SHM_POOL_NAME = "/shm_pool";
const char* SEM_POOL_NAME = "/sem_pool";
const size_t POOL_SIZE = 1024 * 1024; // 1 MB for example

class ShmPool {
public:
    ShmPool(size_t pool_size, std::string const & shm_name) :
        shm_name(shm_name), pool_size(pool_size), shm_fd(-1), sem(nullptr), pool_ptr(nullptr) {

        initialize_shm();
        initialize_sem();
    }

    ~ShmPool() {
        cleanup();
    }

    void* allocate(size_t size) {
        sem_wait(sem); // Lock
        if (free_list == nullptr) {
            sem_post(sem); // Unlock
            throw std::bad_alloc();
        }
        Node* node = free_list;
        free_list = node->next;
        sem_post(sem); // Unlock
        return node;
    }

    void deallocate(void* ptr) {
        sem_wait(sem); // Lock
        Node* node = static_cast<Node*>(ptr);
        node->next = free_list;
        free_list = node;
        sem_post(sem); // Unlock
    }

private:
    struct Node {
        Node* next;
    };

    std::string shm_name;
    size_t pool_size;
    int shm_fd;
    sem_t* sem;
    void* pool_ptr;
    Node* free_list;

    void initialize_shm() {
        shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            perror("shm_open");
            throw std::runtime_error("Failed to open shared memory");
        }

        if (ftruncate(shm_fd, pool_size) == -1) {
            perror("ftruncate");
            throw std::runtime_error("Failed to set shared memory size");
        }

        pool_ptr = mmap(nullptr, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (pool_ptr == MAP_FAILED) {
            perror("mmap");
            throw std::runtime_error("Failed to map shared memory");
        }

        // Initialize the free list
        free_list = static_cast<Node*>(pool_ptr);
        Node* current = free_list;
        for (size_t i = sizeof(Node); i < pool_size; i += sizeof(Node)) {
            current->next = reinterpret_cast<Node*>(reinterpret_cast<char*>(pool_ptr) + i);
            current = current->next;
        }
        current->next = nullptr;
    }

    void initialize_sem() {
        sem = sem_open(shm_name.c_str(), O_CREAT, 0666, 1);
        if (sem == SEM_FAILED) {
            perror("sem_open");
            throw std::runtime_error("Failed to open semaphore");
        }
    }

    void cleanup() {
        if (shm_fd != -1) {
            close(shm_fd);
        }

        if (sem != SEM_FAILED) {
            sem_close(sem);
        }

        if (pool_ptr != MAP_FAILED) {
            munmap(pool_ptr, pool_size);
        }

        shm_unlink(SHM_POOL_NAME);
        sem_unlink(SEM_POOL_NAME);
    }
};
