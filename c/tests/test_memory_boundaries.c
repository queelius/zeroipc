#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#include <errno.h>
#include "../include/zeroipc.h"

// ========== SIZE LIMIT TESTS ==========

void test_minimum_viable_memory() {
    printf("Testing minimum viable memory...\n");
    
    // Smallest memory that can hold table and tiny structure
    size_t min_size = 4096; // Page size
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", min_size, 64);
    assert(mem != NULL);
    
    // Create tiny array
    zeroipc_array_t* arr = zeroipc_array_create(mem, "tiny", sizeof(uint8_t), 10);
    assert(arr != NULL);
    
    uint8_t val = 255;
    assert(zeroipc_array_set(arr, 0, &val) == ZEROIPC_OK);
    
    uint8_t* retrieved = (uint8_t*)zeroipc_array_get(arr, 0);
    assert(retrieved != NULL && *retrieved == 255);
    
    zeroipc_array_close(arr);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Minimum viable memory passed\n");
}

void test_large_allocation_near_limit() {
    printf("Testing large allocation near limit...\n");
    
    size_t mem_size = 10 * 1024 * 1024; // 10MB
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", mem_size, 64);
    assert(mem != NULL);
    
    // Calculate available space (minus table overhead ~3KB)
    size_t table_overhead = 3000;
    size_t available = mem_size - table_overhead;
    
    // Allocate 90% of available space
    size_t large_size = (available * 9) / 10;
    size_t array_elements = large_size / sizeof(double);
    
    zeroipc_array_t* arr = zeroipc_array_create(mem, "large", sizeof(double), array_elements);
    assert(arr != NULL);
    
    // Write to boundaries
    double first = 3.14159;
    double last = 2.71828;
    assert(zeroipc_array_set(arr, 0, &first) == ZEROIPC_OK);
    assert(zeroipc_array_set(arr, array_elements - 1, &last) == ZEROIPC_OK);
    
    double* retrieved_first = (double*)zeroipc_array_get(arr, 0);
    double* retrieved_last = (double*)zeroipc_array_get(arr, array_elements - 1);
    assert(retrieved_first != NULL && *retrieved_first == first);
    assert(retrieved_last != NULL && *retrieved_last == last);
    
    // Next allocation should fail
    zeroipc_array_t* overflow = zeroipc_array_create(mem, "overflow", sizeof(double), array_elements);
    assert(overflow == NULL);
    
    zeroipc_array_close(arr);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Large allocation near limit passed\n");
}

void test_maximum_queue_capacity() {
    printf("Testing maximum queue capacity...\n");
    
    size_t mem_size = 100 * 1024 * 1024; // 100MB
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", mem_size, 64);
    assert(mem != NULL);
    
    // Large struct (1KB per element)
    typedef struct {
        char data[1024];
    } TestStruct;
    
    // Calculate maximum capacity
    size_t overhead = 1024;
    size_t max_capacity = (mem_size - overhead) / sizeof(TestStruct) - 1;
    size_t test_capacity = max_capacity - 100; // Leave margin
    
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "maxq", sizeof(TestStruct), test_capacity);
    assert(queue != NULL);
    
    TestStruct ts;
    memset(ts.data, 'X', sizeof(ts.data));
    
    // Fill to capacity
    size_t pushed = 0;
    while (pushed < test_capacity - 1) {
        if (zeroipc_queue_push(queue, &ts) != ZEROIPC_OK) {
            break;
        }
        pushed++;
    }
    
    printf("  Pushed %zu of %zu items\n", pushed, test_capacity);
    assert(pushed >= test_capacity - 2);
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Maximum queue capacity passed\n");
}

// ========== FRAGMENTATION TESTS ==========

void test_memory_exhaustion() {
    printf("Testing memory exhaustion...\n");
    
    size_t mem_size = 1024 * 1024; // 1MB small memory
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", mem_size, 128);
    assert(mem != NULL);
    
    // Fill up most of the memory
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "array_%d", i);
        // Each array uses ~100KB
        zeroipc_array_t* a = zeroipc_array_create(mem, name, sizeof(int), 25000);
        if (!a) break;  // Stop when we run out of space
        zeroipc_array_close(a);
    }
    
    // This large allocation should fail - no space left
    zeroipc_array_t* large = zeroipc_array_create(mem, "large_array", sizeof(double), 100000);
    assert(large == NULL);  // Should fail - not enough space left
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Memory exhaustion passed\n");
}

void test_table_exhaustion() {
    printf("Testing table exhaustion...\n");
    
    // Use small table size for testing
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", 100*1024*1024, 32);
    assert(mem != NULL);
    
    zeroipc_array_t* arrays[35]; // More than table size
    
    // Fill table to capacity
    int created = 0;
    for (int i = 0; i < 35; i++) {
        char name[32];
        snprintf(name, sizeof(name), "arr_%d", i);
        
        arrays[i] = zeroipc_array_create(mem, name, sizeof(int), 10);
        if (arrays[i] == NULL) {
            printf("  Table exhausted at entry %d\n", i);
            break;
        }
        created++;
    }
    
    // Should have hit limit
    assert(created < 35);
    
    // Cleanup
    for (int i = 0; i < created; i++) {
        zeroipc_array_close(arrays[i]);
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Table exhaustion passed\n");
}

// ========== EXTREME VALUES TESTS ==========

void test_extreme_sizes() {
    printf("Testing extreme sizes...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", 10*1024*1024, 64);
    assert(mem != NULL);
    
    // Test with SIZE_MAX
    zeroipc_queue_t* extreme = zeroipc_queue_create(mem, "extreme", sizeof(int), SIZE_MAX);
    assert(extreme == NULL);
    
    // Test with zero
    zeroipc_queue_t* zero = zeroipc_queue_create(mem, "zero", sizeof(int), 0);
    assert(zero == NULL);
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Extreme sizes passed\n");
}

void test_large_struct_types() {
    printf("Testing large struct types...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", 100*1024*1024, 64);
    assert(mem != NULL);
    
    // 1MB per element
    typedef struct {
        char data[1024 * 1024];
    } HugeStruct;
    
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "huge", sizeof(HugeStruct), 10);
    assert(queue != NULL);
    
    HugeStruct hs;
    memset(hs.data, 'A', sizeof(hs.data));
    
    int count = 0;
    for (int i = 0; i < 10; i++) {
        if (zeroipc_queue_push(queue, &hs) == ZEROIPC_OK) {
            count++;
        } else {
            break;
        }
    }
    
    printf("  Fitted %d 1MB structures\n", count);
    assert(count >= 1 && count <= 10);
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Large struct types passed\n");
}

// ========== CONCURRENT BOUNDARY TESTS ==========

typedef struct {
    zeroipc_queue_t* queue;
    int* successes;
    int* failures;
    pthread_mutex_t* mutex;
} concurrent_test_data_t;

void* concurrent_push_thread(void* arg) {
    concurrent_test_data_t* data = (concurrent_test_data_t*)arg;
    
    for (int i = 0; i < 10; i++) {
        int value = 1000 + i;
        if (zeroipc_queue_push(data->queue, &value) == ZEROIPC_OK) {
            pthread_mutex_lock(data->mutex);
            (*data->successes)++;
            pthread_mutex_unlock(data->mutex);
        } else {
            pthread_mutex_lock(data->mutex);
            (*data->failures)++;
            pthread_mutex_unlock(data->mutex);
        }
    }
    
    return NULL;
}

void test_concurrent_near_capacity() {
    printf("Testing concurrent operations near capacity...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", 10*1024*1024, 64);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "concurrent", sizeof(int), 100);
    
    // Fill to near capacity
    for (int i = 0; i < 98; i++) {
        assert(zeroipc_queue_push(queue, &i) == ZEROIPC_OK);
    }
    
    // Multiple threads compete for last slots
    int successes = 0;
    int failures = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    concurrent_test_data_t thread_data = {
        .queue = queue,
        .successes = &successes,
        .failures = &failures,
        .mutex = &mutex
    };
    
    pthread_t threads[10];
    for (int i = 0; i < 10; i++) {
        pthread_create(&threads[i], NULL, concurrent_push_thread, &thread_data);
    }
    
    for (int i = 0; i < 10; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Should have exactly 1 success (1 slot remaining)
    assert(successes == 1);
    assert(failures == 99);
    assert(zeroipc_queue_full(queue));
    
    pthread_mutex_destroy(&mutex);
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Concurrent near capacity passed\n");
}

// ========== RAPID OPERATIONS TEST ==========

void test_rapid_allocation_deallocation() {
    printf("Testing rapid allocation/deallocation...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", 50*1024*1024, 64);
    assert(mem != NULL);
    
    // Rapidly create and destroy structures
    for (int round = 0; round < 100; round++) {
        char name[32];
        snprintf(name, sizeof(name), "rapid_%d", round % 10);
        
        // Create and destroy queue
        zeroipc_queue_t* q = zeroipc_queue_create(mem, name, sizeof(int), 1000);
        if (q) {
            int val = round;
            zeroipc_queue_push(q, &val);
            zeroipc_queue_close(q);
        }
        
        // Reuse same name with stack
        zeroipc_stack_t* s = zeroipc_stack_create(mem, name, sizeof(int), 1000);
        if (s) {
            int val = round * 2;
            zeroipc_stack_push(s, &val);
            zeroipc_stack_close(s);
        }
    }
    
    // Memory should not be exhausted
    zeroipc_array_t* final = zeroipc_array_create(mem, "final", sizeof(int), 1000);
    assert(final != NULL);
    
    int val = 999;
    assert(zeroipc_array_set(final, 0, &val) == ZEROIPC_OK);
    
    int* retrieved = (int*)zeroipc_array_get(final, 0);
    assert(retrieved != NULL && *retrieved == 999);
    
    zeroipc_array_close(final);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Rapid allocation/deallocation passed\n");
}

// ========== ALIGNMENT TESTS ==========

void test_alignment_boundaries() {
    printf("Testing alignment boundaries...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_boundary", 10*1024*1024, 64);
    assert(mem != NULL);
    
    // Test various alignments
    typedef struct {
        char padding[7];
        double aligned8;  // Should be 8-byte aligned
    } Aligned8;
    
    typedef struct {
        char padding[15];
        long double aligned16;  // Should be 16-byte aligned (on most systems)
    } Aligned16;
    
    zeroipc_array_t* arr8 = zeroipc_array_create(mem, "align8", sizeof(Aligned8), 100);
    zeroipc_array_t* arr16 = zeroipc_array_create(mem, "align16", sizeof(Aligned16), 100);
    
    assert(arr8 != NULL);
    assert(arr16 != NULL);
    
    // Verify data integrity with alignment
    Aligned8 val8 = { .aligned8 = 3.14159 };
    Aligned16 val16 = { .aligned16 = 2.71828 };
    
    assert(zeroipc_array_set(arr8, 50, &val8) == ZEROIPC_OK);
    assert(zeroipc_array_set(arr16, 50, &val16) == ZEROIPC_OK);
    
    Aligned8* retrieved8 = (Aligned8*)zeroipc_array_get(arr8, 50);
    Aligned16* retrieved16 = (Aligned16*)zeroipc_array_get(arr16, 50);
    
    assert(retrieved8 != NULL && retrieved8->aligned8 == val8.aligned8);
    assert(retrieved16 != NULL);
    
    zeroipc_array_close(arr8);
    zeroipc_array_close(arr16);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_boundary");
    
    printf("  ✓ Alignment boundaries passed\n");
}

// ========== MAIN ==========

int main() {
    printf("=== Memory Boundary Tests ===\n\n");
    
    // Size limit tests
    test_minimum_viable_memory();
    test_large_allocation_near_limit();
    test_maximum_queue_capacity();
    
    // Fragmentation tests
    test_memory_exhaustion();
    test_table_exhaustion();
    
    // Extreme values
    test_extreme_sizes();
    test_large_struct_types();
    
    // Concurrent boundaries
    test_concurrent_near_capacity();
    
    // Rapid operations
    test_rapid_allocation_deallocation();
    
    // Alignment
    test_alignment_boundaries();
    
    printf("\n✓ All memory boundary tests passed!\n");
    return 0;
}