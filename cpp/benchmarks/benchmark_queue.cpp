#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>

using namespace zeroipc;
using namespace std::chrono;

class QueueBenchmark {
public:
    static void benchmark_single_thread_throughput() {
        std::cout << "\n=== Queue Single Thread Throughput ===" << std::endl;

        auto run = [](auto type_name, auto iterations) {
            return [=](auto dummy) {
                using T = decltype(dummy);
                Memory::unlink("/bench_q_st");
                Memory mem("/bench_q_st", 512*1024*1024);
                Queue<T> queue(mem, "q", 100000);

                T value{};

                // Push test
                auto start = high_resolution_clock::now();
                for (int i = 0; i < iterations; i++) {
                    while (!queue.push(value)) {
                        queue.pop();
                    }
                }
                auto end = high_resolution_clock::now();
                auto push_us = duration_cast<microseconds>(end - start).count();
                double push_tp = (iterations * 1000000.0) / std::max(push_us, (long)1);

                // Pop test
                start = high_resolution_clock::now();
                for (int i = 0; i < iterations; i++) {
                    while (!queue.pop()) {
                        queue.push(value);
                    }
                }
                end = high_resolution_clock::now();
                auto pop_us = duration_cast<microseconds>(end - start).count();
                double pop_tp = (iterations * 1000000.0) / std::max(pop_us, (long)1);

                std::cout << std::setw(10) << type_name << ": "
                         << "Push: " << std::fixed << std::setprecision(0)
                         << push_tp << " ops/sec, "
                         << "Pop: " << pop_tp << " ops/sec" << std::endl;

                Memory::unlink("/bench_q_st");
            };
        };

        struct D64 { char d[64]; };
        struct D256 { char d[256]; };
        struct D1K { char d[1024]; };
        struct D4K { char d[4096]; };

        run("int (4B)", 1000000)(int{});
        run("64B", 1000000)(D64{});
        run("256B", 500000)(D256{});
        run("1KB", 100000)(D1K{});
        run("4KB", 25000)(D4K{});
    }

    static void benchmark_latency() {
        std::cout << "\n=== Queue Operation Latency ===" << std::endl;

        Memory::unlink("/bench_q_lat");
        Memory mem("/bench_q_lat", 10*1024*1024);
        Queue<int> queue(mem, "lat", 100000);

        const int warmup = 1000;
        const int iterations = 10000;

        for (int i = 0; i < warmup; i++) {
            queue.push(i);
            queue.pop();
        }

        std::vector<double> push_lat, pop_lat;

        for (int i = 0; i < iterations; i++) {
            auto s = high_resolution_clock::now();
            queue.push(i);
            auto e = high_resolution_clock::now();
            push_lat.push_back(duration_cast<nanoseconds>(e - s).count());
        }

        for (int i = 0; i < iterations; i++) {
            auto s = high_resolution_clock::now();
            queue.pop();
            auto e = high_resolution_clock::now();
            pop_lat.push_back(duration_cast<nanoseconds>(e - s).count());
        }

        print_latency_stats("Push", push_lat);
        print_latency_stats("Pop", pop_lat);

        Memory::unlink("/bench_q_lat");
    }

    static void benchmark_concurrent_throughput() {
        std::cout << "\n=== Queue Concurrent Throughput ===" << std::endl;

        std::vector<int> thread_counts = {2, 4, 8, 12};

        for (int num_threads : thread_counts) {
            Memory::unlink("/bench_q_conc");
            Memory mem("/bench_q_conc", 100*1024*1024);
            Queue<int> queue(mem, "q", 100000);

            int producers = num_threads / 2;
            int consumers = num_threads - producers;
            const int items_per_producer = 500000;
            int total_items = items_per_producer * producers;

            std::atomic<int> produced{0};
            std::atomic<int> consumed{0};

            auto start = high_resolution_clock::now();

            std::vector<std::thread> threads;

            for (int i = 0; i < producers; i++) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < items_per_producer; j++) {
                        while (!queue.push(j)) {
                            std::this_thread::yield();
                        }
                        produced.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }

            for (int i = 0; i < consumers; i++) {
                threads.emplace_back([&]() {
                    while (consumed.load(std::memory_order_relaxed) < total_items) {
                        if (queue.pop().has_value()) {
                            consumed.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            std::this_thread::yield();
                        }
                    }
                });
            }

            for (auto& t : threads) t.join();

            auto end = high_resolution_clock::now();
            auto dur_us = duration_cast<microseconds>(end - start).count();
            // Count both push + pop as ops
            double throughput = (consumed.load() * 2.0 * 1000000.0) / std::max(dur_us, (long)1);

            std::cout << "Threads: " << std::setw(2) << num_threads
                     << " (P:" << producers << " C:" << consumers << ")"
                     << " - " << std::fixed << std::setprecision(1)
                     << throughput / 1e6 << "M ops/sec"
                     << " (" << consumed.load() << " items)" << std::endl;

            Memory::unlink("/bench_q_conc");
        }
    }

    static void benchmark_contention() {
        std::cout << "\n=== Queue Contention Scaling ===" << std::endl;

        std::vector<int> thread_counts = {1, 2, 4, 8, 12};

        for (int num_threads : thread_counts) {
            Memory::unlink("/bench_q_cont");
            Memory mem("/bench_q_cont", 100*1024*1024);
            Queue<int> queue(mem, "q", 10000);

            const int ops_per_thread = 200000;
            std::atomic<int> successful_ops{0};
            std::atomic<int> failed_ops{0};

            auto start = high_resolution_clock::now();

            std::vector<std::thread> threads;
            for (int i = 0; i < num_threads; i++) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < ops_per_thread; j++) {
                        if (j % 2 == 0) {
                            if (queue.push(j))
                                successful_ops.fetch_add(1, std::memory_order_relaxed);
                            else
                                failed_ops.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            if (queue.pop().has_value())
                                successful_ops.fetch_add(1, std::memory_order_relaxed);
                            else
                                failed_ops.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                });
            }

            for (auto& t : threads) t.join();

            auto end = high_resolution_clock::now();
            auto dur_us = duration_cast<microseconds>(end - start).count();

            int succ = successful_ops.load();
            int fail = failed_ops.load();
            double throughput = (succ * 1000000.0) / std::max(dur_us, (long)1);
            double success_rate = (succ * 100.0) / std::max(succ + fail, 1);

            std::cout << "Threads: " << std::setw(2) << num_threads
                     << " - " << std::fixed << std::setprecision(1)
                     << throughput / 1e6 << "M ops/sec"
                     << " - Success: " << std::setprecision(1) << success_rate << "%" << std::endl;

            Memory::unlink("/bench_q_cont");
        }
    }

private:
    static void print_latency_stats(const std::string& op, std::vector<double>& latencies) {
        std::sort(latencies.begin(), latencies.end());

        double sum = 0;
        for (double l : latencies) sum += l;
        double avg = sum / latencies.size();

        size_t n = latencies.size();
        std::cout << op << " latency (ns): "
                 << "avg=" << std::fixed << std::setprecision(0) << avg
                 << ", p50=" << latencies[n * 50 / 100]
                 << ", p90=" << latencies[n * 90 / 100]
                 << ", p99=" << latencies[n * 99 / 100]
                 << ", p99.9=" << latencies[std::min(n * 999 / 1000, n - 1)] << std::endl;
    }
};

int main() {
    std::cout << "=== ZeroIPC Queue Performance Benchmarks ===" << std::endl;
    std::cout << "CPU Count: " << std::thread::hardware_concurrency() << std::endl;

    QueueBenchmark::benchmark_single_thread_throughput();
    QueueBenchmark::benchmark_latency();
    QueueBenchmark::benchmark_concurrent_throughput();
    QueueBenchmark::benchmark_contention();

    return 0;
}
