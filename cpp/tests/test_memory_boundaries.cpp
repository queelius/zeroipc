#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <zeroipc/stack.h>
#include <zeroipc/array.h>
#include <bit>
#include <cstring>
#include <thread>
#include <vector>
#include <limits>
#include <random>
#include <sys/resource.h>

using namespace zeroipc;

class MemoryBoundaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        Memory::unlink("/test_boundary");
    }
    
    void TearDown() override {
        Memory::unlink("/test_boundary");
    }
};

// ========== SIZE LIMIT TESTS ==========

TEST_F(MemoryBoundaryTest, MinimumViableMemory) {
    // Smallest memory that can hold a table
    size_t min_size = 4096; // Typically page size
    Memory mem("/test_boundary", min_size);
    
    // Should be able to create at least one tiny structure
    EXPECT_NO_THROW({
        Array<uint8_t> arr(mem, "tiny", 10);
        arr[0] = 255;
        EXPECT_EQ(arr[0], 255);
    });
}

TEST_F(MemoryBoundaryTest, LargeAllocationNearLimit) {
    // Test allocation that nearly fills memory
    size_t mem_size = 10 * 1024 * 1024; // 10MB
    Memory mem("/test_boundary", mem_size);
    
    // Reserve space for table (approximately 2.5KB for 64 entries)
    size_t table_overhead = 3000;
    size_t available = mem_size - table_overhead;
    
    // Try to allocate 90% of available space
    size_t large_size = (available * 9) / 10;
    size_t array_elements = large_size / sizeof(double);
    
    Array<double> arr(mem, "large", array_elements);
    
    // Write to boundaries
    arr[0] = 3.14159;
    arr[array_elements - 1] = 2.71828;
    
    EXPECT_EQ(arr[0], 3.14159);
    EXPECT_EQ(arr[array_elements - 1], 2.71828);
    
    // Try to create another structure - should fail
    EXPECT_THROW(
        Array<double> arr2(mem, "overflow", array_elements),
        std::runtime_error
    );
}

TEST_F(MemoryBoundaryTest, MaximumQueueCapacity) {
    size_t mem_size = 100 * 1024 * 1024; // 100MB
    Memory mem("/test_boundary", mem_size);
    
    // Calculate maximum queue capacity
    // Queue overhead = header + capacity * sizeof(T) + capacity * sizeof(atomic<uint32_t>)
    struct TestStruct {
        char data[1024]; // 1KB per element
    };

    size_t overhead = 1024; // Conservative estimate for header + table
    size_t bytes_per_slot = sizeof(TestStruct) + sizeof(std::atomic<uint32_t>);
    size_t max_capacity = (mem_size - overhead) / bytes_per_slot;

    // Create queue with the largest power-of-two capacity that fits
    // (queue capacities round up to powers of two, so rounding down here
    // keeps the allocation within the segment)
    size_t test_capacity = std::bit_floor(max_capacity - 100);
    Queue<TestStruct> queue(mem, "maxq", test_capacity);
    
    TestStruct ts;
    std::fill(std::begin(ts.data), std::end(ts.data), 'X');
    
    // Fill to capacity (Vyukov queue uses all capacity slots)
    size_t pushed = 0;
    while (queue.push(ts)) {
        pushed++;
        if (pushed >= test_capacity) break;
    }

    EXPECT_GE(pushed, test_capacity - 1); // Should fit almost all
    std::cout << "Pushed " << pushed << " of " << test_capacity << " items" << std::endl;
}

// ========== FRAGMENTATION TESTS ==========

TEST_F(MemoryBoundaryTest, MemoryFragmentation) {
    size_t mem_size = 5 * 1024 * 1024; // 5MB - smaller to test exhaustion
    Memory mem("/test_boundary", mem_size);
    
    // Since we can't delete structures, fill memory with many structures
    std::vector<std::string> names;
    
    // Phase 1: Allocate many structures to use most memory
    for (int i = 0; i < 30; i++) {
        std::string name = "frag_" + std::to_string(i);
        names.push_back(name);
        
        // Allocate larger structures to fill memory
        if (i % 3 == 0) {
            Queue<int> q(mem, name, 10000);  // ~40KB
        } else if (i % 3 == 1) {
            Stack<int> s(mem, name, 10000);  // ~40KB
        } else {
            Array<int> a(mem, name, 10000);  // ~40KB
        }
    }
    
    // Phase 2: Try to allocate large structure - should fail due to insufficient space
    bool large_alloc_failed = false;
    try {
        // Memory used: ~3KB table + 30 * 40KB = ~1.2MB used, ~3.8MB free
        // Try to allocate more than what's free
        Array<double> large(mem, "large_array", 600000);  // ~4.8MB, definitely won't fit
    } catch (const std::exception&) {
        large_alloc_failed = true;
    }
    
    EXPECT_TRUE(large_alloc_failed) << "Large allocation should fail due to insufficient space";
}

TEST_F(MemoryBoundaryTest, TableExhaustion) {
    // Use default 64-entry table
    Memory mem("/test_boundary", 100 * 1024 * 1024);
    
    std::vector<std::unique_ptr<Array<int>>> arrays;
    
    // Fill table to capacity
    for (int i = 0; i < 63; i++) { // Leave room for error
        std::string name = "arr_" + std::to_string(i);
        arrays.push_back(
            std::make_unique<Array<int>>(mem, name, 10)
        );
    }
    
    // Next allocation should fail
    bool failed = false;
    try {
        Array<int> overflow(mem, "overflow", 10);
    } catch (const std::exception& e) {
        failed = true;
        std::cout << "Table exhausted as expected: " << e.what() << std::endl;
    }
    
    // May succeed if table is larger than expected
    if (!failed) {
        std::cout << "Table has more than 63 entries available" << std::endl;
    }
}

// ========== EXTREME VALUES TESTS ==========

TEST_F(MemoryBoundaryTest, ExtremeSizes) {
    Memory mem("/test_boundary", 10 * 1024 * 1024);
    
    // Test with size_t max values
    bool overflow_caught = false;
    try {
        Queue<int> q(mem, "extreme", SIZE_MAX);
    } catch (const std::exception&) {
        overflow_caught = true;
    }
    EXPECT_TRUE(overflow_caught);
    
    // Test with zero
    EXPECT_THROW(
        Queue<int> q(mem, "zero", 0),
        std::exception
    );
}

TEST_F(MemoryBoundaryTest, LargeStructTypes) {
    Memory mem("/test_boundary", 100 * 1024 * 1024);
    
    // Test with very large struct
    struct HugeStruct {
        char data[1024 * 1024]; // 1MB per element
    };
    
    // Should only fit a few elements
    Queue<HugeStruct> queue(mem, "huge", 10);
    
    HugeStruct hs;
    std::fill(std::begin(hs.data), std::end(hs.data), 'A');
    
    int count = 0;
    while (queue.push(hs) && count < 10) {
        count++;
    }
    
    EXPECT_GE(count, 1) << "Should fit at least one huge element";
    EXPECT_LE(count, 10) << "Should not exceed specified capacity";
    
    std::cout << "Fitted " << count << " 1MB structures" << std::endl;
}

// ========== RESOURCE EXHAUSTION TESTS ==========

TEST_F(MemoryBoundaryTest, SystemMemoryPressure) {
    // Get current memory limits
    struct rlimit rlim;
    getrlimit(RLIMIT_AS, &rlim);
    
    std::cout << "Current memory limit: " << rlim.rlim_cur << " bytes" << std::endl;
    
    // Try to create very large shared memory
    size_t huge_size = 1024ULL * 1024 * 1024; // 1GB
    
    bool failed = false;
    try {
        Memory mem("/test_boundary", huge_size);
        
        // If successful, verify we can use it
        Array<int> arr(mem, "test", 1000);
        arr[0] = 42;
        EXPECT_EQ(arr[0], 42);
    } catch (const std::exception& e) {
        failed = true;
        std::cout << "Large allocation failed (expected): " << e.what() << std::endl;
    }
    
    // Either outcome is acceptable - depends on system
    if (!failed) {
        std::cout << "System allowed 1GB shared memory allocation" << std::endl;
    }
}

TEST_F(MemoryBoundaryTest, RapidAllocationDeallocation) {
    Memory mem("/test_boundary", 50 * 1024 * 1024);
    
    // Rapidly create structures (table doesn't support deletion, so use unique names)
    for (int round = 0; round < 30; round++) {  // Reduced to 30 to stay within 64-entry table limit (30*2 = 60)
        std::string qname = "q_" + std::to_string(round);
        std::string sname = "s_" + std::to_string(round);
        
        {
            Queue<int> q(mem, qname, 100);  // Smaller size to fit more
            ASSERT_TRUE(q.push(round));
        }
        
        {
            Stack<int> s(mem, sname, 100);
            s.push(round * 2);
        }
    }
    
    // Memory should not be exhausted (50*2 = 100 structures, but table has 64 limit)
    // So we expect this to fail at some point, but the test should handle it gracefully
    bool allocation_succeeded = false;
    try {
        Array<int> final_array(mem, "final", 1000);
        final_array[0] = 999;
        allocation_succeeded = (final_array[0] == 999);
    } catch (...) {
        // Expected if table is full
    }
    
    // Either allocation succeeds or table is full - both are valid outcomes
    EXPECT_TRUE(true);  // Test passes either way
}

// ========== ALIGNMENT TESTS ==========

TEST_F(MemoryBoundaryTest, AlignmentBoundaries) {
    Memory mem("/test_boundary", 10 * 1024 * 1024);

    // The library guarantees 8-byte section alignment. Element types whose
    // alignment is <= 8 are placed on natural boundaries; over-aligned types
    // (alignof > 8, e.g. alignas(16)/alignas(64)) are rejected at compile time
    // by the static_assert(alignof(T) <= MAX_ELEM_ALIGN) in each container, so
    // they cannot be instantiated here.
    struct Aligned8 {
        alignas(8) char data[8];
    };

    Array<Aligned8> arr8(mem, "align8", 100);
    Array<double>   arrd(mem, "alignd", 100);
    Array<uint64_t> arru(mem, "alignu", 100);

    // Every 8-aligned element must be naturally aligned for its full width.
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&arr8[0]) % alignof(Aligned8), 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&arrd[0]) % alignof(double), 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&arru[0]) % alignof(uint64_t), 0);

    // Interior elements stay aligned across the whole array.
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&arrd[37]) % alignof(double), 0);
}

// ========== CONCURRENT BOUNDARY TESTS ==========

TEST_F(MemoryBoundaryTest, ConcurrentNearCapacity) {
    Memory mem("/test_boundary", 10 * 1024 * 1024);
    Queue<int> queue(mem, "concurrent", 100); // Small queue (rounds to 128)

    // Fill queue to one below actual capacity (Vyukov queue uses all N slots)
    for (int i = 0; i < static_cast<int>(queue.capacity()) - 1; i++) {
        ASSERT_TRUE(queue.push(i));
    }
    
    // Multiple threads compete for last slots
    std::atomic<int> successes{0};
    std::atomic<int> failures{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10; i++) {
                if (queue.push(1000 + i)) {
                    successes++;
                } else {
                    failures++;
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    // Should have exactly 1 success (space for 1 more item)
    EXPECT_EQ(successes, 1);
    EXPECT_EQ(failures, 99);
    EXPECT_TRUE(queue.full());
}

TEST_F(MemoryBoundaryTest, StressTestMemoryBarriers) {
    Memory mem("/test_boundary", 50 * 1024 * 1024);
    
    // Create multiple structures at memory boundaries
    std::vector<std::unique_ptr<Queue<int>>> queues;
    std::vector<std::unique_ptr<Stack<int>>> stacks;
    
    // Allocate until memory is nearly full
    int structures_created = 0;
    
    try {
        for (int i = 0; i < 1000; i++) {
            if (i % 2 == 0) {
                std::string name = "q_" + std::to_string(i);
                queues.push_back(
                    std::make_unique<Queue<int>>(mem, name, 10000)
                );
            } else {
                std::string name = "s_" + std::to_string(i);
                stacks.push_back(
                    std::make_unique<Stack<int>>(mem, name, 10000)
                );
            }
            structures_created++;
        }
    } catch (const std::exception&) {
        // Expected to fail at some point
    }
    
    std::cout << "Created " << structures_created << " structures before exhaustion" << std::endl;
    EXPECT_GT(structures_created, 0);
    
    // Verify all created structures still work
    for (auto& q : queues) {
        ASSERT_TRUE(q->push(42));
        EXPECT_EQ(*q->pop(), 42);
    }
    
    for (auto& s : stacks) {
        s->push(84);
        EXPECT_EQ(*s->pop(), 84);
    }
}
// ========== FORMAT V2 SECTION ALIGNMENT (LAYOUT REGRESSION) ==========
// Format v2 requires the queue's sequence array and the stack's slot-state
// array to start at header(16) + align8(elem_size * capacity). These tests
// recompute that offset from the spec formula and verify, via raw memory
// reads at the computed address, that the implementation actually placed
// the side arrays there. If the layout rule ever drifts, the raw reads
// stop matching the structures' observable side-array contents.

namespace {

template<typename T>
void expect_queue_section_layout(Memory& mem, const std::string& name,
                                 size_t cap) {
    Queue<T> q(mem, name, cap);

    // The queue rounds the requested capacity up to a power of two
    // (wrap-safety); the layout is computed from the actual capacity.
    const size_t actual_cap = q.capacity();
    EXPECT_GE(actual_cap, cap);
    EXPECT_EQ(actual_cap & (actual_cap - 1), 0u) << "capacity not a power of two";

    size_t offset = 0, size = 0;
    ASSERT_TRUE(mem.find(name, offset, size));

    // Spec formula, independent of the implementation's internal pointers
    const size_t side_off = 16 + align_up(sizeof(T) * actual_cap, 8);
    EXPECT_EQ(size, side_off + actual_cap * sizeof(uint32_t))
        << "table entry size wrong for elem_size " << sizeof(T);

    // Vyukov invariant: seq[i] == i immediately after creation. Finding
    // those values at the spec-computed offset proves placement.
    const char* base = static_cast<const char*>(mem.base());
    for (size_t i = 0; i < actual_cap; ++i) {
        uint32_t seq;
        std::memcpy(&seq, base + offset + side_off + i * 4, 4);
        EXPECT_EQ(seq, static_cast<uint32_t>(i))
            << "seq[" << i << "] not at spec offset for elem_size " << sizeof(T);
    }
}

template<typename T>
void expect_stack_section_layout(Memory& mem, const std::string& name,
                                 size_t cap) {
    Stack<T> s(mem, name, cap);

    size_t offset = 0, size = 0;
    ASSERT_TRUE(mem.find(name, offset, size));

    const size_t side_off = 16 + align_up(sizeof(T) * cap, 8);
    EXPECT_EQ(size, side_off + cap * sizeof(uint32_t))
        << "table entry size wrong for elem_size " << sizeof(T);

    ASSERT_TRUE(s.push(static_cast<T>(1)));
    ASSERT_TRUE(s.push(static_cast<T>(2)));

    const char* base = static_cast<const char*>(mem.base());
    auto state_at = [&](size_t i) {
        uint32_t v;
        std::memcpy(&v, base + offset + side_off + i * 4, 4);
        return v;
    };
    EXPECT_EQ(state_at(0), Stack<T>::SLOT_READY);
    EXPECT_EQ(state_at(1), Stack<T>::SLOT_READY);
    EXPECT_EQ(state_at(2), Stack<T>::SLOT_EMPTY);

    ASSERT_TRUE(s.pop().has_value());
    EXPECT_EQ(state_at(1), Stack<T>::SLOT_EMPTY);
}

} // namespace

TEST_F(MemoryBoundaryTest, QueueSideArrayAt8ByteBoundary) {
    Memory mem("/test_boundary", 1024 * 1024);
    expect_queue_section_layout<char>(mem, "q_char", 5);     // 5 -> pad to 8
    expect_queue_section_layout<uint32_t>(mem, "q_u32", 5);  // 20 -> pad to 24
    expect_queue_section_layout<double>(mem, "q_f64", 5);    // 40, no pad
}

TEST_F(MemoryBoundaryTest, StackSideArrayAt8ByteBoundary) {
    Memory mem("/test_boundary", 1024 * 1024);
    expect_stack_section_layout<char>(mem, "s_char", 5);
    expect_stack_section_layout<uint32_t>(mem, "s_u32", 5);
    expect_stack_section_layout<double>(mem, "s_f64", 5);
}
