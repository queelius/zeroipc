#!/usr/bin/env python3
"""Performance benchmarks for ZeroIPC Python implementation."""

import time
import numpy as np
import threading
from concurrent.futures import ThreadPoolExecutor
import statistics
import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from zeroipc import Memory, Queue, Stack, Array


class Benchmark:
    """Base benchmark class with utilities."""
    
    @staticmethod
    def format_throughput(ops_per_sec):
        """Format throughput with appropriate units."""
        if ops_per_sec > 1e9:
            return f"{ops_per_sec/1e9:.2f} Gops/s"
        elif ops_per_sec > 1e6:
            return f"{ops_per_sec/1e6:.2f} Mops/s"
        elif ops_per_sec > 1e3:
            return f"{ops_per_sec/1e3:.2f} Kops/s"
        else:
            return f"{ops_per_sec:.2f} ops/s"
    
    @staticmethod
    def measure_latency(func, iterations=10000, warmup=1000):
        """Measure operation latency."""
        # Warmup
        for _ in range(warmup):
            func()
        
        # Measure
        latencies = []
        for _ in range(iterations):
            start = time.perf_counter_ns()
            func()
            end = time.perf_counter_ns()
            latencies.append(end - start)
        
        latencies.sort()
        return {
            'avg': statistics.mean(latencies),
            'p50': latencies[int(len(latencies) * 0.50)],
            'p90': latencies[int(len(latencies) * 0.90)],
            'p99': latencies[int(len(latencies) * 0.99)],
            'p999': latencies[min(int(len(latencies) * 0.999), len(latencies)-1)]
        }


class QueueBenchmark(Benchmark):
    """Queue performance benchmarks."""
    
    def benchmark_single_thread_throughput(self):
        """Benchmark single-threaded queue throughput."""
        print("\n=== Queue Single Thread Throughput ===")
        
        Memory.unlink("/bench_queue")
        mem = Memory("/bench_queue", size=100*1024*1024)
        
        # Test different data sizes
        sizes = [
            (np.int32, "int32 (4B)", 1000000),
            (np.dtype([('data', 'u1', 64)]), "64B struct", 500000),
            (np.dtype([('data', 'u1', 256)]), "256B struct", 200000),
            (np.dtype([('data', 'u1', 1024)]), "1KB struct", 50000),
        ]
        
        for dtype, name, iterations in sizes:
            queue = Queue(mem, f"throughput_{name}", capacity=100000, dtype=dtype)
            
            if dtype == np.int32:
                value = 42
            else:
                value = np.zeros(1, dtype=dtype)[0]
            
            # Push benchmark
            start = time.perf_counter()
            for i in range(iterations):
                while not queue.push(value):
                    queue.pop()  # Make room
            end = time.perf_counter()
            
            push_throughput = iterations / (end - start)
            
            # Pop benchmark
            start = time.perf_counter()
            for i in range(iterations):
                while queue.pop() is None:
                    queue.push(value)  # Add items
            end = time.perf_counter()
            
            pop_throughput = iterations / (end - start)
            
            print(f"{name:12} Push: {self.format_throughput(push_throughput):>12}, "
                  f"Pop: {self.format_throughput(pop_throughput):>12}")
    
    def benchmark_latency(self):
        """Benchmark queue operation latency."""
        print("\n=== Queue Operation Latency (nanoseconds) ===")
        
        Memory.unlink("/bench_queue")
        mem = Memory("/bench_queue", size=10*1024*1024)
        queue = Queue(mem, "latency", capacity=10000, dtype=np.int32)
        
        # Pre-fill for pop tests
        for i in range(5000):
            queue.push(i)
        
        # Push latency
        push_stats = self.measure_latency(lambda: queue.push(42))
        print(f"Push: avg={push_stats['avg']:.0f}, p50={push_stats['p50']:.0f}, "
              f"p90={push_stats['p90']:.0f}, p99={push_stats['p99']:.0f}, "
              f"p99.9={push_stats['p999']:.0f}")
        
        # Pop latency
        pop_stats = self.measure_latency(lambda: queue.pop())
        print(f"Pop:  avg={pop_stats['avg']:.0f}, p50={pop_stats['p50']:.0f}, "
              f"p90={pop_stats['p90']:.0f}, p99={pop_stats['p99']:.0f}, "
              f"p99.9={pop_stats['p999']:.0f}")
    
    def benchmark_concurrent(self):
        """Benchmark concurrent queue operations."""
        print("\n=== Queue Concurrent Throughput ===")
        
        Memory.unlink("/bench_queue")
        mem = Memory("/bench_queue", size=100*1024*1024)
        
        thread_counts = [1, 2, 4, 8]
        
        for num_threads in thread_counts:
            queue = Queue(mem, f"concurrent_{num_threads}", 
                         capacity=100000, dtype=np.int32)
            
            items_per_thread = 50000
            total_ops = [0]
            
            def worker(thread_id):
                ops = 0
                for i in range(items_per_thread):
                    if thread_id % 2 == 0:
                        while not queue.push(i):
                            time.sleep(0.00001)
                        ops += 1
                    else:
                        while queue.pop() is None:
                            time.sleep(0.00001)
                        ops += 1
                return ops
            
            start = time.perf_counter()
            
            with ThreadPoolExecutor(max_workers=num_threads) as executor:
                futures = [executor.submit(worker, i) for i in range(num_threads)]
                total_ops = sum(f.result() for f in futures)
            
            end = time.perf_counter()
            
            throughput = total_ops / (end - start)
            
            producers = num_threads // 2 or 1
            consumers = num_threads - producers
            
            print(f"Threads: {num_threads:2} (P:{producers} C:{consumers}) - "
                  f"Throughput: {self.format_throughput(throughput):>12}")


class StackBenchmark(Benchmark):
    """Stack performance benchmarks."""
    
    def benchmark_single_thread_throughput(self):
        """Benchmark single-threaded stack throughput."""
        print("\n=== Stack Single Thread Throughput ===")
        
        Memory.unlink("/bench_stack")
        mem = Memory("/bench_stack", size=100*1024*1024)
        
        sizes = [
            (np.int32, "int32 (4B)", 1000000),
            (np.float64, "float64 (8B)", 1000000),
            (np.dtype([('data', 'u1', 64)]), "64B struct", 500000),
        ]
        
        for dtype, name, iterations in sizes:
            stack = Stack(mem, f"throughput_{name}", capacity=100000, dtype=dtype)
            
            if dtype in [np.int32, np.float64]:
                value = 42 if dtype == np.int32 else 3.14159
            else:
                value = np.zeros(1, dtype=dtype)[0]
            
            # Push benchmark
            start = time.perf_counter()
            for i in range(iterations):
                while not stack.push(value):
                    stack.pop()  # Make room
            end = time.perf_counter()
            
            push_throughput = iterations / (end - start)
            
            # Pop benchmark
            start = time.perf_counter()
            for i in range(iterations):
                while stack.pop() is None:
                    stack.push(value)  # Add items
            end = time.perf_counter()
            
            pop_throughput = iterations / (end - start)
            
            print(f"{name:12} Push: {self.format_throughput(push_throughput):>12}, "
                  f"Pop: {self.format_throughput(pop_throughput):>12}")
    
    def benchmark_lifo_pattern(self):
        """Benchmark LIFO access patterns."""
        print("\n=== Stack LIFO Pattern Performance ===")
        
        Memory.unlink("/bench_stack")
        mem = Memory("/bench_stack", size=50*1024*1024)
        stack = Stack(mem, "lifo", capacity=1000000, dtype=np.int32)
        
        batch_sizes = [1, 10, 100, 1000, 10000]
        
        for batch_size in batch_sizes:
            total_ops = 1000000
            batches = total_ops // batch_size
            
            start = time.perf_counter()
            
            for b in range(batches):
                # Push batch
                for i in range(batch_size):
                    stack.push(b * batch_size + i)
                
                # Pop batch (LIFO order)
                for i in range(batch_size):
                    stack.pop()
            
            end = time.perf_counter()
            
            throughput = (total_ops * 2) / (end - start)
            
            print(f"Batch size: {batch_size:5} - "
                  f"Throughput: {self.format_throughput(throughput):>12}")


class ArrayBenchmark(Benchmark):
    """Array performance benchmarks."""
    
    def benchmark_sequential_access(self):
        """Benchmark sequential array access."""
        print("\n=== Array Sequential Access ===")
        
        Memory.unlink("/bench_array")
        mem = Memory("/bench_array", size=100*1024*1024)
        
        sizes = [1000, 10000, 100000, 1000000]
        
        for size in sizes:
            array = Array(mem, f"seq_{size}", capacity=size, dtype=np.int32)
            
            # Write benchmark
            start = time.perf_counter()
            for i in range(size):
                array[i] = i
            end = time.perf_counter()
            
            write_throughput = size / (end - start)
            
            # Read benchmark
            start = time.perf_counter()
            total = 0
            for i in range(size):
                total += array[i]
            end = time.perf_counter()
            
            read_throughput = size / (end - start)
            
            print(f"Size: {size:7} - "
                  f"Write: {self.format_throughput(write_throughput):>12}, "
                  f"Read: {self.format_throughput(read_throughput):>12}")
    
    def benchmark_random_access(self):
        """Benchmark random array access."""
        print("\n=== Array Random Access ===")
        
        Memory.unlink("/bench_array")
        mem = Memory("/bench_array", size=100*1024*1024)
        array = Array(mem, "random", capacity=1000000, dtype=np.int32)
        
        # Initialize array
        for i in range(1000000):
            array[i] = i
        
        # Generate random indices
        np.random.seed(42)
        indices = np.random.randint(0, 1000000, size=100000)
        
        # Random read benchmark
        start = time.perf_counter()
        total = 0
        for idx in indices:
            total += array[idx]
        end = time.perf_counter()
        
        read_throughput = len(indices) / (end - start)
        
        # Random write benchmark
        start = time.perf_counter()
        for idx in indices:
            array[idx] = idx * 2
        end = time.perf_counter()
        
        write_throughput = len(indices) / (end - start)
        
        print("Random access (100k ops on 1M array):")
        print(f"  Read:  {self.format_throughput(read_throughput):>12}")
        print(f"  Write: {self.format_throughput(write_throughput):>12}")
    
    def benchmark_numpy_operations(self):
        """Benchmark numpy-style operations."""
        print("\n=== Array NumPy-style Operations ===")
        
        Memory.unlink("/bench_array")
        mem = Memory("/bench_array", size=100*1024*1024)
        
        size = 1000000
        array = Array(mem, "numpy_ops", capacity=size, dtype=np.float64)
        
        # Initialize with numpy array
        data = np.arange(size, dtype=np.float64)
        
        # Bulk write
        start = time.perf_counter()
        array[:] = data
        end = time.perf_counter()
        
        bulk_write_throughput = size / (end - start)
        bulk_write_bandwidth = (size * 8) / (end - start) / (1024*1024)  # MB/s
        
        # Bulk read
        start = time.perf_counter()
        result = array[:]
        end = time.perf_counter()
        
        bulk_read_throughput = size / (end - start)
        bulk_read_bandwidth = (size * 8) / (end - start) / (1024*1024)  # MB/s
        
        # Slice operations
        start = time.perf_counter()
        array[::2] = 42.0  # Every other element
        end = time.perf_counter()
        
        slice_write_throughput = (size // 2) / (end - start)
        
        print(f"Bulk write:  {self.format_throughput(bulk_write_throughput):>12} "
              f"({bulk_write_bandwidth:.1f} MB/s)")
        print(f"Bulk read:   {self.format_throughput(bulk_read_throughput):>12} "
              f"({bulk_read_bandwidth:.1f} MB/s)")
        print(f"Slice write: {self.format_throughput(slice_write_throughput):>12}")


def main():
    """Run all benchmarks."""
    print("=== ZeroIPC Python Performance Benchmarks ===")
    print(f"NumPy version: {np.__version__}")
    
    # Queue benchmarks
    queue_bench = QueueBenchmark()
    queue_bench.benchmark_single_thread_throughput()
    queue_bench.benchmark_latency()
    queue_bench.benchmark_concurrent()
    
    # Stack benchmarks
    stack_bench = StackBenchmark()
    stack_bench.benchmark_single_thread_throughput()
    stack_bench.benchmark_lifo_pattern()
    
    # Array benchmarks
    array_bench = ArrayBenchmark()
    array_bench.benchmark_sequential_access()
    array_bench.benchmark_random_access()
    array_bench.benchmark_numpy_operations()
    
    # Cleanup
    Memory.unlink("/bench_queue")
    Memory.unlink("/bench_stack")
    Memory.unlink("/bench_array")
    
    print("\n✓ All benchmarks completed")


if __name__ == "__main__":
    main()