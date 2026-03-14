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

        auto run = [](const char* name, int iters) {
            return [=](auto dummy) {
                using T = decltype(dummy);
                Memory::unlink("/bench_s_st");
                Memory mem("/bench_s_st", 512*1024*1024);
                Stack<T> stack(mem, "s", 100000);

                T value{};

                auto start = high_resolution_clock::now();
                for (int i = 0; i < iters; i++) {
                    while (!stack.push(value)) stack.pop();
                }
                auto end = high_resolution_clock::now();
                auto push_us = duration_cast<microseconds>(end - start).count();
                double push_tp = (iters * 1000000.0) / std::max(push_us, (long)1);

                start = high_resolution_clock::now();
                for (int i = 0; i < iters; i++) {
                    while (!stack.pop()) stack.push(value);
                }
                end = high_resolution_clock::now();
                auto pop_us = duration_cast<microseconds>(end - start).count();
                double pop_tp = (iters * 1000000.0) / std::max(pop_us, (long)1);

                std::cout << std::setw(10) << name << ": "
                         << "Push: " << std::fixed << std::setprecision(0)
                         << push_tp << " ops/sec, "
                         << "Pop: " << pop_tp << " ops/sec" << std::endl;

                Memory::unlink("/bench_s_st");
            };
        };

        struct D64 { char d[64]; };
        struct D256 { char d[256]; };
        struct D1K { char d[1024]; };

        run("int (4B)", 1000000)(int{});
        run("64B", 1000000)(D64{});
        run("256B", 500000)(D256{});
        run("1KB", 100000)(D1K{});
    }

    static void benchmark_latency() {
        std::cout << "\n=== Stack Operation Latency ===" << std::endl;

        Memory::unlink("/bench_s_lat");
        Memory mem("/bench_s_lat", 10*1024*1024);
        Stack<int> stack(mem, "lat", 100000);

        // Warmup
        for (int i = 0; i < 1000; i++) { stack.push(i); }
        for (int i = 0; i < 1000; i++) { stack.pop(); }

        const int N = 10000;
        std::vector<double> push_lat, pop_lat, top_lat;

        for (int i = 0; i < N; i++) {
            auto s = high_resolution_clock::now();
            stack.push(i);
            auto e = high_resolution_clock::now();
            push_lat.push_back(duration_cast<nanoseconds>(e - s).count());
        }

        // Push some back for top() measurement
        for (int i = 0; i < N; i++) {
            auto s = high_resolution_clock::now();
            stack.pop();
            auto e = high_resolution_clock::now();
            pop_lat.push_back(duration_cast<nanoseconds>(e - s).count());
        }

        // Refill for top measurement
        for (int i = 0; i < 1000; i++) stack.push(i);

        for (int i = 0; i < N; i++) {
            auto s = high_resolution_clock::now();
            auto v = stack.top();
            auto e = high_resolution_clock::now();
            top_lat.push_back(duration_cast<nanoseconds>(e - s).count());
        }

        print_latency_stats("Push", push_lat);
        print_latency_stats("Pop", pop_lat);
        print_latency_stats("Top", top_lat);

        Memory::unlink("/bench_s_lat");
    }

    static void benchmark_concurrent() {
        std::cout << "\n=== Stack Concurrent Push/Pop ===" << std::endl;

        std::vector<int> thread_counts = {2, 4, 8, 12};

        for (int num_threads : thread_counts) {
            Memory::unlink("/bench_s_conc");
            Memory mem("/bench_s_conc", 100*1024*1024);
            Stack<int> stack(mem, "s", 100000);

            const int ops_per_thread = 200000;
            std::atomic<int> successful{0};

            auto start = high_resolution_clock::now();

            std::vector<std::thread> threads;
            for (int i = 0; i < num_threads; i++) {
                threads.emplace_back([&, i]() {
                    for (int j = 0; j < ops_per_thread; j++) {
                        if ((i + j) % 2 == 0) {
                            if (stack.push(j))
                                successful.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            if (stack.pop().has_value())
                                successful.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                });
            }

            for (auto& t : threads) t.join();

            auto end = high_resolution_clock::now();
            auto dur_us = duration_cast<microseconds>(end - start).count();

            int succ = successful.load();
            double throughput = (succ * 1000000.0) / std::max(dur_us, (long)1);

            std::cout << "Threads: " << std::setw(2) << num_threads
                     << " - " << std::fixed << std::setprecision(1)
                     << throughput / 1e6 << "M ops/sec"
                     << " (" << succ << " successful ops)" << std::endl;

            Memory::unlink("/bench_s_conc");
        }
    }

    static void benchmark_lifo_pattern() {
        std::cout << "\n=== Stack LIFO Pattern Performance ===" << std::endl;

        Memory::unlink("/bench_s_lifo");
        Memory mem("/bench_s_lifo", 50*1024*1024);
        Stack<int> stack(mem, "lifo", 1000000);

        int batch_sizes[] = {1, 10, 100, 1000, 10000};

        for (int bs : batch_sizes) {
            int total_ops = 1000000;
            int batches = total_ops / bs;

            auto start = high_resolution_clock::now();
            for (int b = 0; b < batches; b++) {
                for (int i = 0; i < bs; i++) stack.push(b * bs + i);
                for (int i = 0; i < bs; i++) stack.pop();
            }
            auto end = high_resolution_clock::now();
            auto dur_us = duration_cast<microseconds>(end - start).count();
            double throughput = (total_ops * 2.0 * 1000000.0) / std::max(dur_us, (long)1);

            std::cout << "Batch: " << std::setw(5) << bs
                     << " - " << std::fixed << std::setprecision(1)
                     << throughput / 1e6 << "M ops/sec" << std::endl;
        }

        Memory::unlink("/bench_s_lifo");
    }

private:
    static void print_latency_stats(const std::string& op, std::vector<double>& lat) {
        std::sort(lat.begin(), lat.end());
        double sum = 0;
        for (double l : lat) sum += l;
        size_t n = lat.size();

        std::cout << op << " latency (ns): "
                 << "avg=" << std::fixed << std::setprecision(0) << sum / n
                 << ", p50=" << lat[n * 50 / 100]
                 << ", p90=" << lat[n * 90 / 100]
                 << ", p99=" << lat[n * 99 / 100]
                 << ", p99.9=" << lat[std::min(n * 999 / 1000, n - 1)] << std::endl;
    }
};

int main() {
    std::cout << "=== ZeroIPC Stack Performance Benchmarks ===" << std::endl;
    std::cout << "CPU Count: " << std::thread::hardware_concurrency() << std::endl;

    StackBenchmark::benchmark_single_thread_throughput();
    StackBenchmark::benchmark_latency();
    StackBenchmark::benchmark_concurrent();
    StackBenchmark::benchmark_lifo_pattern();

    return 0;
}
