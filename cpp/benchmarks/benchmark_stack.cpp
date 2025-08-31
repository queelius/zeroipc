#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <zeroipc/memory.h>
#include <zeroipc/stack.h>

using namespace zeroipc;
using namespace std::chrono;

class StackBenchmark {
public:
    static void benchmark_single_thread_throughput() {
        std::cout << "\n=== Stack Single Thread Throughput ===" << std::endl;
        
        Memory mem("/bench_stack", 100*1024*1024);
        
        // Test different data sizes
        benchmark_size<int>(mem, "int (4 bytes)", 1000000);
        
        struct Data64 { char data[64]; };
        benchmark_size<Data64>(mem, "64 bytes", 1000000);
        
        struct Data256 { char data[256]; };
        benchmark_size<Data256>(mem, "256 bytes", 500000);
        
        struct Data1K { char data[1024]; };
        benchmark_size<Data1K>(mem, "1KB", 100000);
    }
    
    static void benchmark_latency() {
        std::cout << "\n=== Stack Operation Latency ===" << std::endl;
        
        Memory mem("/bench_stack", 10*1024*1024);
        Stack<int> stack(mem, "latency", 10000);
        
        const int warmup = 1000;
        const int iterations = 10000;
        
        // Warmup
        for (int i = 0; i < warmup; i++) {
            stack.push(i);
        }
        for (int i = 0; i < warmup; i++) {
            stack.pop();
        }
        
        // Measure push latency
        std::vector<double> push_latencies;
        for (int i = 0; i < iterations; i++) {
            auto start = high_resolution_clock::now();
            stack.push(i);
            auto end = high_resolution_clock::now();
            
            push_latencies.push_back(
                duration_cast<nanoseconds>(end - start).count()
            );
        }
        
        // Measure pop latency
        std::vector<double> pop_latencies;
        for (int i = 0; i < iterations; i++) {
            auto start = high_resolution_clock::now();
            stack.pop();
            auto end = high_resolution_clock::now();
            
            pop_latencies.push_back(
                duration_cast<nanoseconds>(end - start).count()
            );
        }
        
        // Measure top latency
        for (int i = 0; i < 1000; i++) {
            stack.push(i);
        }
        
        std::vector<double> top_latencies;
        for (int i = 0; i < iterations; i++) {
            auto start = high_resolution_clock::now();
            auto val = stack.top();
            auto end = high_resolution_clock::now();
            
            top_latencies.push_back(
                duration_cast<nanoseconds>(end - start).count()
            );
        }
        
        print_latency_stats("Push", push_latencies);
        print_latency_stats("Pop", pop_latencies);
        print_latency_stats("Top", top_latencies);
    }
    
    static void benchmark_concurrent_push_pop() {
        std::cout << "\n=== Stack Concurrent Push/Pop ===" << std::endl;
        
        Memory mem("/bench_stack", 100*1024*1024);
        
        std::vector<int> thread_counts = {1, 2, 4, 8, 16};
        
        for (int num_threads : thread_counts) {
            Stack<int> stack(mem, "concurrent", 100000);
            
            const int ops_per_thread = 100000;
            std::atomic<int> total_pushes{0};
            std::atomic<int> total_pops{0};
            
            auto start = high_resolution_clock::now();
            
            std::vector<std::thread> threads;
            for (int i = 0; i < num_threads; i++) {
                threads.emplace_back([&, i]() {
                    // Each thread does mixed push/pop
                    for (int j = 0; j < ops_per_thread; j++) {
                        if ((i + j) % 2 == 0) {
                            if (stack.push(j)) {
                                total_pushes++;
                            }
                        } else {
                            if (stack.pop().has_value()) {
                                total_pops++;
                            }
                        }
                    }
                });
            }
            
            for (auto& t : threads) t.join();
            
            auto end = high_resolution_clock::now();
            auto duration_ms = duration_cast<milliseconds>(end - start).count();
            
            int total_ops = total_pushes + total_pops;
            double throughput = (total_ops * 1000.0) / duration_ms;
            
            std::cout << "Threads: " << std::setw(2) << num_threads 
                     << " - Throughput: " << std::fixed << std::setprecision(0) 
                     << throughput << " ops/sec"
                     << " (Push: " << total_pushes 
                     << ", Pop: " << total_pops << ")" << std::endl;
        }
    }
    
    static void benchmark_lifo_pattern() {
        std::cout << "\n=== Stack LIFO Pattern Performance ===" << std::endl;
        
        Memory mem("/bench_stack", 50*1024*1024);
        Stack<int> stack(mem, "lifo", 1000000);
        
        const int batch_sizes[] = {1, 10, 100, 1000, 10000};
        
        for (int batch_size : batch_sizes) {
            const int total_ops = 1000000;
            const int batches = total_ops / batch_size;
            
            auto start = high_resolution_clock::now();
            
            for (int b = 0; b < batches; b++) {
                // Push batch
                for (int i = 0; i < batch_size; i++) {
                    stack.push(b * batch_size + i);
                }
                
                // Pop batch (LIFO order)
                for (int i = 0; i < batch_size; i++) {
                    stack.pop();
                }
            }
            
            auto end = high_resolution_clock::now();
            auto duration_us = duration_cast<microseconds>(end - start).count();
            
            double throughput = (total_ops * 2 * 1000000.0) / duration_us;
            
            std::cout << "Batch size: " << std::setw(5) << batch_size
                     << " - Throughput: " << std::fixed << std::setprecision(0)
                     << throughput << " ops/sec" << std::endl;
        }
    }

private:
    template<typename T>
    static void benchmark_size(Memory& mem, const std::string& type_name, int iterations) {
        Stack<T> stack(mem, "size_" + type_name, 100000);
        
        T value{};
        
        // Push benchmark
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            while (!stack.push(value)) {
                stack.pop();  // Make room if full
            }
        }
        auto end = high_resolution_clock::now();
        
        auto push_duration = duration_cast<microseconds>(end - start).count();
        double push_throughput = (iterations * 1000000.0) / push_duration;
        
        // Pop benchmark
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            while (!stack.pop()) {
                stack.push(value);  // Add items if empty
            }
        }
        end = high_resolution_clock::now();
        
        auto pop_duration = duration_cast<microseconds>(end - start).count();
        double pop_throughput = (iterations * 1000000.0) / pop_duration;
        
        std::cout << std::setw(10) << type_name << ": "
                 << "Push: " << std::fixed << std::setprecision(0) 
                 << push_throughput << " ops/sec, "
                 << "Pop: " << pop_throughput << " ops/sec" << std::endl;
    }
    
    static void print_latency_stats(const std::string& op, std::vector<double>& latencies) {
        std::sort(latencies.begin(), latencies.end());
        
        double sum = 0;
        for (double l : latencies) sum += l;
        double avg = sum / latencies.size();
        
        size_t n = latencies.size();
        double p50 = latencies[n * 0.50];
        double p90 = latencies[n * 0.90];
        double p99 = latencies[n * 0.99];
        double p999 = latencies[std::min(size_t(n * 0.999), n-1)];
        
        std::cout << op << " latency (ns): "
                 << "avg=" << std::fixed << std::setprecision(0) << avg
                 << ", p50=" << p50
                 << ", p90=" << p90
                 << ", p99=" << p99
                 << ", p99.9=" << p999 << std::endl;
    }
};

int main() {
    std::cout << "=== ZeroIPC Stack Performance Benchmarks ===" << std::endl;
    std::cout << "CPU Count: " << std::thread::hardware_concurrency() << std::endl;
    
    Memory::unlink("/bench_stack");
    
    StackBenchmark::benchmark_single_thread_throughput();
    StackBenchmark::benchmark_latency();
    StackBenchmark::benchmark_concurrent_push_pop();
    StackBenchmark::benchmark_lifo_pattern();
    
    Memory::unlink("/bench_stack");
    
    return 0;
}