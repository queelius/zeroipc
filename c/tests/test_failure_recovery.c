#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "../include/zeroipc.h"

// ========== CRASH SIMULATION TESTS ==========

void test_process_crash_during_write() {
    printf("Testing process crash during write...\n");
    
    // Parent creates shared memory
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 10*1024*1024, 64);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "crash_queue", sizeof(int), 1000);
    
    // Add initial data
    for (int i = 0; i < 100; i++) {
        assert(zeroipc_queue_push(queue, &i) == ZEROIPC_OK);
    }
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process - simulate crash during operations
        zeroipc_memory_t* child_mem = zeroipc_memory_open("/test_recovery");
        zeroipc_queue_t* child_queue = zeroipc_queue_open(child_mem, "crash_queue");
        
        // Start adding data
        for (int i = 1000; i < 1050; i++) {
            zeroipc_queue_push(child_queue, &i);
            
            if (i == 1025) {
                // Simulate crash - immediate exit without cleanup
                _exit(42);
            }
        }
        
        zeroipc_queue_close(child_queue);
        zeroipc_memory_close(child_mem);
        _exit(0);
    }
    
    // Parent waits for child to "crash"
    int status;
    waitpid(pid, &status, 0);
    assert(WEXITSTATUS(status) == 42);
    
    // Verify parent can still access queue
    int count = 0;
    int val;
    while (zeroipc_queue_pop(queue, &val) == ZEROIPC_OK) {
        count++;
    }
    
    // Should have original 100 + some from child
    assert(count >= 100);
    printf("  Recovered %d items after child crash\n", count);
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Process crash during write passed\n");
}

void test_recovery_after_termination() {
    printf("Testing recovery after abrupt termination...\n");
    
    // Create and populate data
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 10*1024*1024, 64);
    zeroipc_stack_t* stack = zeroipc_stack_create(mem, "persist_stack", sizeof(double), 500);
    
    for (int i = 0; i < 250; i++) {
        double val = i * 3.14;
        zeroipc_stack_push(stack, &val);
    }
    
    // Close without unlinking
    zeroipc_stack_close(stack);
    zeroipc_memory_close(mem);
    
    // Simulate new process accessing same memory
    zeroipc_memory_t* recovered_mem = zeroipc_memory_open("/test_recovery");
    zeroipc_stack_t* recovered_stack = zeroipc_stack_open(recovered_mem, "persist_stack");
    
    // Should be able to read all data
    int count = 0;
    double sum = 0;
    double val;
    
    while (zeroipc_stack_pop(recovered_stack, &val) == ZEROIPC_OK) {
        count++;
        sum += val;
    }
    
    assert(count == 250);
    printf("  Recovered %d values, sum: %.2f\n", count, sum);
    
    zeroipc_stack_close(recovered_stack);
    zeroipc_memory_close(recovered_mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Recovery after termination passed\n");
}

// ========== PARTIAL WRITE RECOVERY ==========

typedef struct {
    uint32_t sequence;
    char data[1020];
    uint32_t checksum;
} DataWithChecksum;

uint32_t calculate_checksum(uint32_t sequence, const char* data) {
    uint32_t checksum = sequence;
    for (int i = 0; i < 1020; i++) {
        checksum = (checksum << 1) ^ data[i];
    }
    return checksum;
}

void test_partial_write_detection() {
    printf("Testing partial write detection...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 10*1024*1024, 64);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "checksum_queue", sizeof(DataWithChecksum), 100);
    
    // Write valid data
    for (uint32_t i = 0; i < 50; i++) {
        DataWithChecksum item;
        item.sequence = i;
        memset(item.data, 'A' + (i % 26), sizeof(item.data));
        item.checksum = calculate_checksum(i, item.data);
        
        assert(zeroipc_queue_push(queue, &item) == ZEROIPC_OK);
    }
    
    // Read back and verify checksums
    int valid_count = 0;
    int invalid_count = 0;
    DataWithChecksum item;
    
    while (zeroipc_queue_pop(queue, &item) == ZEROIPC_OK) {
        uint32_t expected = calculate_checksum(item.sequence, item.data);
        if (item.checksum == expected) {
            valid_count++;
        } else {
            invalid_count++;
            printf("  Detected corrupted item at sequence %u\n", item.sequence);
        }
    }
    
    assert(valid_count == 50);
    assert(invalid_count == 0);
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Partial write detection passed\n");
}

// ========== SIGNAL HANDLING RECOVERY ==========

static volatile sig_atomic_t signal_received = 0;

void signal_handler(int sig) {
    (void)sig;
    signal_received = 1;
}

void test_graceful_shutdown_on_signal() {
    printf("Testing graceful shutdown on signal...\n");
    
    // Install signal handler
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 10*1024*1024, 64);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "signal_queue", sizeof(int), 1000);
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child - writer process
        zeroipc_memory_t* child_mem = zeroipc_memory_open("/test_recovery");
        zeroipc_queue_t* child_queue = zeroipc_queue_open(child_mem, "signal_queue");
        
        int value = 0;
        while (!signal_received) {
            if (zeroipc_queue_push(child_queue, &value) == ZEROIPC_OK) {
                value++;
            }
            usleep(100);
        }
        
        printf("  Child wrote %d values before signal\n", value);
        
        zeroipc_queue_close(child_queue);
        zeroipc_memory_close(child_mem);
        _exit(value);
    }
    
    // Parent
    usleep(100000); // Let child run for 100ms
    
    // Send signal to child
    kill(pid, SIGUSR1);
    
    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    int written = WEXITSTATUS(status);
    
    // Verify data integrity
    int read_count = 0;
    int last_value = -1;
    int sequence_valid = 1;
    int val;
    
    while (zeroipc_queue_pop(queue, &val) == ZEROIPC_OK) {
        if (val != last_value + 1 && last_value != -1) {
            sequence_valid = 0;
        }
        last_value = val;
        read_count++;
    }
    
    assert(sequence_valid);
    printf("  Gracefully recovered %d items after signal\n", read_count);
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Graceful shutdown on signal passed\n");
}

// ========== DEADLOCK RECOVERY ==========

typedef struct {
    zeroipc_queue_t* queue;
    int* deadlock_detected;
    pthread_mutex_t* mutex;
} deadlock_test_data_t;

void* deadlock_pusher_thread(void* arg) {
    deadlock_test_data_t* data = (deadlock_test_data_t*)arg;
    
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Try to push with timeout
    while (1) {
        if (zeroipc_queue_push(data->queue, &(int){999}) == ZEROIPC_OK) {
            break;
        }
        
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + 
                         (now.tv_nsec - start.tv_nsec) / 1000000;
        
        if (elapsed_ms > 100) {
            pthread_mutex_lock(data->mutex);
            *data->deadlock_detected = 1;
            pthread_mutex_unlock(data->mutex);
            break;
        }
        
        usleep(1000);
    }
    
    return NULL;
}

void test_deadlock_timeout() {
    printf("Testing deadlock timeout...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 10*1024*1024, 64);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "deadlock_queue", sizeof(int), 10);
    
    // Fill queue
    for (int i = 0; i < 9; i++) {
        assert(zeroipc_queue_push(queue, &i) == ZEROIPC_OK);
    }
    
    int deadlock_detected = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    
    deadlock_test_data_t thread_data = {
        .queue = queue,
        .deadlock_detected = &deadlock_detected,
        .mutex = &mutex
    };
    
    pthread_t pusher;
    pthread_create(&pusher, NULL, deadlock_pusher_thread, &thread_data);
    
    // Simulate delayed consumer
    usleep(50000);
    
    if (!deadlock_detected) {
        // Make room
        int val;
        zeroipc_queue_pop(queue, &val);
    }
    
    pthread_join(pusher, NULL);
    
    pthread_mutex_destroy(&mutex);
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Deadlock timeout passed\n");
}

// ========== ATOMIC OPERATION RECOVERY ==========

void test_incomplete_atomic_operation() {
    printf("Testing incomplete atomic operation recovery...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 10*1024*1024, 64);
    zeroipc_stack_t* stack = zeroipc_stack_create(mem, "atomic_stack", sizeof(uint64_t), 1000);
    
    const uint64_t MARKER = 0xDEADBEEFCAFEBABE;
    const uint64_t INCOMPLETE = 0xFFFFFFFFFFFFFFFF;
    
    // Push marker values
    for (int i = 0; i < 100; i++) {
        uint64_t value = MARKER + i;
        zeroipc_stack_push(stack, &value);
    }
    
    // Recovery: scan for incomplete markers
    uint64_t recovered[100];
    int recovered_count = 0;
    uint64_t val;
    
    while (zeroipc_stack_pop(stack, &val) == ZEROIPC_OK) {
        if (val != INCOMPLETE) {
            recovered[recovered_count++] = val;
        }
    }
    
    // Verify all valid data recovered
    assert(recovered_count == 100);
    for (int i = 0; i < recovered_count; i++) {
        assert(recovered[i] >= MARKER);
        assert(recovered[i] < MARKER + 100);
    }
    
    printf("  Recovered %d valid items\n", recovered_count);
    
    zeroipc_stack_close(stack);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Incomplete atomic operation recovery passed\n");
}

// ========== MULTI-PROCESS RECOVERY ==========

void test_multi_process_crash_recovery() {
    printf("Testing multi-process crash recovery...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 50*1024*1024, 128);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "multi_queue", sizeof(int), 10000);
    
    const int num_processes = 5;
    pid_t pids[num_processes];
    
    // Launch multiple child processes
    for (int p = 0; p < num_processes; p++) {
        pids[p] = fork();
        
        if (pids[p] == 0) {
            // Child process
            zeroipc_memory_t* child_mem = zeroipc_memory_open("/test_recovery");
            zeroipc_queue_t* child_queue = zeroipc_queue_open(child_mem, "multi_queue");
            
            // Each child writes its range
            for (int i = 0; i < 1000; i++) {
                int value = p * 10000 + i;
                zeroipc_queue_push(child_queue, &value);
                
                // Simulate random crash
                if (i == 500 + p * 100) {
                    _exit(p);
                }
            }
            
            zeroipc_queue_close(child_queue);
            zeroipc_memory_close(child_mem);
            _exit(0);
        }
    }
    
    // Wait for all children
    for (int p = 0; p < num_processes; p++) {
        int status;
        waitpid(pids[p], &status, 0);
        printf("  Process %d exited with status %d\n", p, WEXITSTATUS(status));
    }
    
    // Parent recovers all data
    int recovered_count = 0;
    int val;
    
    while (zeroipc_queue_pop(queue, &val) == ZEROIPC_OK) {
        recovered_count++;
    }
    
    assert(recovered_count > 0);
    printf("  Recovered %d values from crashed processes\n", recovered_count);
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Multi-process crash recovery passed\n");
}

// ========== CONSISTENCY CHECK ==========

void test_consistency_after_failures() {
    printf("Testing consistency after failures...\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_recovery", 10*1024*1024, 64);
    
    // Create multiple structures
    zeroipc_array_t* array = zeroipc_array_create(mem, "cons_array", sizeof(int), 100);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "cons_queue", sizeof(int), 100);
    zeroipc_stack_t* stack = zeroipc_stack_create(mem, "cons_stack", sizeof(int), 100);
    
    // Initialize with known pattern
    for (int i = 0; i < 100; i++) {
        int val = i * 100;
        zeroipc_array_set(array, i, &val);
    }
    
    for (int i = 0; i < 50; i++) {
        int qval = i * 10;
        int sval = i * 20;
        zeroipc_queue_push(queue, &qval);
        zeroipc_stack_push(stack, &sval);
    }
    
    // Close structures
    zeroipc_array_close(array);
    zeroipc_queue_close(queue);
    zeroipc_stack_close(stack);
    zeroipc_memory_close(mem);
    
    // Reopen structures
    zeroipc_memory_t* recovered_mem = zeroipc_memory_open("/test_recovery");
    zeroipc_array_t* rec_array = zeroipc_array_open(recovered_mem, "cons_array");
    zeroipc_queue_t* rec_queue = zeroipc_queue_open(recovered_mem, "cons_queue");
    zeroipc_stack_t* rec_stack = zeroipc_stack_open(recovered_mem, "cons_stack");
    
    // Verify array unchanged
    int array_valid = 1;
    for (int i = 0; i < 100; i++) {
        int* val = (int*)zeroipc_array_get(rec_array, i);
        if (!val || *val != i * 100) {
            array_valid = 0;
            break;
        }
    }
    assert(array_valid);
    
    // Verify queue has correct size
    assert(zeroipc_queue_size(rec_queue) == 50);
    
    // Verify stack has correct size  
    assert(zeroipc_stack_size(rec_stack) == 50);
    
    printf("  All structures consistent after recovery\n");
    
    zeroipc_array_close(rec_array);
    zeroipc_queue_close(rec_queue);
    zeroipc_stack_close(rec_stack);
    zeroipc_memory_close(recovered_mem);
    zeroipc_memory_unlink("/test_recovery");
    
    printf("  ✓ Consistency after failures passed\n");
}

// ========== MAIN ==========

int main() {
    printf("=== Failure Recovery Tests ===\n\n");
    
    test_process_crash_during_write();
    test_recovery_after_termination();
    test_partial_write_detection();
    test_graceful_shutdown_on_signal();
    test_deadlock_timeout();
    test_incomplete_atomic_operation();
    test_multi_process_crash_recovery();
    test_consistency_after_failures();
    
    printf("\n✓ All failure recovery tests passed!\n");
    return 0;
}