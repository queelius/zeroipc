#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sched.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#include "zeroipc.h"

void test_queue_basic() {
    printf("Testing queue basic operations...\n");
    
    /* Create memory */
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_qs", 1024*1024, 64);
    assert(mem != NULL);
    
    /* Create queue */
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "int_queue", sizeof(int), 100);
    assert(queue != NULL);
    assert(zeroipc_queue_empty(queue));
    assert(!zeroipc_queue_full(queue));
    assert(zeroipc_queue_capacity(queue) == 100);
    
    /* Push values */
    int values[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        assert(zeroipc_queue_push(queue, &values[i]) == ZEROIPC_OK);
    }
    
    assert(!zeroipc_queue_empty(queue));
    assert(zeroipc_queue_size(queue) == 5);
    
    /* Pop values (FIFO) */
    int val;
    for (int i = 0; i < 5; i++) {
        assert(zeroipc_queue_pop(queue, &val) == ZEROIPC_OK);
        assert(val == values[i]);
    }
    
    assert(zeroipc_queue_empty(queue));
    
    /* Clean up */
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_qs");
    
    printf("  ✓ Queue basic operations passed\n");
}

void test_stack_basic() {
    printf("Testing stack basic operations...\n");
    
    /* Create memory */
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_stack", 1024*1024, 64);
    assert(mem != NULL);
    
    /* Create stack */
    zeroipc_stack_t* stack = zeroipc_stack_create(mem, "int_stack", sizeof(int), 100);
    assert(stack != NULL);
    assert(zeroipc_stack_empty(stack));
    assert(!zeroipc_stack_full(stack));
    assert(zeroipc_stack_capacity(stack) == 100);
    
    /* Push values */
    int values[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        assert(zeroipc_stack_push(stack, &values[i]) == ZEROIPC_OK);
    }
    
    assert(!zeroipc_stack_empty(stack));
    assert(zeroipc_stack_size(stack) == 5);
    
    /* Check top */
    int top;
    assert(zeroipc_stack_top(stack, &top) == ZEROIPC_OK);
    assert(top == 50);
    
    /* Pop values (LIFO) */
    int val;
    for (int i = 4; i >= 0; i--) {
        assert(zeroipc_stack_pop(stack, &val) == ZEROIPC_OK);
        assert(val == values[i]);
    }
    
    assert(zeroipc_stack_empty(stack));
    
    /* Clean up */
    zeroipc_stack_close(stack);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_stack");
    
    printf("  ✓ Stack basic operations passed\n");
}

/* Thread data for concurrent test */
typedef struct {
    zeroipc_queue_t* queue;
    int start;
    int count;
} thread_data_t;

void* producer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    
    for (int i = data->start; i < data->start + data->count; i++) {
        while (zeroipc_queue_push(data->queue, &i) != ZEROIPC_OK) {
            sched_yield();
        }
    }
    
    return NULL;
}

void* consumer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int sum = 0;
    int val;
    
    for (int i = 0; i < data->count; i++) {
        while (zeroipc_queue_pop(data->queue, &val) != ZEROIPC_OK) {
            sched_yield();
        }
        sum += val;
    }
    
    return (void*)(intptr_t)sum;
}

void test_queue_concurrent() {
    printf("Testing queue concurrent operations...\n");
    
    /* Create memory */
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_concurrent", 10*1024*1024, 64);
    assert(mem != NULL);
    
    /* Create queue */
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "concurrent_queue", sizeof(int), 1000);
    assert(queue != NULL);
    
    /* Create threads */
    pthread_t producer, consumer;
    thread_data_t prod_data = {queue, 0, 10000};
    thread_data_t cons_data = {queue, 0, 10000};
    
    pthread_create(&producer, NULL, producer_thread, &prod_data);
    pthread_create(&consumer, NULL, consumer_thread, &cons_data);
    
    /* Wait for threads */
    pthread_join(producer, NULL);
    void* result;
    pthread_join(consumer, &result);
    
    /* Verify sum */
    int expected_sum = 0;
    for (int i = 0; i < 10000; i++) {
        expected_sum += i;
    }
    assert((intptr_t)result == expected_sum);
    
    assert(zeroipc_queue_empty(queue));
    
    /* Clean up */
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_concurrent");
    
    printf("  ✓ Queue concurrent operations passed\n");
}

typedef struct {
    zeroipc_queue_t* queue;
    int id;
    int count;
} producer_data_t;

typedef struct {
    zeroipc_queue_t* queue;
    int expected_total;
    _Atomic int* consumed;
    _Atomic uint64_t* checksum;
} consumer_data_t;

void* mpmc_producer(void* arg) {
    producer_data_t* data = (producer_data_t*)arg;
    for (int i = 0; i < data->count; ++i) {
        uint32_t value = ((uint32_t)data->id << 24) | (uint32_t)i;
        while (zeroipc_queue_push(data->queue, &value) != ZEROIPC_OK) {
            sched_yield();
        }
    }
    return NULL;
}

void* mpmc_consumer(void* arg) {
    consumer_data_t* data = (consumer_data_t*)arg;
    uint32_t value;
    while (atomic_load_explicit(data->consumed, memory_order_acquire) < data->expected_total) {
        if (zeroipc_queue_pop(data->queue, &value) == ZEROIPC_OK) {
            atomic_fetch_add_explicit(data->consumed, 1, memory_order_acq_rel);
            atomic_fetch_add_explicit(data->checksum, (uint64_t)value, memory_order_acq_rel);
        } else {
            sched_yield();
        }
    }
    return NULL;
}

void test_queue_mpmc() {
    printf("Testing queue multi-producer/multi-consumer operations...\n");

    zeroipc_memory_t* mem = zeroipc_memory_create("/test_mpmc", 10*1024*1024, 64);
    assert(mem != NULL);

    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "mpmc_queue", sizeof(uint32_t), 4096);
    assert(queue != NULL);

    const int producers = 4;
    const int consumers = 4;
    const int per_producer = 2000;
    const int expected_total = producers * per_producer;

    _Atomic int consumed = 0;
    _Atomic uint64_t checksum = 0;

    pthread_t prod_threads[producers];
    pthread_t cons_threads[consumers];
    producer_data_t prod_data[producers];
    consumer_data_t cons_data[consumers];

    for (int i = 0; i < producers; ++i) {
        prod_data[i].queue = queue;
        prod_data[i].id = i;
        prod_data[i].count = per_producer;
        pthread_create(&prod_threads[i], NULL, mpmc_producer, &prod_data[i]);
    }

    for (int i = 0; i < consumers; ++i) {
        cons_data[i].queue = queue;
        cons_data[i].expected_total = expected_total;
        cons_data[i].consumed = &consumed;
        cons_data[i].checksum = &checksum;
        pthread_create(&cons_threads[i], NULL, mpmc_consumer, &cons_data[i]);
    }

    for (int i = 0; i < producers; ++i) {
        pthread_join(prod_threads[i], NULL);
    }

    /* Wait for consumers with timeout to avoid hanging on regressions */
    int wait_ms = 0;
    while (atomic_load_explicit(&consumed, memory_order_acquire) < expected_total && wait_ms < 5000) {
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000L * 50}; /* 50ms */
        nanosleep(&ts, NULL);
        wait_ms += 50;
    }
    assert(atomic_load_explicit(&consumed, memory_order_acquire) == expected_total);

    for (int i = 0; i < consumers; ++i) {
        pthread_join(cons_threads[i], NULL);
    }

    assert(atomic_load_explicit(&consumed, memory_order_acquire) == expected_total);

    /* Compute expected checksum */
    uint64_t expected_checksum = 0;
    for (int id = 0; id < producers; ++id) {
        uint64_t base = (uint64_t)id << 24;
        for (int i = 0; i < per_producer; ++i) {
            expected_checksum += base + (uint64_t)i;
        }
    }
    assert(atomic_load_explicit(&checksum, memory_order_acquire) == expected_checksum);

    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_mpmc");

    printf("  ✓ Queue MPMC operations passed\n");
}

typedef struct {
    zeroipc_stack_t* stack;
    int id;
    int count;
    _Atomic int* produced;
} stack_producer_data_t;

typedef struct {
    zeroipc_stack_t* stack;
    int expected_total;
    _Atomic int* consumed;
    _Atomic uint64_t* checksum;
} stack_consumer_data_t;

void* stack_producer(void* arg) {
    stack_producer_data_t* data = (stack_producer_data_t*)arg;
    for (int i = 0; i < data->count; ++i) {
        uint32_t value = ((uint32_t)data->id << 24) | (uint32_t)i;
        while (zeroipc_stack_push(data->stack, &value) != ZEROIPC_OK) {
            sched_yield();
        }
        atomic_fetch_add_explicit(data->produced, 1, memory_order_acq_rel);
    }
    return NULL;
}

void* stack_consumer(void* arg) {
    stack_consumer_data_t* data = (stack_consumer_data_t*)arg;
    uint32_t value;
    while (atomic_load_explicit(data->consumed, memory_order_acquire) < data->expected_total) {
        if (zeroipc_stack_pop(data->stack, &value) == ZEROIPC_OK) {
            atomic_fetch_add_explicit(data->consumed, 1, memory_order_acq_rel);
            atomic_fetch_add_explicit(data->checksum, (uint64_t)value, memory_order_acq_rel);
        } else {
            sched_yield();
        }
    }
    return NULL;
}

void test_stack_mpmc() {
    printf("Testing stack multi-producer/multi-consumer operations...\n");

    zeroipc_memory_t* mem = zeroipc_memory_create("/test_stack_mpmc", 10*1024*1024, 64);
    assert(mem != NULL);

    zeroipc_stack_t* stack = zeroipc_stack_create(mem, "mpmc_stack", sizeof(uint32_t), 4096);
    assert(stack != NULL);

    const int producers = 4;
    const int consumers = 4;
    const int per_producer = 2000;
    const int expected_total = producers * per_producer;

    _Atomic int consumed = 0;
    _Atomic int produced = 0;
    _Atomic uint64_t checksum = 0;

    pthread_t prod_threads[producers];
    pthread_t cons_threads[consumers];
    stack_producer_data_t prod_data[producers];
    stack_consumer_data_t cons_data[consumers];

    for (int i = 0; i < producers; ++i) {
        prod_data[i].stack = stack;
        prod_data[i].id = i;
        prod_data[i].count = per_producer;
        prod_data[i].produced = &produced;
        pthread_create(&prod_threads[i], NULL, stack_producer, &prod_data[i]);
    }

    for (int i = 0; i < consumers; ++i) {
        cons_data[i].stack = stack;
        cons_data[i].expected_total = expected_total;
        cons_data[i].consumed = &consumed;
        cons_data[i].checksum = &checksum;
        pthread_create(&cons_threads[i], NULL, stack_consumer, &cons_data[i]);
    }

    /* Wait with timeout to avoid hangs on regressions */
    int wait_ms = 0;
    while (wait_ms < 5000 &&
           (atomic_load_explicit(&produced, memory_order_acquire) < expected_total ||
            atomic_load_explicit(&consumed, memory_order_acquire) < expected_total)) {
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000L * 50}; /* 50ms */
        nanosleep(&ts, NULL);
        wait_ms += 50;
    }

    int produced_count = atomic_load_explicit(&produced, memory_order_acquire);
    int consumed_count = atomic_load_explicit(&consumed, memory_order_acquire);
    if (produced_count != expected_total || consumed_count != expected_total) {
        size_t top = zeroipc_stack_size(stack);
        fprintf(stderr, "  ✗ Stack MPMC stalled (produced=%d consumed=%d top=%llu)\n",
                produced_count, consumed_count, (unsigned long long)top);
        fflush(stderr);
        assert(0 && "Stack MPMC stalled");
    }

    for (int i = 0; i < producers; ++i) {
        pthread_join(prod_threads[i], NULL);
    }
    for (int i = 0; i < consumers; ++i) {
        pthread_join(cons_threads[i], NULL);
    }

    uint64_t expected_checksum = 0;
    for (int id = 0; id < producers; ++id) {
        uint64_t base = (uint64_t)id << 24;
        for (int i = 0; i < per_producer; ++i) {
            expected_checksum += base + (uint64_t)i;
        }
    }
    assert(atomic_load_explicit(&checksum, memory_order_acquire) == expected_checksum);

    zeroipc_stack_close(stack);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_stack_mpmc");

    printf("  ✓ Stack MPMC operations passed\n");
}

int main() {
    printf("=== ZeroIPC C Queue/Stack Tests ===\n\n");
    
    test_queue_basic();
    test_stack_basic();
    test_queue_concurrent();
    test_queue_mpmc();
    test_stack_mpmc();
    
    printf("\n✓ All Queue/Stack tests passed!\n");
    return 0;
}
