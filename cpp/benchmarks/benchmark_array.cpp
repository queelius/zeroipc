#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <random>
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

using namespace zeroipc;
using namespace std::chrono;

class ArrayBenchmark {
public:
    static void benchmark_sequential_access() {
        std::cout << "\n=== Array Sequential Access ===" << std::endl;
        
        Memory mem("/bench_array", 100*1024*1024);
        
        const size_t sizes[] = {1000, 10000, 100000, 1000000};
        
        for (size_t size : sizes) {
            Array<int> array(mem, "seq_" + std::to_string(size), size);
            
            // Write benchmark
            auto start = high_resolution_clock::now();
            for (size_t i = 0; i < size; i++) {
                array[i] = i;
            }
            auto end = high_resolution_clock::now();
            
            auto write_duration = duration_cast<microseconds>(end - start).count();
            double write_throughput = (size * 1000000.0) / write_duration;
            
            // Read benchmark
            start = high_resolution_clock::now();
            int sum = 0;
            for (size_t i = 0; i < size; i++) {
                sum += array[i];
            }
            end = high_resolution_clock::now();
            
            auto read_duration = duration_cast<microseconds>(end - start).count();
            double read_throughput = (size * 1000000.0) / read_duration;
            
            std::cout << "Size: " << std::setw(7) << size << " - "
                     << "Write: " << std::fixed << std::setprecision(0) 
                     << write_throughput << " ops/sec, "
                     << "Read: " << read_throughput << " ops/sec"
                     << " (sum=" << sum << ")" << std::endl;
        }
    }
    
    static void benchmark_random_access() {
        std::cout << "\n=== Array Random Access ===" << std::endl;
        
        Memory mem("/bench_array", 100*1024*1024);
        Array<int> array(mem, "random", 1000000);
        
        // Initialize array
        for (size_t i = 0; i < 1000000; i++) {
            array[i] = i;
        }
        
        // Generate random indices
        std::vector<size_t> indices(100000);
        std::mt19937 rng(42);
        std::uniform_int_distribution<size_t> dist(0, 999999);
        for (auto& idx : indices) {
            idx = dist(rng);
        }
        
        // Random read benchmark
        auto start = high_resolution_clock::now();
        int sum = 0;
        for (size_t idx : indices) {
            sum += array[idx];
        }
        auto end = high_resolution_clock::now();
        
        auto read_duration = duration_cast<microseconds>(end - start).count();
        double read_throughput = (indices.size() * 1000000.0) / read_duration;
        
        // Random write benchmark
        start = high_resolution_clock::now();
        for (size_t idx : indices) {
            array[idx] = idx * 2;
        }
        end = high_resolution_clock::now();
        
        auto write_duration = duration_cast<microseconds>(end - start).count();
        double write_throughput = (indices.size() * 1000000.0) / write_duration;
        
        std::cout << "Random access (100k ops on 1M array):" << std::endl;
        std::cout << "  Read:  " << std::fixed << std::setprecision(0) 
                 << read_throughput << " ops/sec" << std::endl;
        std::cout << "  Write: " << write_throughput << " ops/sec" << std::endl;
    }
    
    static void benchmark_access_patterns() {
        std::cout << "\n=== Array Access Patterns ===" << std::endl;
        
        Memory mem("/bench_array", 100*1024*1024);
        Array<int> array(mem, "patterns", 1000000);
        
        const int iterations = 100000;
        
        // Stride-1 (sequential)
        benchmark_stride(array, 1, iterations, "Stride-1 (sequential)");
        
        // Stride-16 (cache line)
        benchmark_stride(array, 16, iterations, "Stride-16 (cache line)");
        
        // Stride-64 (page)
        benchmark_stride(array, 64, iterations, "Stride-64");
        
        // Stride-256
        benchmark_stride(array, 256, iterations, "Stride-256");
        
        // Stride-1024
        benchmark_stride(array, 1024, iterations / 10, "Stride-1024");
    }
    
    static void benchmark_concurrent_access() {
        std::cout << "\n=== Array Concurrent Access ===" << std::endl;
        
        Memory mem("/bench_array", 100*1024*1024);
        Array<std::atomic<int>> array(mem, "concurrent", 1000000);
        
        // Initialize
        for (size_t i = 0; i < 1000000; i++) {
            array[i].store(0);
        }
        
        std::vector<int> thread_counts = {1, 2, 4, 8, 16};
        
        for (int num_threads : thread_counts) {
            auto start = high_resolution_clock::now();
            
            std::vector<std::thread> threads;
            const size_t chunk_size = 1000000 / num_threads;
            
            for (int t = 0; t < num_threads; t++) {
                threads.emplace_back([&, t]() {
                    size_t start_idx = t * chunk_size;
                    size_t end_idx = (t == num_threads - 1) ? 1000000 : (t + 1) * chunk_size;
                    
                    for (size_t i = start_idx; i < end_idx; i++) {
                        array[i].fetch_add(1);
                    }
                });
            }
            
            for (auto& t : threads) t.join();
            
            auto end = high_resolution_clock::now();
            auto duration_ms = duration_cast<milliseconds>(end - start).count();
            
            double throughput = (1000000.0 * 1000) / duration_ms;
            
            std::cout << "Threads: " << std::setw(2) << num_threads
                     << " - Throughput: " << std::fixed << std::setprecision(0)
                     << throughput << " ops/sec" << std::endl;
        }
    }
    
    static void benchmark_data_types() {
        std::cout << "\n=== Array Different Data Types ===" << std::endl;
        
        Memory mem("/bench_array", 100*1024*1024);
        
        const int iterations = 1000000;
        
        // 1 byte
        benchmark_type<uint8_t>(mem, "uint8", iterations);
        
        // 4 bytes
        benchmark_type<int32_t>(mem, "int32", iterations);
        
        // 8 bytes
        benchmark_type<double>(mem, "double", iterations);
        
        // 16 bytes
        struct Vec4 { float x, y, z, w; };
        benchmark_type<Vec4>(mem, "Vec4 (16B)", iterations);
        
        // 64 bytes
        struct CacheLine { char data[64]; };
        benchmark_type<CacheLine>(mem, "CacheLine (64B)", iterations / 10);
        
        // 256 bytes
        struct Block256 { char data[256]; };
        benchmark_type<Block256>(mem, "Block256", iterations / 40);
    }

private:
    static void benchmark_stride(Array<int>& array, size_t stride, 
                                 int iterations, const std::string& name) {
        auto start = high_resolution_clock::now();
        
        int sum = 0;
        for (int i = 0; i < iterations; i++) {
            size_t idx = (i * stride) % array.capacity();
            sum += array[idx];
        }
        
        auto end = high_resolution_clock::now();
        auto duration_us = duration_cast<microseconds>(end - start).count();
        double throughput = (iterations * 1000000.0) / duration_us;
        
        std::cout << std::setw(25) << name << ": "
                 << std::fixed << std::setprecision(0)
                 << throughput << " ops/sec" << std::endl;
    }
    
    template<typename T>
    static void benchmark_type(Memory& mem, const std::string& type_name, size_t size) {
        Array<T> array(mem, "type_" + type_name, size);
        
        T value{};
        
        // Write benchmark
        auto start = high_resolution_clock::now();
        for (size_t i = 0; i < size; i++) {
            array[i] = value;
        }
        auto end = high_resolution_clock::now();
        
        auto write_duration = duration_cast<microseconds>(end - start).count();
        double write_throughput = (size * 1000000.0) / write_duration;
        double write_bandwidth = (size * sizeof(T)) / (write_duration / 1000000.0) / (1024*1024);
        
        // Read benchmark
        start = high_resolution_clock::now();
        T sum{};
        for (size_t i = 0; i < size; i++) {
            sum = array[i];  // Avoid optimization
        }
        end = high_resolution_clock::now();
        
        auto read_duration = duration_cast<microseconds>(end - start).count();
        double read_throughput = (size * 1000000.0) / read_duration;
        double read_bandwidth = (size * sizeof(T)) / (read_duration / 1000000.0) / (1024*1024);
        
        std::cout << std::setw(15) << type_name << ": "
                 << "R=" << std::fixed << std::setprecision(0) 
                 << read_throughput << " ops/s (" 
                 << std::setprecision(1) << read_bandwidth << " MB/s), "
                 << "W=" << std::setprecision(0) 
                 << write_throughput << " ops/s ("
                 << std::setprecision(1) << write_bandwidth << " MB/s)"
                 << std::endl;
    }
};

int main() {
    std::cout << "=== ZeroIPC Array Performance Benchmarks ===" << std::endl;
    std::cout << "CPU Count: " << std::thread::hardware_concurrency() << std::endl;
    
    Memory::unlink("/bench_array");
    
    ArrayBenchmark::benchmark_sequential_access();
    ArrayBenchmark::benchmark_random_access();
    ArrayBenchmark::benchmark_access_patterns();
    ArrayBenchmark::benchmark_concurrent_access();
    ArrayBenchmark::benchmark_data_types();
    
    Memory::unlink("/bench_array");
    
    return 0;
}