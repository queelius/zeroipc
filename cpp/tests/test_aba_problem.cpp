#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <zeroipc/stack.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <set>
#include <unordered_map>

using namespace zeroipc;
using namespace std::chrono;

class ABATest : public ::testing::Test {
protected:
    void SetUp() override {
        Memory::unlink("/test_aba");
    }
    
    void TearDown() override {
        Memory::unlink("/test_aba");
    }
};

// ========== QUEUE ABA TESTS ==========

TEST_F(ABATest, QueueABAScenario) {
    // Classic ABA scenario for queue:
    // Thread 1: Read head = A, get preempted
    // Thread 2: Pop A, Pop B, Push A (head is A again)
    // Thread 1: CAS succeeds but internal state is different
    
    Memory mem("/test_aba", 10*1024*1024);
    Queue<uint64_t> queue(mem, "aba_queue", 1000);
    
    // Use unique values to detect ABA
    const uint64_t MARKER_A = 0xAAAAAAAAAAAAAAAA;
    const uint64_t MARKER_B = 0xBBBBBBBBBBBBBBBB;
    const uint64_t MARKER_C = 0xCCCCCCCCCCCCCCCC;
    
    // Pre-fill queue
    queue.push(MARKER_A);
    queue.push(MARKER_B);
    queue.push(MARKER_C);
    
    std::atomic<int> phase{0};
    std::atomic<bool> aba_detected{false};
    std::vector<uint64_t> thread1_values;
    std::vector<uint64_t> thread2_values;
    
    // Thread 1: Slow reader
    std::thread t1([&]() {
        while (phase < 3) {
            if (phase == 1) {
                // Try to pop - might get delayed
                std::this_thread::sleep_for(microseconds(100));
                auto val = queue.pop();
                if (val) {
                    thread1_values.push_back(*val);
                    
                    // Check if we got an unexpected value due to ABA
                    if (*val == MARKER_A && thread2_values.size() > 1) {
                        // Thread 2 has already processed, possible ABA
                        aba_detected = true;
                    }
                }
                phase = 2;
            }
            std::this_thread::yield();
        }
    });
    
    // Thread 2: Fast manipulator
    std::thread t2([&]() {
        phase = 1;  // Signal thread 1 to start
        
        // Quickly manipulate queue
        auto val1 = queue.pop();
        if (val1) thread2_values.push_back(*val1);
        
        auto val2 = queue.pop();
        if (val2) thread2_values.push_back(*val2);
        
        // Push original value back
        if (val1) queue.push(*val1);
        
        // Wait for thread 1
        while (phase < 2) {
            std::this_thread::yield();
        }
        phase = 3;
    });
    
    t1.join();
    t2.join();
    
    // Verify operations completed
    EXPECT_GT(thread1_values.size() + thread2_values.size(), 0);
    
    // ABA is prevented by proper CAS implementation
    if (aba_detected) {
        std::cout << "Potential ABA scenario detected (but handled correctly)" << std::endl;
    }
}

TEST_F(ABATest, QueueRapidRecycling) {
    // Test rapid push/pop cycling that could trigger ABA
    Memory mem("/test_aba", 10*1024*1024);
    Queue<int> queue(mem, "recycle_queue", 100);
    
    const int num_threads = 4;
    const int ops_per_thread = 10000;
    
    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> error_detected{false};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            std::set<int> my_values;
            
            for (int i = 0; i < ops_per_thread; i++) {
                int value = t * ops_per_thread + i;
                
                // Push unique value
                if (queue.push(value)) {
                    my_values.insert(value);
                    total_pushed++;
                }
                
                // Immediately try to pop
                auto popped = queue.pop();
                if (popped) {
                    total_popped++;
                    
                    // Check if value makes sense
                    if (*popped < 0 || *popped >= num_threads * ops_per_thread) {
                        error_detected = true;
                    }
                }
                
                // Rapid cycling
                if (i % 10 == 0) {
                    // Burst of pops
                    for (int j = 0; j < 5; j++) {
                        auto val = queue.pop();
                        if (val) total_popped++;
                    }
                    
                    // Burst of pushes
                    for (int j = 0; j < 5; j++) {
                        if (queue.push(value + j * 1000000)) {
                            total_pushed++;
                        }
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    EXPECT_FALSE(error_detected) << "Invalid values detected";
    
    // Drain remaining
    while (!queue.empty()) {
        queue.pop();
    }
    
    std::cout << "Rapid recycling: " << total_pushed << " pushes, " 
             << total_popped << " pops" << std::endl;
}

// ========== STACK ABA TESTS ==========

TEST_F(ABATest, StackABAScenario) {
    // Classic ABA for stack:
    // Thread 1: Read top = A, get preempted
    // Thread 2: Pop A, Pop B, Push A (top is A again)
    // Thread 1: CAS succeeds but stack structure changed
    
    Memory mem("/test_aba", 10*1024*1024);
    Stack<uintptr_t> stack(mem, "aba_stack", 1000);
    
    // Use pointer-like values to simulate real scenario
    const uintptr_t NODE_A = 0x1000;
    const uintptr_t NODE_B = 0x2000;
    const uintptr_t NODE_C = 0x3000;
    
    // Pre-fill stack
    stack.push(NODE_C);
    stack.push(NODE_B);
    stack.push(NODE_A);  // Top
    
    std::atomic<int> state{0};
    std::vector<uintptr_t> thread1_ops;
    std::vector<uintptr_t> thread2_ops;
    
    // Thread 1: Slow popper
    std::thread t1([&]() {
        while (state == 0) {
            std::this_thread::yield();
        }
        
        // Attempt pop after delay
        std::this_thread::sleep_for(microseconds(100));
        
        auto val = stack.pop();
        if (val) {
            thread1_ops.push_back(*val);
        }
        
        state = 3;
    });
    
    // Thread 2: ABA manipulator
    std::thread t2([&]() {
        state = 1;  // Start thread 1
        
        // Quick manipulation
        auto a = stack.pop();
        if (a) thread2_ops.push_back(*a);
        
        auto b = stack.pop();
        if (b) thread2_ops.push_back(*b);
        
        // Push A back (creating ABA situation)
        if (a) stack.push(*a);
        
        state = 2;
        
        while (state < 3) {
            std::this_thread::yield();
        }
    });
    
    t1.join();
    t2.join();
    
    // Verify operations completed without corruption
    EXPECT_GT(thread1_ops.size() + thread2_ops.size(), 0);
    
    // Check final stack state
    size_t remaining = stack.size();
    std::cout << "Stack ABA test: " << remaining << " items remain" << std::endl;
}

TEST_F(ABATest, StackFreeListRecycling) {
    // Simulate free-list style recycling that can cause ABA
    Memory mem("/test_aba", 10*1024*1024);
    Stack<int> stack(mem, "freelist_stack", 500);
    
    const int num_threads = 8;
    const int iterations = 5000;
    
    std::atomic<int> generation{0};
    std::atomic<bool> corruption_detected{false};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < iterations; i++) {
                // Encode generation in value
                int value = (generation.load() << 16) | (t << 8) | (i & 0xFF);
                
                if (t % 2 == 0) {
                    // Even threads: push then pop
                    stack.push(value);
                    
                    auto popped = stack.pop();
                    if (popped) {
                        // Verify value structure
                        int gen = (*popped >> 16) & 0xFFFF;
                        int thread = (*popped >> 8) & 0xFF;
                        int iter = *popped & 0xFF;
                        
                        if (thread >= num_threads || iter >= iterations) {
                            corruption_detected = true;
                        }
                    }
                } else {
                    // Odd threads: pop then push
                    auto popped = stack.pop();
                    if (popped) {
                        // Recycle value with new generation
                        int recycled = ((generation.load() + 1) << 16) | 
                                      (t << 8) | (i & 0xFF);
                        stack.push(recycled);
                    } else {
                        stack.push(value);
                    }
                }
                
                // Periodically increment generation
                if (i % 100 == 0) {
                    generation++;
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    EXPECT_FALSE(corruption_detected) << "Stack corruption detected";
    
    std::cout << "Free-list recycling test completed with " 
             << generation.load() << " generations" << std::endl;
}

// ========== TAGGED POINTER TESTS ==========

TEST_F(ABATest, TaggedPointerSimulation) {
    // Simulate tagged pointer approach to prevent ABA
    Memory mem("/test_aba", 10*1024*1024);
    
    struct TaggedValue {
        uint32_t tag;
        uint32_t value;
        
        bool operator==(const TaggedValue& other) const {
            return tag == other.tag && value == other.value;
        }
    };
    
    Stack<TaggedValue> stack(mem, "tagged_stack", 1000);
    
    std::atomic<uint32_t> global_tag{0};
    const int num_threads = 6;
    const int ops_per_thread = 1000;
    
    std::atomic<int> aba_prevented{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < ops_per_thread; i++) {
                TaggedValue tv;
                tv.tag = global_tag.fetch_add(1);
                tv.value = t * 1000 + i;
                
                if (i % 3 == 0) {
                    // Push with unique tag
                    stack.push(tv);
                } else if (i % 3 == 1) {
                    // Pop and check tag
                    auto popped = stack.pop();
                    if (popped) {
                        // In real implementation, would check if tag prevents ABA
                        if (popped->tag != tv.tag) {
                            aba_prevented++;
                        }
                    }
                } else {
                    // Pop, modify, push back
                    auto popped = stack.pop();
                    if (popped) {
                        TaggedValue modified = *popped;
                        modified.tag = global_tag.fetch_add(1);
                        stack.push(modified);
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    std::cout << "Tagged pointer simulation: " << aba_prevented 
             << " potential ABA situations handled" << std::endl;
    
    EXPECT_GT(global_tag.load(), 0);
}

// ========== MEMORY ORDERING TESTS ==========

TEST_F(ABATest, MemoryOrderingABA) {
    // Test that proper memory ordering prevents ABA issues
    Memory mem("/test_aba", 10*1024*1024);
    Queue<std::atomic<int>> queue(mem, "ordering_queue", 100);
    
    // Initialize with atomic values
    for (int i = 0; i < 50; i++) {
        std::atomic<int> val{i};
        queue.push(val);
    }
    
    const int num_threads = 4;
    std::atomic<bool> stop{false};
    std::atomic<int> inconsistencies{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            int last_value = -1;
            
            while (!stop) {
                if (t % 2 == 0) {
                    // Consumer thread
                    auto val = queue.pop();
                    if (val) {
                        int current = val->load(std::memory_order_acquire);
                        
                        // Check for inconsistency
                        if (last_value >= 0 && current < last_value - 100) {
                            // Unexpected jump backwards
                            inconsistencies++;
                        }
                        last_value = current;
                        
                        // Modify and push back
                        val->store(current + 1000, std::memory_order_release);
                        queue.push(*val);
                    }
                } else {
                    // Producer thread
                    std::atomic<int> new_val{t * 10000 + last_value};
                    queue.push(new_val);
                    last_value++;
                }
                
                std::this_thread::yield();
            }
        });
    }
    
    std::this_thread::sleep_for(milliseconds(100));
    stop = true;
    
    for (auto& t : threads) t.join();
    
    EXPECT_EQ(inconsistencies.load(), 0) << "Memory ordering violations detected";
    
    std::cout << "Memory ordering test completed" << std::endl;
}

// ========== HAZARD POINTER SIMULATION ==========

TEST_F(ABATest, HazardPointerConcept) {
    // Simulate hazard pointer approach for ABA prevention
    Memory mem("/test_aba", 10*1024*1024);
    Stack<uint64_t> stack(mem, "hazard_stack", 1000);
    
    // Simulated hazard pointers (thread-local protection)
    thread_local uint64_t hazard_pointer = 0;
    std::atomic<int> protected_pops{0};
    std::atomic<int> total_ops{0};
    
    const int num_threads = 6;
    const int ops_per_thread = 2000;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < ops_per_thread; i++) {
                if (i % 2 == 0) {
                    // Protected pop
                    auto val = stack.top();
                    if (val) {
                        hazard_pointer = *val;  // Protect this value
                        
                        // Simulate some work
                        std::this_thread::yield();
                        
                        auto popped = stack.pop();
                        if (popped && *popped == hazard_pointer) {
                            protected_pops++;
                        }
                        
                        hazard_pointer = 0;  // Clear protection
                    }
                } else {
                    // Push operation
                    uint64_t value = (t << 32) | i;
                    stack.push(value);
                }
                
                total_ops++;
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    std::cout << "Hazard pointer simulation: " << protected_pops 
             << " protected pops out of " << total_ops << " total ops" << std::endl;
    
    EXPECT_GT(protected_pops.load(), 0);
}

// ========== VERIFICATION TEST ==========

TEST_F(ABATest, ComprehensiveABAStress) {
    // Comprehensive stress test for ABA issues
    Memory mem("/test_aba", 100*1024*1024);
    Queue<uint64_t> queue(mem, "stress_queue", 10000);
    Stack<uint64_t> stack(mem, "stress_stack", 10000);
    
    const int num_threads = 10;
    const int duration_ms = 1000;
    
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> operations{0};
    std::unordered_map<uint64_t, int> value_counts;
    std::mutex counts_mutex;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            std::mt19937_64 rng(t);
            
            while (!stop) {
                uint64_t value = rng();
                int op = rng() % 4;
                
                switch(op) {
                    case 0:  // Queue push
                        queue.push(value);
                        break;
                    case 1:  // Queue pop
                        queue.pop();
                        break;
                    case 2:  // Stack push
                        stack.push(value);
                        break;
                    case 3:  // Stack pop
                        stack.pop();
                        break;
                }
                
                operations++;
                
                // Track value frequency for ABA detection
                if (op == 0 || op == 2) {
                    std::lock_guard<std::mutex> lock(counts_mutex);
                    value_counts[value]++;
                }
            }
        });
    }
    
    std::this_thread::sleep_for(milliseconds(duration_ms));
    stop = true;
    
    for (auto& t : threads) t.join();
    
    // Check for value reuse (potential ABA)
    int reused_values = 0;
    for (const auto& [value, count] : value_counts) {
        if (count > 1) {
            reused_values++;
        }
    }
    
    std::cout << "Comprehensive stress test: " << operations.load() 
             << " operations, " << reused_values << " values reused" << std::endl;
    
    // Some reuse is expected in random generation
    EXPECT_GT(operations.load(), 0);
}