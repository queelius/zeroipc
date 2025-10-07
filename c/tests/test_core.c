/**
 * Test suite for the elegant ZeroIPC Core API
 *
 * Tests composability, simplicity, and cross-language compatibility
 */

#include "zeroipc_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>

#define TEST_SHM "/test_core"
#define TEST_SIZE (1024 * 1024)  // 1MB

/* Test colors for output */
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define RESET "\033[0m"

/* Test result tracking */
static int tests_run = 0;
static int tests_passed = 0;

/* Helper macro for test assertions */
#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        printf(GREEN "  ✓ " RESET "%s\n", message); \
        tests_passed++; \
    } else { \
        printf(RED "  ✗ " RESET "%s (line %d)\n", message, __LINE__); \
    } \
} while(0)

/* ============================================================================
 * Test 1: Memory Management
 * ============================================================================ */

void test_memory_management(void) {
    printf("\n=== Test 1: Memory Management ===\n");

    /* Create new shared memory */
    zipc_shm_t shm = zipc_open(TEST_SHM, TEST_SIZE, 128);
    TEST_ASSERT(shm != NULL, "Create shared memory");
    TEST_ASSERT(zipc_size(shm) >= TEST_SIZE, "Memory size correct");
    TEST_ASSERT(zipc_raw(shm) != NULL, "Get raw memory pointer");

    /* Open existing memory from another handle */
    zipc_shm_t shm2 = zipc_open(TEST_SHM, 0, 0);
    TEST_ASSERT(shm2 != NULL, "Open existing memory");
    TEST_ASSERT(zipc_size(shm2) == zipc_size(shm), "Sizes match");

    /* Close handles */
    zipc_close(shm);
    zipc_close(shm2);

    /* Clean up */
    zipc_destroy(TEST_SHM);

    /* Verify destroyed */
    zipc_shm_t shm3 = zipc_open(TEST_SHM, 0, 0);
    TEST_ASSERT(shm3 == NULL, "Memory properly destroyed");
}

/* ============================================================================
 * Test 2: Array Operations
 * ============================================================================ */

void test_array_operations(void) {
    printf("\n=== Test 2: Array Operations ===\n");

    zipc_shm_t shm = zipc_open(TEST_SHM, TEST_SIZE, 64);
    assert(shm != NULL);

    /* Create array of integers */
    zipc_view_t array = zipc_create(shm, "int_array", ZIPC_TYPE_ARRAY,
                                    sizeof(int), 100);
    TEST_ASSERT(array != NULL, "Create integer array");
    TEST_ASSERT(zipc_view_capacity(array) == 100, "Array capacity correct");
    TEST_ASSERT(zipc_view_elemsize(array) == sizeof(int), "Element size correct");

    /* Set and get values */
    for (int i = 0; i < 10; i++) {
        int value = i * i;
        zipc_result res = zipc_array_set(array, i, &value);
        TEST_ASSERT(res == ZIPC_OK, "Set array element");
    }

    int retrieved;
    for (int i = 0; i < 10; i++) {
        zipc_result res = zipc_array_get(array, i, &retrieved);
        TEST_ASSERT(res == ZIPC_OK && retrieved == i * i, "Get array element");
    }

    /* Direct pointer access */
    int* data = (int*)zipc_view_data(array);
    TEST_ASSERT(data != NULL, "Get array data pointer");
    TEST_ASSERT(data[5] == 25, "Direct pointer access works");

    /* Bounds checking */
    zipc_result res = zipc_array_set(array, 100, &retrieved);
    TEST_ASSERT(res == ZIPC_INVALID, "Bounds checking works");

    zipc_view_close(array);

    /* Open existing array */
    zipc_view_t array2 = zipc_get(shm, "int_array", ZIPC_TYPE_ARRAY, sizeof(int));
    TEST_ASSERT(array2 != NULL, "Open existing array");

    zipc_array_get(array2, 7, &retrieved);
    TEST_ASSERT(retrieved == 49, "Data persisted correctly");

    zipc_view_close(array2);
    zipc_close(shm);
    zipc_destroy(TEST_SHM);
}

/* ============================================================================
 * Test 3: Queue Operations (Lock-free MPMC)
 * ============================================================================ */

typedef struct {
    zipc_view_t queue;
    int thread_id;
    int operations;
} queue_thread_args_t;

void* queue_producer(void* arg) {
    queue_thread_args_t* args = (queue_thread_args_t*)arg;

    for (int i = 0; i < args->operations; i++) {
        int value = args->thread_id * 1000 + i;
        while (zipc_queue_push(args->queue, &value) != ZIPC_OK) {
            usleep(10);  // Queue full, retry
        }
    }

    return NULL;
}

void* queue_consumer(void* arg) {
    queue_thread_args_t* args = (queue_thread_args_t*)arg;

    for (int i = 0; i < args->operations; i++) {
        int value;
        while (zipc_queue_pop(args->queue, &value) != ZIPC_OK) {
            usleep(10);  // Queue empty, retry
        }
    }

    return NULL;
}

void test_queue_operations(void) {
    printf("\n=== Test 3: Queue Operations (Lock-free MPMC) ===\n");

    zipc_shm_t shm = zipc_open(TEST_SHM, TEST_SIZE, 64);
    assert(shm != NULL);

    /* Create queue */
    zipc_view_t queue = zipc_create(shm, "test_queue", ZIPC_TYPE_QUEUE,
                                    sizeof(int), 50);
    TEST_ASSERT(queue != NULL, "Create queue");
    TEST_ASSERT(zipc_queue_empty(queue), "Queue initially empty");
    TEST_ASSERT(!zipc_queue_full(queue), "Queue not full");

    /* Single-threaded operations */
    for (int i = 0; i < 10; i++) {
        zipc_result res = zipc_queue_push(queue, &i);
        TEST_ASSERT(res == ZIPC_OK, "Push to queue");
    }

    TEST_ASSERT(zipc_queue_size(queue) == 10, "Queue size correct");

    int value;
    for (int i = 0; i < 10; i++) {
        zipc_result res = zipc_queue_pop(queue, &value);
        TEST_ASSERT(res == ZIPC_OK && value == i, "Pop from queue in order");
    }

    TEST_ASSERT(zipc_queue_empty(queue), "Queue empty after pops");

    /* Multi-threaded stress test */
    printf("  Running multi-threaded stress test...\n");

    #define NUM_THREADS 4
    #define OPS_PER_THREAD 100

    pthread_t producers[NUM_THREADS];
    pthread_t consumers[NUM_THREADS];
    queue_thread_args_t args[NUM_THREADS];

    /* Start producers and consumers */
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].queue = queue;
        args[i].thread_id = i;
        args[i].operations = OPS_PER_THREAD;

        pthread_create(&producers[i], NULL, queue_producer, &args[i]);
        pthread_create(&consumers[i], NULL, queue_consumer, &args[i]);
    }

    /* Wait for completion */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(producers[i], NULL);
        pthread_join(consumers[i], NULL);
    }

    TEST_ASSERT(zipc_queue_empty(queue), "Queue empty after stress test");
    printf("  Stress test: %d threads, %d ops each\n",
           NUM_THREADS * 2, OPS_PER_THREAD);

    zipc_view_close(queue);
    zipc_close(shm);
    zipc_destroy(TEST_SHM);
}

/* ============================================================================
 * Test 4: Stack Operations (Lock-free LIFO)
 * ============================================================================ */

void test_stack_operations(void) {
    printf("\n=== Test 4: Stack Operations (Lock-free LIFO) ===\n");

    zipc_shm_t shm = zipc_open(TEST_SHM, TEST_SIZE, 64);
    assert(shm != NULL);

    /* Create stack */
    zipc_view_t stack = zipc_create(shm, "test_stack", ZIPC_TYPE_STACK,
                                    sizeof(double), 20);
    TEST_ASSERT(stack != NULL, "Create stack");
    TEST_ASSERT(zipc_stack_empty(stack), "Stack initially empty");

    /* Push values */
    for (int i = 0; i < 10; i++) {
        double value = i * 3.14159;
        zipc_result res = zipc_stack_push(stack, &value);
        TEST_ASSERT(res == ZIPC_OK, "Push to stack");
    }

    TEST_ASSERT(zipc_stack_size(stack) == 10, "Stack size correct");

    /* Peek at top */
    double top_value;
    zipc_result res = zipc_stack_top(stack, &top_value);
    TEST_ASSERT(res == ZIPC_OK, "Peek at stack top");
    TEST_ASSERT(top_value > 28.0 && top_value < 29.0, "Top value correct");

    /* Pop values (LIFO order) */
    for (int i = 9; i >= 0; i--) {
        double value;
        res = zipc_stack_pop(stack, &value);
        double expected = i * 3.14159;
        TEST_ASSERT(res == ZIPC_OK, "Pop from stack");
        TEST_ASSERT(value > expected - 0.1 && value < expected + 0.1,
                   "Stack LIFO order maintained");
    }

    TEST_ASSERT(zipc_stack_empty(stack), "Stack empty after pops");

    zipc_view_close(stack);
    zipc_close(shm);
    zipc_destroy(TEST_SHM);
}

/* ============================================================================
 * Test 5: Cross-Process Access
 * ============================================================================ */

void test_cross_process_access(void) {
    printf("\n=== Test 5: Cross-Process Access ===\n");

    zipc_shm_t shm = zipc_open(TEST_SHM, TEST_SIZE, 64);
    assert(shm != NULL);

    /* Create structures in parent */
    zipc_view_t array = zipc_create(shm, "shared_array", ZIPC_TYPE_ARRAY,
                                    sizeof(int), 10);
    assert(array != NULL);

    for (int i = 0; i < 10; i++) {
        zipc_array_set(array, i, &i);
    }

    zipc_view_close(array);

    /* Fork child process */
    pid_t pid = fork();

    if (pid == 0) {
        /* Child process */
        zipc_shm_t child_shm = zipc_open(TEST_SHM, 0, 0);
        if (!child_shm) exit(1);

        zipc_view_t child_array = zipc_get(child_shm, "shared_array",
                                           ZIPC_TYPE_ARRAY, sizeof(int));
        if (!child_array) exit(1);

        /* Modify data in child */
        for (int i = 0; i < 10; i++) {
            int value;
            zipc_array_get(child_array, i, &value);
            value *= 2;
            zipc_array_set(child_array, i, &value);
        }

        zipc_view_close(child_array);
        zipc_close(child_shm);
        exit(0);
    }

    /* Parent waits for child */
    int status;
    waitpid(pid, &status, 0);
    TEST_ASSERT(WEXITSTATUS(status) == 0, "Child process succeeded");

    /* Verify modifications in parent */
    array = zipc_get(shm, "shared_array", ZIPC_TYPE_ARRAY, sizeof(int));
    assert(array != NULL);

    int value;
    zipc_array_get(array, 5, &value);
    TEST_ASSERT(value == 10, "Cross-process data modification works");

    zipc_view_close(array);
    zipc_close(shm);
    zipc_destroy(TEST_SHM);
}

/* ============================================================================
 * Test 6: Table Iteration
 * ============================================================================ */

bool count_callback(const char* name, size_t offset, size_t size, void* ctx) {
    int* count = (int*)ctx;
    (*count)++;
    return true;  // Continue iteration
}

void test_table_iteration(void) {
    printf("\n=== Test 6: Table Iteration ===\n");

    zipc_shm_t shm = zipc_open(TEST_SHM, TEST_SIZE, 64);
    assert(shm != NULL);

    /* Create multiple structures */
    const char* names[] = {"array1", "queue1", "stack1", "array2", "queue2"};
    for (int i = 0; i < 5; i++) {
        zipc_type type = (i % 3 == 0) ? ZIPC_TYPE_ARRAY :
                        (i % 3 == 1) ? ZIPC_TYPE_QUEUE : ZIPC_TYPE_STACK;

        zipc_view_t view = zipc_create(shm, names[i], type, sizeof(int), 10);
        TEST_ASSERT(view != NULL, "Create structure for iteration");
        zipc_view_close(view);
    }

    /* Count structures */
    TEST_ASSERT(zipc_count(shm) == 5, "Structure count correct");

    /* Iterate with callback */
    int callback_count = 0;
    zipc_iterate(shm, count_callback, &callback_count);
    TEST_ASSERT(callback_count == 5, "Iterator visited all structures");

    zipc_close(shm);
    zipc_destroy(TEST_SHM);
}

/* ============================================================================
 * Test 7: Error Handling
 * ============================================================================ */

void test_error_handling(void) {
    printf("\n=== Test 7: Error Handling ===\n");

    zipc_shm_t shm = zipc_open(TEST_SHM, TEST_SIZE, 64);
    assert(shm != NULL);

    /* Test invalid parameters */
    zipc_view_t view = zipc_create(shm, NULL, ZIPC_TYPE_ARRAY, 4, 10);
    TEST_ASSERT(view == NULL, "Reject NULL name");

    view = zipc_create(shm, "test", ZIPC_TYPE_ARRAY, 0, 10);
    TEST_ASSERT(view == NULL, "Reject zero element size");

    view = zipc_create(shm, "test", ZIPC_TYPE_ARRAY, 4, 0);
    TEST_ASSERT(view == NULL, "Reject zero capacity");

    /* Test name too long */
    char long_name[64];
    memset(long_name, 'a', sizeof(long_name));
    long_name[63] = '\0';
    view = zipc_create(shm, long_name, ZIPC_TYPE_ARRAY, 4, 10);
    TEST_ASSERT(view == NULL, "Reject too-long name");

    /* Test duplicate names */
    view = zipc_create(shm, "unique", ZIPC_TYPE_ARRAY, 4, 10);
    TEST_ASSERT(view != NULL, "Create with unique name");
    zipc_view_close(view);

    view = zipc_create(shm, "unique", ZIPC_TYPE_ARRAY, 4, 10);
    TEST_ASSERT(view == NULL, "Reject duplicate name");

    /* Test non-existent structure */
    view = zipc_get(shm, "nonexistent", ZIPC_TYPE_ARRAY, 4);
    TEST_ASSERT(view == NULL, "Return NULL for non-existent structure");

    /* Test type mismatch */
    view = zipc_create(shm, "typed", ZIPC_TYPE_QUEUE, sizeof(int), 10);
    assert(view != NULL);
    zipc_view_close(view);

    view = zipc_get(shm, "typed", ZIPC_TYPE_QUEUE, sizeof(double));
    TEST_ASSERT(view == NULL, "Detect element size mismatch");

    /* Test error strings */
    const char* msg = zipc_strerror(ZIPC_OK);
    TEST_ASSERT(strcmp(msg, "Success") == 0, "Error string for OK");

    msg = zipc_strerror(ZIPC_FULL);
    TEST_ASSERT(strcmp(msg, "Full") == 0, "Error string for FULL");

    zipc_close(shm);
    zipc_destroy(TEST_SHM);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("   ZeroIPC Core API Test Suite\n");
    printf("========================================\n");

    /* Ensure clean state */
    zipc_destroy(TEST_SHM);

    /* Run all tests */
    test_memory_management();
    test_array_operations();
    test_queue_operations();
    test_stack_operations();
    test_cross_process_access();
    test_table_iteration();
    test_error_handling();

    /* Summary */
    printf("\n");
    printf("========================================\n");
    if (tests_passed == tests_run) {
        printf(GREEN "✓ All tests passed! " RESET);
    } else {
        printf(RED "✗ Some tests failed. " RESET);
    }
    printf("(%d/%d)\n", tests_passed, tests_run);
    printf("========================================\n");
    printf("\n");

    /* Clean up */
    zipc_destroy(TEST_SHM);

    return (tests_passed == tests_run) ? 0 : 1;
}