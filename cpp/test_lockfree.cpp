#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include "zeroipc/memory.h"
#include "zeroipc/queue.h"
#include "zeroipc/stack.h"

using namespace zeroipc;
using zeroipc::Memory;
using zeroipc::Queue;
using zeroipc::Stack;

void test_queue_concurrent() {
    std::cout << "Testing Queue concurrent operations..." << std::endl;
    
    // Create shared memory
    Memory shm("/test_queue_concurrent", 10 * 1024 * 1024);
    
    // Create queue
    Queue<int> q(shm, "/test_queue", 1000);
    
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_thread = 1000;
    
    std::atomic<int> produced(0);
    std::atomic<int> consumed(0);
    
    // Producer threads
    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; i++) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < items_per_thread; j++) {
                int value = i * items_per_thread + j;
                while (!q.push(value)) {
                    std::this_thread::yield();
                }
                produced.fetch_add(1);
            }
        });
    }
    
    // Consumer threads
    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; i++) {
        consumers.emplace_back([&]() {
            for (int j = 0; j < items_per_thread; j++) {
                while (true) {
                    auto value = q.pop();
                    if (value.has_value()) {
                        consumed.fetch_add(1);
                        break;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    assert(produced.load() == num_producers * items_per_thread);
    assert(consumed.load() == num_consumers * items_per_thread);
    assert(q.empty());
    
    std::cout << "  ✓ Queue concurrent test passed" << std::endl;
}

void test_stack_concurrent() {
    std::cout << "Testing Stack concurrent operations..." << std::endl;
    
    // Create shared memory
    Memory shm("/test_stack_concurrent", 10 * 1024 * 1024);
    
    // Create stack
    Stack<int> s(shm, "/test_stack", 1000);
    
    const int num_pushers = 4;
    const int num_poppers = 4;
    const int items_per_thread = 1000;
    
    std::atomic<int> pushed(0);
    std::atomic<int> popped(0);
    
    // Pusher threads
    std::vector<std::thread> pushers;
    for (int i = 0; i < num_pushers; i++) {
        pushers.emplace_back([&, i]() {
            for (int j = 0; j < items_per_thread; j++) {
                int value = i * items_per_thread + j;
                while (!s.push(value)) {
                    std::this_thread::yield();
                }
                pushed.fetch_add(1);
            }
        });
    }
    
    // Popper threads
    std::vector<std::thread> poppers;
    for (int i = 0; i < num_poppers; i++) {
        poppers.emplace_back([&]() {
            for (int j = 0; j < items_per_thread; j++) {
                while (true) {
                    auto value = s.pop();
                    if (value.has_value()) {
                        popped.fetch_add(1);
                        break;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : pushers) t.join();
    for (auto& t : poppers) t.join();
    
    assert(pushed.load() == num_pushers * items_per_thread);
    assert(popped.load() == num_poppers * items_per_thread);
    assert(s.empty());
    
    std::cout << "  ✓ Stack concurrent test passed" << std::endl;
}

int main() {
    std::cout << "=== C++ Lock-Free Tests ===" << std::endl;
    
    test_queue_concurrent();
    test_stack_concurrent();
    
    std::cout << "\n✓ All lock-free tests passed!" << std::endl;
    
    // Cleanup
    Memory::unlink("/test_queue_concurrent");
    Memory::unlink("/test_stack_concurrent");
    
    return 0;
}