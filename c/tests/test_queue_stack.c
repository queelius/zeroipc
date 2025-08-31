#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
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
            usleep(100);
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
            usleep(100);
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

int main() {
    printf("=== ZeroIPC C Queue/Stack Tests ===\n\n");
    
    test_queue_basic();
    test_stack_basic();
    test_queue_concurrent();
    
    printf("\n✓ All Queue/Stack tests passed!\n");
    return 0;
}