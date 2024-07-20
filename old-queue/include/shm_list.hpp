#pragma once

#include <semaphore.h>
#include <stdexcept>
#include <iostream>
#include <cstring>

template <typename T>
struct Node {
    T data;
    Node* next;
};

template <typename T>
class ShmList {
public:
    ShmList(MemoryPool& pool) : pool(pool), head(nullptr), sem(nullptr) {
        initialize_sem();
    }

    ~ShmList() {
        cleanup();
    }

    void push_back(const T& value) {
        sem_wait(sem); // Lock
        Node<T>* new_node = new (pool.allocate(sizeof(Node<T>))) Node<T>{value, nullptr};
        if (head == nullptr) {
            head = new_node;
        } else {
            Node<T>* current = head;
            while (current->next != nullptr) {
                current = current->next;
            }
            current->next = new_node;
        }
        sem_post(sem); // Unlock
    }

    void pop_front() {
        sem_wait(sem); // Lock
        if (head != nullptr) {
            Node<T>* old_head = head;
            head = head->next;
            pool.deallocate(old_head);
        }
        sem_post(sem); // Unlock
    }

    void traverse() const {
        Node<T>* current = head;
        while (current != nullptr) {
            std::cout << current->data << " ";
            current = current->next;
        }
        std::cout << std::endl;
    }

private:
    MemoryPool& pool;
    Node<T>* head;
    sem_t* sem;

    void initialize_sem() {
        sem = sem_open("/sem_list", O_CREAT, 0666, 1);
        if (sem == SEM_FAILED) {
            perror("sem_open");
            throw std::runtime_error("Failed to open semaphore");
        }
    }

    void cleanup() {
        if (sem != SEM_FAILED) {
            sem_close(sem);
        }
    }
};
