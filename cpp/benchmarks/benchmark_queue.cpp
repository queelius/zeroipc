#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>

using namespace zeroipc;
using namespace std::chrono;

class QueueBenchmark {
public:
    static void benchmark_single_thread_throughput() {
        std::cout << "\n=== Queue Single Thread Throughput ===" << std::endl;
        
        Memory mem("/bench_queue", 100*1024*1024);
        
        // Test different data sizes
        std::vector<size_t> sizes = {4, 64, 256, 1024, 4096};
        
        for (size_t size : sizes) {
            // Use char array of specified size
            if (size == 4) {
                run_throughput_test<int>(mem, "int (4 bytes)", 1000000);
            } else if (size == 64) {
                struct Data64 { char data[64]; };
                run_throughput_test<Data64>(mem, "64 bytes", 1000000);
            } else if (size == 256) {
                struct Data256 { char data[256]; };
                run_throughput_test<Data256>(mem, "256 bytes", 500000);
            } else if (size == 1024) {
                struct Data1K { char data[1024]; };
                run_throughput_test<Data1K>(mem, "1KB", 100000);
            } else if (size == 4096) {
                struct Data4K { char data[4096]; };
                run_throughput_test<Data4K>(mem, "4KB", 25000);
            }
        }
    }
    
    static void benchmark_latency() {
        std::cout << "\n=== Queue Operation Latency ===" << std::endl;
        
        Memory mem("/bench_queue", 10*1024*1024);
        Queue<int> queue(mem, "latency", 10000);
        
        const int warmup = 1000;
        const int iterations = 10000;
        
        // Warmup
        for (int i = 0; i < warmup; i++) {
            queue.push(i);
            queue.pop();
        }
        
        // Measure push latency
        std::vector<double> push_latencies;
        for (int i = 0; i < iterations; i++) {
            auto start = high_resolution_clock::now();
            queue.push(i);
            auto end = high_resolution_clock::now();
            
            auto latency = duration_cast<nanoseconds>(end - start).count();
            push_latencies.push_back(latency);
        }
        
        // Measure pop latency
        std::vector<double> pop_latencies;
        for (int i = 0; i < iterations; i++) {
            auto start = high_resolution_clock::now();
            queue.pop();
            auto end = high_resolution_clock::now();
            
            auto latency = duration_cast<nanoseconds>(end - start).count();
            pop_latencies.push_back(latency);
        }
        
        print_latency_stats("Push", push_latencies);
        print_latency_stats("Pop", pop_latencies);
    }
    
    static void benchmark_concurrent_throughput() {
        std::cout << "\n=== Queue Concurrent Throughput ===" << std::endl;
        
        Memory mem("/bench_queue", 100*1024*1024);
        
        std::vector<int> thread_counts = {1, 2, 4, 8, 16};
        
        for (int num_threads : thread_counts) {
            Queue<int> queue(mem, "concurrent", 100000);
            
            const int items_per_thread = 100000;
            std::atomic<int> total_ops{0};
            
            auto start = high_resolution_clock::now();
            
            std::vector<std::thread> threads;
            
            // Half producers, half consumers
            int producers = num_threads / 2;
            int consumers = num_threads - producers;
            
            if (producers == 0) producers = 1;
            if (consumers == 0) consumers = 1;
            
            std::atomic<bool> done{false};
            
            // Producers
            for (int i = 0; i < producers; i++) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < items_per_thread; j++) {
                        while (!queue.push(j)) {
                            std::this_thread::yield();
                        }
                        total_ops++;
                    }
                });
            }
            
            // Consumers
            for (int i = 0; i < consumers; i++) {
                threads.emplace_back([&]() {
                    int consumed = 0;
                    while (consumed < items_per_thread) {
                        if (queue.pop().has_value()) {
                            consumed++;
                            total_ops++;
                        } else if (done) {
                            break;
                        } else {
                            std::this_thread::yield();
                        }
                    }
                });
            }
            
            // Wait for producers
            for (int i = 0; i < producers; i++) {
                threads[i].join();
            }
            done = true;
            
            // Wait for consumers
            for (int i = producers; i < num_threads; i++) {
                threads[i].join();
            }
            
            auto end = high_resolution_clock::now();
            auto duration_ms = duration_cast<milliseconds>(end - start).count();
            
            double throughput = (total_ops.load() * 1000.0) / duration_ms;
            
            std::cout << "Threads: " << std::setw(2) << num_threads 
                     << " (P:" << producers << " C:" << consumers << ")"
                     << " - Throughput: " << std::fixed << std::setprecision(0) 
                     << throughput << " ops/sec" << std::endl;
        }
    }
    
    static void benchmark_contention() {
        std::cout << "\n=== Queue Contention Scaling ===" << std::endl;
        
        Memory mem("/bench_queue", 100*1024*1024);
        Queue<int> queue(mem, "contention", 10000);
        
        std::vector<int> thread_counts = {1, 2, 4, 8, 16, 32};
        
        for (int num_threads : thread_counts) {
            const int ops_per_thread = 100000;
            std::atomic<int> successful_ops{0};
            std::atomic<int> failed_ops{0};
            
            auto start = high_resolution_clock::now();
            
            std::vector<std::thread> threads;
            for (int i = 0; i < num_threads; i++) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < ops_per_thread; j++) {
                        if (j % 2 == 0) {
                            if (queue.push(j)) {
                                successful_ops++;
                            } else {
                                failed_ops++;
                            }
                        } else {
                            if (queue.pop().has_value()) {
                                successful_ops++;
                            } else {
                                failed_ops++;
                            }
                        }
                    }
                });
            }
            
            for (auto& t : threads) t.join();
            
            auto end = high_resolution_clock::now();
            auto duration_ms = duration_cast<milliseconds>(end - start).count();
            
            double throughput = (successful_ops.load() * 1000.0) / duration_ms;
            double success_rate = (successful_ops.load() * 100.0) / 
                                 (successful_ops.load() + failed_ops.load());
            
            std::cout << "Threads: " << std::setw(2) << num_threads 
                     << " - Throughput: " << std::fixed << std::setprecision(0) 
                     << throughput << " ops/sec"
                     << " - Success rate: " << std::setprecision(1) 
                     << success_rate << "%" << std::endl;
        }
    }

private:
    template<typename T>
    static void run_throughput_test(Memory& mem, const std::string& type_name, int iterations) {
        Queue<T> queue(mem, "throughput_" + type_name, 100000);
        
        T value{};
        
        // Push test
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            while (!queue.push(value)) {
                queue.pop();  // Make room
            }
        }
        auto end = high_resolution_clock::now();
        
        auto push_duration = duration_cast<microseconds>(end - start).count();
        double push_throughput = (iterations * 1000000.0) / push_duration;
        
        // Pop test
        start = high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            while (!queue.pop()) {
                queue.push(value);  // Add items
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
        
        double p50 = latencies[latencies.size() * 0.50];
        double p90 = latencies[latencies.size() * 0.90];
        double p99 = latencies[latencies.size() * 0.99];
        double p999 = latencies[latencies.size() * 0.999];
        
        std::cout << op << " latency (ns): "
                 << "avg=" << std::fixed << std::setprecision(0) << avg
                 << ", p50=" << p50
                 << ", p90=" << p90
                 << ", p99=" << p99
                 << ", p99.9=" << p999 << std::endl;
    }
};

int main() {
    std::cout << "=== ZeroIPC Queue Performance Benchmarks ===" << std::endl;
    std::cout << "CPU Count: " << std::thread::hardware_concurrency() << std::endl;
    
    Memory::unlink("/bench_queue");
    
    QueueBenchmark::benchmark_single_thread_throughput();
    QueueBenchmark::benchmark_latency();
    QueueBenchmark::benchmark_concurrent_throughput();
    QueueBenchmark::benchmark_contention();
    
    Memory::unlink("/bench_queue");
    
    return 0;
}