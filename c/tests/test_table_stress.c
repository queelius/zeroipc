#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "../include/zeroipc.h"

// ========== TABLE CAPACITY TESTS ==========

void test_table_fill_to_capacity() {
    printf("Testing table fill to capacity...\n");
    
    // Create memory with small table (16 entries)
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table_stress", 10*1024*1024, 16);
    assert(mem != NULL);
    
    zeroipc_array_t* arrays[20];
    int created = 0;
    
    // Try to create more than capacity
    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, sizeof(name), "array_%d", i);
        
        arrays[i] = zeroipc_array_create(mem, name, sizeof(int), 10);
        if (arrays[i] == NULL) {
            // Expected when table is full
            break;
        }
        created++;
    }
    
    // Should have created around 16 entries
    assert(created >= 15 && created <= 16);
    printf("  Created %d entries in 16-entry table\n", created);
    
    // Verify all created arrays are still accessible
    for (int i = 0; i < created; i++) {
        int value = i * 100;
        assert(zeroipc_array_set(arrays[i], 0, &value) == ZEROIPC_OK);
        
        int* retrieved = (int*)zeroipc_array_get(arrays[i], 0);
        assert(retrieved != NULL && *retrieved == value);
    }
    
    // Cleanup
    for (int i = 0; i < created; i++) {
        zeroipc_array_close(arrays[i]);
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table_stress");
    
    printf("  ✓ Table fill to capacity passed\n");
}

void test_table_name_collisions() {
    printf("Testing table name collisions...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table_stress", 10*1024*1024, 64);
    
    // Create first structure
    zeroipc_array_t* arr1 = zeroipc_array_create(mem, "duplicate_name", sizeof(int), 100);
    assert(arr1 != NULL);
    
    int value = 42;
    zeroipc_array_set(arr1, 0, &value);
    
    // Try to create with same name - should fail
    zeroipc_array_t* arr2 = zeroipc_array_create(mem, "duplicate_name", sizeof(int), 200);
    assert(arr2 == NULL);
    
    // Original should still work
    int* retrieved = (int*)zeroipc_array_get(arr1, 0);
    assert(retrieved != NULL && *retrieved == 42);
    
    zeroipc_array_close(arr1);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table_stress");
    
    printf("  ✓ Table name collisions passed\n");
}

void test_table_long_names() {
    printf("Testing table long names...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table_stress", 10*1024*1024, 64);
    
    // Maximum name length (31 chars + null)
    char max_name[32];
    memset(max_name, 'A', 31);
    max_name[31] = '\0';
    
    zeroipc_array_t* arr1 = zeroipc_array_create(mem, max_name, sizeof(int), 10);
    assert(arr1 != NULL);
    
    int val1 = 100;
    zeroipc_array_set(arr1, 0, &val1);
    
    // Name that's too long (should be rejected)
    char long_name[101];
    memset(long_name, 'B', 100);
    long_name[100] = '\0';
    
    zeroipc_array_t* arr2 = zeroipc_array_create(mem, long_name, sizeof(int), 10);
    assert(arr2 == NULL);  // Should fail - name too long
    
    // Use a shorter name that fits
    char short_name[32] = "B_truncated";
    arr2 = zeroipc_array_create(mem, short_name, sizeof(int), 10);
    assert(arr2 != NULL);
    
    int val2 = 200;
    zeroipc_array_set(arr2, 0, &val2);
    
    // Both should work
    int* ret1 = (int*)zeroipc_array_get(arr1, 0);
    int* ret2 = (int*)zeroipc_array_get(arr2, 0);
    assert(ret1 != NULL && *ret1 == 100);
    assert(ret2 != NULL && *ret2 == 200);
    
    // Try to find by the correct short name
    zeroipc_array_t* arr2_ref = zeroipc_array_open(mem, short_name);
    assert(arr2_ref != NULL);
    
    // Note: array_open doesn't preserve elem_size, so we can't use array_get
    // Just verify it exists
    assert(zeroipc_array_data(arr2_ref) != NULL);
    
    zeroipc_array_close(arr1);
    zeroipc_array_close(arr2);
    zeroipc_array_close(arr2_ref);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table_stress");
    
    printf("  ✓ Table long names passed\n");
}

// ========== CONCURRENT TABLE ACCESS ==========

typedef struct {
    zeroipc_memory_t* mem;
    int thread_id;
    int structures_per_thread;
    int* successes;
    int* failures;
    pthread_mutex_t* mutex;
} concurrent_create_data_t;

void* concurrent_create_thread(void* arg) {
    concurrent_create_data_t* data = (concurrent_create_data_t*)arg;
    
    int local_success = 0;
    int local_failure = 0;
    
    for (int i = 0; i < data->structures_per_thread; i++) {
        char name[64];
        snprintf(name, sizeof(name), "t%d_s%d", data->thread_id, i);
        
        // Randomly create different structure types
        int type = (data->thread_id + i) % 3;
        
        void* structure = NULL;
        if (type == 0) {
            structure = zeroipc_array_create(data->mem, name, sizeof(int), 100);
            if (structure) {
                int value = data->thread_id * 1000 + i;
                zeroipc_array_set(structure, 0, &value);
                zeroipc_array_close(structure);
            }
        } else if (type == 1) {
            structure = zeroipc_queue_create(data->mem, name, sizeof(int), 100);
            if (structure) {
                int value = data->thread_id * 1000 + i;
                zeroipc_queue_push(structure, &value);
                zeroipc_queue_close(structure);
            }
        } else {
            structure = zeroipc_stack_create(data->mem, name, sizeof(int), 100);
            if (structure) {
                int value = data->thread_id * 1000 + i;
                zeroipc_stack_push(structure, &value);
                zeroipc_stack_close(structure);
            }
        }
        
        if (structure != NULL) {
            local_success++;
        } else {
            local_failure++;
        }
    }
    
    pthread_mutex_lock(data->mutex);
    *data->successes += local_success;
    *data->failures += local_failure;
    pthread_mutex_unlock(data->mutex);
    
    return NULL;
}

void test_concurrent_table_creation() {
    printf("Testing concurrent table creation...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table_stress", 50*1024*1024, 128);
    
    const int num_threads = 10;
    const int structures_per_thread = 10;
    
    int successes = 0;
    int failures = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    pthread_t threads[num_threads];
    concurrent_create_data_t thread_data[num_threads];
    
    for (int t = 0; t < num_threads; t++) {
        thread_data[t].mem = mem;
        thread_data[t].thread_id = t;
        thread_data[t].structures_per_thread = structures_per_thread;
        thread_data[t].successes = &successes;
        thread_data[t].failures = &failures;
        thread_data[t].mutex = &mutex;
        
        pthread_create(&threads[t], NULL, concurrent_create_thread, &thread_data[t]);
    }
    
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    
    printf("  Concurrent creation: %d successes, %d failures\n", successes, failures);
    
    // Most should succeed (table has 128 entries)
    assert(successes > 50);
    
    pthread_mutex_destroy(&mutex);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table_stress");
    
    printf("  ✓ Concurrent table creation passed\n");
}

// ========== RAPID TABLE OPERATIONS ==========

void test_rapid_table_churn() {
    printf("Testing rapid table churn...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table_stress", 20*1024*1024, 32);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Rapidly create structures with reused names
    const int iterations = 1000;
    for (int i = 0; i < iterations; i++) {
        char name[32];
        snprintf(name, sizeof(name), "churn_%d", i % 10);
        
        // Create different types in sequence with same name
        zeroipc_array_t* arr = zeroipc_array_create(mem, name, sizeof(int), 10);
        if (arr) {
            int val = i;
            zeroipc_array_set(arr, 0, &val);
            zeroipc_array_close(arr);
        }
        
        zeroipc_queue_t* queue = zeroipc_queue_create(mem, name, sizeof(double), 10);
        if (queue) {
            double val = i * 3.14;
            zeroipc_queue_push(queue, &val);
            zeroipc_queue_close(queue);
        }
        
        zeroipc_stack_t* stack = zeroipc_stack_create(mem, name, sizeof(char), 10);
        if (stack) {
            char val = 'A' + (i % 26);
            zeroipc_stack_push(stack, &val);
            zeroipc_stack_close(stack);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    
    printf("  Completed %d table operations in %ldms\n", iterations * 3, duration_ms);
    
    double ops_per_sec = (iterations * 3 * 1000.0) / duration_ms;
    printf("  Table throughput: %.0f ops/sec\n", ops_per_sec);
    
    // Should complete reasonably fast
    assert(duration_ms < 5000);  // Less than 5 seconds
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table_stress");
    
    printf("  ✓ Rapid table churn passed\n");
}

// ========== TABLE PATTERN TESTS ==========

void test_table_access_patterns() {
    printf("Testing table access patterns...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table_stress", 10*1024*1024, 64);
    
    // Sequential pattern
    for (int i = 0; i < 30; i++) {
        char name[32];
        snprintf(name, sizeof(name), "seq_%d", i);
        
        zeroipc_array_t* arr = zeroipc_array_create(mem, name, sizeof(int), 5);
        if (arr) {
            int val = i;
            zeroipc_array_set(arr, 0, &val);
            zeroipc_array_close(arr);
        }
    }
    
    // Random pattern
    srand(42);
    for (int i = 0; i < 100; i++) {
        int idx = rand() % 30;
        char name[32];
        snprintf(name, sizeof(name), "seq_%d", idx);
        
        zeroipc_array_t* arr = zeroipc_array_open(mem, name);
        if (arr) {
            int* val = (int*)zeroipc_array_get(arr, 0);
            assert(val != NULL && *val == idx);
            zeroipc_array_close(arr);
        }
    }
    
    // Batch pattern (multiple lookups of same name)
    for (int i = 0; i < 50; i++) {
        zeroipc_array_t* arr = zeroipc_array_open(mem, "seq_15");
        if (arr) {
            int* val = (int*)zeroipc_array_get(arr, 0);
            assert(val != NULL && *val == 15);
            zeroipc_array_close(arr);
        }
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table_stress");
    
    printf("  ✓ Table access patterns passed\n");
}

// ========== MIXED TYPE TABLE ==========

void test_mixed_type_table() {
    printf("Testing mixed type table...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table_stress", 20*1024*1024, 128);
    
    // Create mix of all structure types
    for (int i = 0; i < 30; i++) {
        char base_name[32];
        char full_name[64];
        
        snprintf(base_name, sizeof(base_name), "mixed_%d", i);
        
        // Array
        snprintf(full_name, sizeof(full_name), "%s_arr", base_name);
        zeroipc_array_t* arr = zeroipc_array_create(mem, full_name, sizeof(int), 10);
        if (arr) {
            int val = i;
            zeroipc_array_set(arr, 0, &val);
            zeroipc_array_close(arr);
        }
        
        // Queue
        snprintf(full_name, sizeof(full_name), "%s_queue", base_name);
        zeroipc_queue_t* queue = zeroipc_queue_create(mem, full_name, sizeof(double), 10);
        if (queue) {
            double val = i * 2.5;
            zeroipc_queue_push(queue, &val);
            zeroipc_queue_close(queue);
        }
        
        // Stack
        snprintf(full_name, sizeof(full_name), "%s_stack", base_name);
        zeroipc_stack_t* stack = zeroipc_stack_create(mem, full_name, sizeof(char), 10);
        if (stack) {
            char val = 'A' + i;
            zeroipc_stack_push(stack, &val);
            zeroipc_stack_close(stack);
        }
    }
    
    // Verify all can be accessed
    for (int i = 0; i < 30; i++) {
        char base_name[32];
        char full_name[64];
        
        snprintf(base_name, sizeof(base_name), "mixed_%d", i);
        
        // Array
        snprintf(full_name, sizeof(full_name), "%s_arr", base_name);
        zeroipc_array_t* arr = zeroipc_array_open(mem, full_name);
        assert(arr != NULL);
        int* arr_val = (int*)zeroipc_array_get(arr, 0);
        assert(arr_val != NULL && *arr_val == i);
        zeroipc_array_close(arr);
        
        // Queue
        snprintf(full_name, sizeof(full_name), "%s_queue", base_name);
        zeroipc_queue_t* queue = zeroipc_queue_open(mem, full_name);
        assert(queue != NULL);
        double qval;
        assert(zeroipc_queue_pop(queue, &qval) == ZEROIPC_OK);
        assert(qval == i * 2.5);
        zeroipc_queue_close(queue);
        
        // Stack
        snprintf(full_name, sizeof(full_name), "%s_stack", base_name);
        zeroipc_stack_t* stack = zeroipc_stack_open(mem, full_name);
        assert(stack != NULL);
        char sval;
        assert(zeroipc_stack_pop(stack, &sval) == ZEROIPC_OK);
        assert(sval == 'A' + i);
        zeroipc_stack_close(stack);
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table_stress");
    
    printf("  ✓ Mixed type table passed\n");
}

// ========== MAIN ==========

int main() {
    printf("=== Table Stress Tests ===\n\n");
    
    // Table capacity tests
    test_table_fill_to_capacity();
    test_table_name_collisions();
    test_table_long_names();
    
    // Concurrent access
    test_concurrent_table_creation();
    
    // Rapid operations
    test_rapid_table_churn();
    
    // Access patterns
    test_table_access_patterns();
    
    // Mixed types
    test_mixed_type_table();
    
    printf("\n✓ All table stress tests passed!\n");
    return 0;
}