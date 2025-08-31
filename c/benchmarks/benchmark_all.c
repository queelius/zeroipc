#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include "../include/zeroipc.h"

#define BILLION 1000000000L

// ========== TIMING UTILITIES ==========

typedef struct {
    struct timespec start;
    struct timespec end;
} bench_timer_t;

void timer_start(bench_timer_t* timer) {
    clock_gettime(CLOCK_MONOTONIC, &timer->start);
}

void timer_stop(bench_timer_t* timer) {
    clock_gettime(CLOCK_MONOTONIC, &timer->end);
}

double timer_elapsed_ns(bench_timer_t* timer) {
    return (timer->end.tv_sec - timer->start.tv_sec) * BILLION +
           (timer->end.tv_nsec - timer->start.tv_nsec);
}

double timer_elapsed_ms(bench_timer_t* timer) {
    return timer_elapsed_ns(timer) / 1000000.0;
}

double timer_elapsed_s(bench_timer_t* timer) {
    return timer_elapsed_ns(timer) / BILLION;
}

const char* format_throughput(double ops_per_sec, char* buffer) {
    if (ops_per_sec > 1e9) {
        sprintf(buffer, "%.2f Gops/s", ops_per_sec / 1e9);
    } else if (ops_per_sec > 1e6) {
        sprintf(buffer, "%.2f Mops/s", ops_per_sec / 1e6);
    } else if (ops_per_sec > 1e3) {
        sprintf(buffer, "%.2f Kops/s", ops_per_sec / 1e3);
    } else {
        sprintf(buffer, "%.2f ops/s", ops_per_sec);
    }
    return buffer;
}

// ========== QUEUE BENCHMARKS ==========

void benchmark_queue_single_thread() {
    printf("\n=== Queue Single Thread Throughput ===\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/bench_queue", 100*1024*1024, 128);
    
    // Test different data sizes
    struct {
        size_t size;
        const char* name;
        int iterations;
    } test_cases[] = {
        {sizeof(int), "int (4B)", 1000000},
        {64, "64B struct", 500000},
        {256, "256B struct", 200000},
        {1024, "1KB struct", 50000},
        {0, NULL, 0}
    };
    
    for (int t = 0; test_cases[t].name != NULL; t++) {
        size_t size = test_cases[t].size;
        const char* name = test_cases[t].name;
        int iterations = test_cases[t].iterations;
        
        char queue_name[64];
        sprintf(queue_name, "throughput_%zu", size);
        
        zeroipc_queue_t* queue = zeroipc_queue_create(mem, queue_name, size, 100000);
        
        void* value = calloc(1, size);
        bench_timer_t timer;
        
        // Push benchmark
        timer_start(&timer);
        for (int i = 0; i < iterations; i++) {
            while (zeroipc_queue_push(queue, value) != ZEROIPC_OK) {
                void* dummy = malloc(size);
                zeroipc_queue_pop(queue, dummy);  // Make room
                free(dummy);
            }
        }
        timer_stop(&timer);
        
        double push_throughput = iterations / timer_elapsed_s(&timer);
        
        // Pop benchmark
        timer_start(&timer);
        for (int i = 0; i < iterations; i++) {
            while (zeroipc_queue_pop(queue, value) != ZEROIPC_OK) {
                zeroipc_queue_push(queue, value);  // Add items
            }
        }
        timer_stop(&timer);
        
        double pop_throughput = iterations / timer_elapsed_s(&timer);
        
        char push_str[32], pop_str[32];
        printf("%-12s Push: %12s, Pop: %12s\n", 
               name,
               format_throughput(push_throughput, push_str),
               format_throughput(pop_throughput, pop_str));
        
        free(value);
        zeroipc_queue_close(queue);
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/bench_queue");
}

void benchmark_queue_latency() {
    printf("\n=== Queue Operation Latency (nanoseconds) ===\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/bench_queue", 10*1024*1024, 64);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "latency", sizeof(int), 10000);
    
    const int warmup = 1000;
    const int iterations = 10000;
    
    // Warmup
    int value = 42;
    for (int i = 0; i < warmup; i++) {
        zeroipc_queue_push(queue, &value);
        zeroipc_queue_pop(queue, &value);
    }
    
    // Measure push latency
    double* push_latencies = malloc(iterations * sizeof(double));
    bench_timer_t timer;
    
    for (int i = 0; i < iterations; i++) {
        timer_start(&timer);
        zeroipc_queue_push(queue, &value);
        timer_stop(&timer);
        push_latencies[i] = timer_elapsed_ns(&timer);
    }
    
    // Calculate statistics
    double push_sum = 0;
    for (int i = 0; i < iterations; i++) {
        push_sum += push_latencies[i];
    }
    double push_avg = push_sum / iterations;
    
    // Simple percentile calculation (would need sorting for accurate percentiles)
    printf("Push: avg=%.0f ns\n", push_avg);
    
    // Measure pop latency
    double* pop_latencies = malloc(iterations * sizeof(double));
    
    for (int i = 0; i < iterations; i++) {
        timer_start(&timer);
        zeroipc_queue_pop(queue, &value);
        timer_stop(&timer);
        pop_latencies[i] = timer_elapsed_ns(&timer);
    }
    
    double pop_sum = 0;
    for (int i = 0; i < iterations; i++) {
        pop_sum += pop_latencies[i];
    }
    double pop_avg = pop_sum / iterations;
    
    printf("Pop:  avg=%.0f ns\n", pop_avg);
    
    free(push_latencies);
    free(pop_latencies);
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/bench_queue");
}

// ========== STACK BENCHMARKS ==========

void benchmark_stack_single_thread() {
    printf("\n=== Stack Single Thread Throughput ===\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/bench_stack", 100*1024*1024, 128);
    
    struct {
        size_t size;
        const char* name;
        int iterations;
    } test_cases[] = {
        {sizeof(int), "int (4B)", 1000000},
        {sizeof(double), "double (8B)", 1000000},
        {64, "64B struct", 500000},
        {0, NULL, 0}
    };
    
    for (int t = 0; test_cases[t].name != NULL; t++) {
        size_t size = test_cases[t].size;
        const char* name = test_cases[t].name;
        int iterations = test_cases[t].iterations;
        
        char stack_name[64];
        sprintf(stack_name, "throughput_%zu", size);
        
        zeroipc_stack_t* stack = zeroipc_stack_create(mem, stack_name, size, 100000);
        
        void* value = calloc(1, size);
        bench_timer_t timer;
        
        // Push benchmark
        timer_start(&timer);
        for (int i = 0; i < iterations; i++) {
            while (zeroipc_stack_push(stack, value) != ZEROIPC_OK) {
                void* dummy = malloc(size);
                zeroipc_stack_pop(stack, dummy);  // Make room
                free(dummy);
            }
        }
        timer_stop(&timer);
        
        double push_throughput = iterations / timer_elapsed_s(&timer);
        
        // Pop benchmark
        timer_start(&timer);
        for (int i = 0; i < iterations; i++) {
            while (zeroipc_stack_pop(stack, value) != ZEROIPC_OK) {
                zeroipc_stack_push(stack, value);  // Add items
            }
        }
        timer_stop(&timer);
        
        double pop_throughput = iterations / timer_elapsed_s(&timer);
        
        char push_str[32], pop_str[32];
        printf("%-12s Push: %12s, Pop: %12s\n", 
               name,
               format_throughput(push_throughput, push_str),
               format_throughput(pop_throughput, pop_str));
        
        free(value);
        zeroipc_stack_close(stack);
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/bench_stack");
}

void benchmark_stack_lifo_pattern() {
    printf("\n=== Stack LIFO Pattern Performance ===\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/bench_stack", 50*1024*1024, 64);
    zeroipc_stack_t* stack = zeroipc_stack_create(mem, "lifo", sizeof(int), 1000000);
    
    int batch_sizes[] = {1, 10, 100, 1000, 10000, 0};
    
    for (int b = 0; batch_sizes[b] != 0; b++) {
        int batch_size = batch_sizes[b];
        int total_ops = 1000000;
        int batches = total_ops / batch_size;
        
        bench_timer_t timer;
        timer_start(&timer);
        
        for (int i = 0; i < batches; i++) {
            // Push batch
            for (int j = 0; j < batch_size; j++) {
                int value = i * batch_size + j;
                zeroipc_stack_push(stack, &value);
            }
            
            // Pop batch (LIFO order)
            for (int j = 0; j < batch_size; j++) {
                int value;
                zeroipc_stack_pop(stack, &value);
            }
        }
        
        timer_stop(&timer);
        
        double throughput = (total_ops * 2) / timer_elapsed_s(&timer);
        
        char throughput_str[32];
        printf("Batch size: %5d - Throughput: %12s\n",
               batch_size,
               format_throughput(throughput, throughput_str));
    }
    
    zeroipc_stack_close(stack);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/bench_stack");
}

// ========== ARRAY BENCHMARKS ==========

void benchmark_array_sequential() {
    printf("\n=== Array Sequential Access ===\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/bench_array", 100*1024*1024, 128);
    
    size_t sizes[] = {1000, 10000, 100000, 1000000, 0};
    
    for (int s = 0; sizes[s] != 0; s++) {
        size_t size = sizes[s];
        
        char array_name[64];
        sprintf(array_name, "seq_%zu", size);
        
        zeroipc_array_t* array = zeroipc_array_create(mem, array_name, sizeof(int), size);
        
        bench_timer_t timer;
        
        // Write benchmark
        timer_start(&timer);
        for (size_t i = 0; i < size; i++) {
            int value = i;
            zeroipc_array_set(array, i, &value);
        }
        timer_stop(&timer);
        
        double write_throughput = size / timer_elapsed_s(&timer);
        
        // Read benchmark
        timer_start(&timer);
        int sum = 0;
        for (size_t i = 0; i < size; i++) {
            int* value = (int*)zeroipc_array_get(array, i);
            sum += *value;
        }
        timer_stop(&timer);
        
        double read_throughput = size / timer_elapsed_s(&timer);
        
        char write_str[32], read_str[32];
        printf("Size: %7zu - Write: %12s, Read: %12s\n",
               size,
               format_throughput(write_throughput, write_str),
               format_throughput(read_throughput, read_str));
        
        zeroipc_array_close(array);
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/bench_array");
}

void benchmark_array_random() {
    printf("\n=== Array Random Access ===\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/bench_array", 100*1024*1024, 64);
    zeroipc_array_t* array = zeroipc_array_create(mem, "random", sizeof(int), 1000000);
    
    // Initialize array
    for (size_t i = 0; i < 1000000; i++) {
        int value = i;
        zeroipc_array_set(array, i, &value);
    }
    
    // Generate random indices
    size_t num_indices = 100000;
    size_t* indices = malloc(num_indices * sizeof(size_t));
    srand(42);
    for (size_t i = 0; i < num_indices; i++) {
        indices[i] = rand() % 1000000;
    }
    
    bench_timer_t timer;
    
    // Random read benchmark
    timer_start(&timer);
    int sum = 0;
    for (size_t i = 0; i < num_indices; i++) {
        int* value = (int*)zeroipc_array_get(array, indices[i]);
        sum += *value;
    }
    timer_stop(&timer);
    
    double read_throughput = num_indices / timer_elapsed_s(&timer);
    
    // Random write benchmark
    timer_start(&timer);
    for (size_t i = 0; i < num_indices; i++) {
        int value = indices[i] * 2;
        zeroipc_array_set(array, indices[i], &value);
    }
    timer_stop(&timer);
    
    double write_throughput = num_indices / timer_elapsed_s(&timer);
    
    char read_str[32], write_str[32];
    printf("Random access (100k ops on 1M array):\n");
    printf("  Read:  %12s\n", format_throughput(read_throughput, read_str));
    printf("  Write: %12s\n", format_throughput(write_throughput, write_str));
    
    free(indices);
    zeroipc_array_close(array);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/bench_array");
}

void benchmark_array_data_types() {
    printf("\n=== Array Different Data Types ===\n");
    
    zeroipc_memory_t* mem = zeroipc_memory_create("/bench_array", 100*1024*1024, 128);
    
    struct {
        size_t size;
        const char* name;
        size_t iterations;
    } types[] = {
        {1, "uint8 (1B)", 1000000},
        {4, "int32 (4B)", 1000000},
        {8, "double (8B)", 1000000},
        {16, "Vec4 (16B)", 1000000},
        {64, "CacheLine (64B)", 100000},
        {256, "Block256", 25000},
        {0, NULL, 0}
    };
    
    for (int t = 0; types[t].name != NULL; t++) {
        size_t size = types[t].size;
        const char* name = types[t].name;
        size_t iterations = types[t].iterations;
        
        char array_name[64];
        sprintf(array_name, "type_%zu", size);
        
        zeroipc_array_t* array = zeroipc_array_create(mem, array_name, size, iterations);
        
        void* value = calloc(1, size);
        bench_timer_t timer;
        
        // Write benchmark
        timer_start(&timer);
        for (size_t i = 0; i < iterations; i++) {
            zeroipc_array_set(array, i, value);
        }
        timer_stop(&timer);
        
        double write_throughput = iterations / timer_elapsed_s(&timer);
        double write_bandwidth = (iterations * size) / timer_elapsed_s(&timer) / (1024*1024);
        
        // Read benchmark
        timer_start(&timer);
        for (size_t i = 0; i < iterations; i++) {
            void* ptr = zeroipc_array_get(array, i);
            memcpy(value, ptr, size);  // Prevent optimization
        }
        timer_stop(&timer);
        
        double read_throughput = iterations / timer_elapsed_s(&timer);
        double read_bandwidth = (iterations * size) / timer_elapsed_s(&timer) / (1024*1024);
        
        char read_str[32], write_str[32];
        printf("%-15s R=%s (%.1f MB/s), W=%s (%.1f MB/s)\n",
               name,
               format_throughput(read_throughput, read_str), read_bandwidth,
               format_throughput(write_throughput, write_str), write_bandwidth);
        
        free(value);
        zeroipc_array_close(array);
    }
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/bench_array");
}

// ========== MAIN ==========

int main() {
    printf("=== ZeroIPC C Performance Benchmarks ===\n");
    printf("CPU Count: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    
    // Queue benchmarks
    benchmark_queue_single_thread();
    benchmark_queue_latency();
    
    // Stack benchmarks
    benchmark_stack_single_thread();
    benchmark_stack_lifo_pattern();
    
    // Array benchmarks
    benchmark_array_sequential();
    benchmark_array_random();
    benchmark_array_data_types();
    
    printf("\n✓ All benchmarks completed\n");
    
    return 0;
}