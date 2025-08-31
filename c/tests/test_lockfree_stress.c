#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include "../include/zeroipc.h"

#define NUM_THREADS 8
#define ITEMS_PER_THREAD 10000
#define QUEUE_SIZE 1000

// Shared state
zeroipc_memory_t* mem;
zeroipc_queue_t* queue;
zeroipc_stack_t* stack;
atomic_int produced = 0;
atomic_int consumed = 0;
atomic_int pushed = 0;
atomic_int popped = 0;

void* queue_producer(void* arg) {
    int thread_id = *(int*)arg;
    int base = thread_id * ITEMS_PER_THREAD;
    
    for (int i = 0; i < ITEMS_PER_THREAD; i++) {
        int value = base + i;
        while (zeroipc_queue_push(queue, &value) != ZEROIPC_OK) {
            usleep(1); // Brief yield
        }
        atomic_fetch_add(&produced, 1);
    }
    
    return NULL;
}

void* queue_consumer(void* arg) {
    (void)arg;
    
    for (int i = 0; i < ITEMS_PER_THREAD; i++) {
        int value;
        while (zeroipc_queue_pop(queue, &value) != ZEROIPC_OK) {
            usleep(1); // Brief yield
        }
        atomic_fetch_add(&consumed, 1);
    }
    
    return NULL;
}

void* stack_pusher(void* arg) {
    int thread_id = *(int*)arg;
    int base = thread_id * ITEMS_PER_THREAD;
    
    for (int i = 0; i < ITEMS_PER_THREAD; i++) {
        int value = base + i;
        while (zeroipc_stack_push(stack, &value) != ZEROIPC_OK) {
            usleep(1); // Brief yield
        }
        atomic_fetch_add(&pushed, 1);
    }
    
    return NULL;
}

void* stack_popper(void* arg) {
    (void)arg;
    
    for (int i = 0; i < ITEMS_PER_THREAD; i++) {
        int value;
        while (zeroipc_stack_pop(stack, &value) != ZEROIPC_OK) {
            usleep(1); // Brief yield
        }
        atomic_fetch_add(&popped, 1);
    }
    
    return NULL;
}

void test_queue_stress() {
    printf("Testing Queue with %d threads (%d items each)...\n", 
           NUM_THREADS, ITEMS_PER_THREAD);
    
    // Reset counters
    atomic_store(&produced, 0);
    atomic_store(&consumed, 0);
    
    // Create queue
    queue = zeroipc_queue_create(mem, "stress_queue", sizeof(int), QUEUE_SIZE);
    assert(queue != NULL);
    
    pthread_t producers[NUM_THREADS/2];
    pthread_t consumers[NUM_THREADS/2];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
    }
    
    // Start producers
    for (int i = 0; i < NUM_THREADS/2; i++) {
        pthread_create(&producers[i], NULL, queue_producer, &thread_ids[i]);
    }
    
    // Start consumers
    for (int i = 0; i < NUM_THREADS/2; i++) {
        pthread_create(&consumers[i], NULL, queue_consumer, &thread_ids[i]);
    }
    
    // Wait for all threads
    for (int i = 0; i < NUM_THREADS/2; i++) {
        pthread_join(producers[i], NULL);
        pthread_join(consumers[i], NULL);
    }
    
    int total_expected = (NUM_THREADS/2) * ITEMS_PER_THREAD;
    printf("  Produced: %d, Consumed: %d (expected: %d each)\n", 
           atomic_load(&produced), atomic_load(&consumed), total_expected);
    
    assert(atomic_load(&produced) == total_expected);
    assert(atomic_load(&consumed) == total_expected);
    assert(zeroipc_queue_empty(queue));
    
    zeroipc_queue_close(queue);
    printf("  ✓ Queue stress test passed\n");
}

void test_stack_stress() {
    printf("Testing Stack with %d threads (%d items each)...\n", 
           NUM_THREADS, ITEMS_PER_THREAD);
    
    // Reset counters
    atomic_store(&pushed, 0);
    atomic_store(&popped, 0);
    
    // Create stack
    stack = zeroipc_stack_create(mem, "stress_stack", sizeof(int), QUEUE_SIZE);
    assert(stack != NULL);
    
    pthread_t pushers[NUM_THREADS/2];
    pthread_t poppers[NUM_THREADS/2];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
    }
    
    // Start pushers
    for (int i = 0; i < NUM_THREADS/2; i++) {
        pthread_create(&pushers[i], NULL, stack_pusher, &thread_ids[i]);
    }
    
    // Start poppers
    for (int i = 0; i < NUM_THREADS/2; i++) {
        pthread_create(&poppers[i], NULL, stack_popper, &thread_ids[i]);
    }
    
    // Wait for all threads
    for (int i = 0; i < NUM_THREADS/2; i++) {
        pthread_join(pushers[i], NULL);
        pthread_join(poppers[i], NULL);
    }
    
    int total_expected = (NUM_THREADS/2) * ITEMS_PER_THREAD;
    printf("  Pushed: %d, Popped: %d (expected: %d each)\n", 
           atomic_load(&pushed), atomic_load(&popped), total_expected);
    
    assert(atomic_load(&pushed) == total_expected);
    assert(atomic_load(&popped) == total_expected);
    assert(zeroipc_stack_empty(stack));
    
    zeroipc_stack_close(stack);
    printf("  ✓ Stack stress test passed\n");
}

// High contention test data
typedef struct {
    zeroipc_queue_t* queue;
    atomic_int* push_count;
    atomic_int* pop_count;
    int items;
} contention_data_t;

void* high_contention_producer(void* arg) {
    contention_data_t* data = (contention_data_t*)arg;
    for (int j = 0; j < data->items; j++) {
        int val = j;
        while (zeroipc_queue_push(data->queue, &val) != ZEROIPC_OK) {
            usleep(1);
        }
        atomic_fetch_add(data->push_count, 1);
    }
    return NULL;
}

void* high_contention_consumer(void* arg) {
    contention_data_t* data = (contention_data_t*)arg;
    for (int j = 0; j < data->items; j++) {
        int val;
        while (zeroipc_queue_pop(data->queue, &val) != ZEROIPC_OK) {
            usleep(1);
        }
        atomic_fetch_add(data->pop_count, 1);
    }
    return NULL;
}

// Test with high contention
void test_high_contention() {
    printf("Testing high contention (16 threads, small queue)...\n");
    
    zeroipc_queue_t* small_queue = zeroipc_queue_create(mem, "small_queue", sizeof(int), 10);
    assert(small_queue != NULL);
    
    const int num_threads = 16;
    const int items = 1000;
    atomic_int push_count = 0;
    atomic_int pop_count = 0;
    
    contention_data_t data = {
        .queue = small_queue,
        .push_count = &push_count,
        .pop_count = &pop_count,
        .items = items
    };
    
    pthread_t threads[num_threads];
    
    // Mixed push/pop threads
    for (int i = 0; i < num_threads; i++) {
        if (i % 2 == 0) {
            // Producer
            pthread_create(&threads[i], NULL, high_contention_producer, &data);
        } else {
            // Consumer
            pthread_create(&threads[i], NULL, high_contention_consumer, &data);
        }
    }
    
    // Wait for all
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    int expected = (num_threads/2) * items;
    printf("  Push: %d, Pop: %d (expected: %d each)\n",
           atomic_load(&push_count), atomic_load(&pop_count), expected);
    
    assert(atomic_load(&push_count) == expected);
    assert(atomic_load(&pop_count) == expected);
    
    zeroipc_queue_close(small_queue);
    printf("  ✓ High contention test passed\n");
}

int main() {
    printf("=== Lock-Free Stress Tests ===\n\n");
    
    // Create shared memory
    mem = zeroipc_memory_create("/stress_test", 100 * 1024 * 1024, 128);
    assert(mem != NULL);
    
    test_queue_stress();
    test_stack_stress();
    test_high_contention();
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/stress_test");
    
    printf("\n✓ All stress tests passed!\n");
    return 0;
}