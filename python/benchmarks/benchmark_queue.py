#!/usr/bin/env python3
"""Benchmark Queue performance."""

import time
import numpy as np
from zeroipc import Memory, Queue


def benchmark_queue_throughput():
    """Benchmark queue push/pop throughput."""
    print("=== Queue Throughput Benchmark ===\n")
    
    # Create large shared memory
    mem = Memory("/bench_queue", size=100*1024*1024)  # 100MB
    
    # Test different queue sizes
    for capacity in [100, 1000, 10000]:
        queue = Queue(mem, f"queue_{capacity}", capacity=capacity, dtype=np.int32)
        
        # Benchmark push operations
        num_ops = min(capacity - 1, 10000)  # Leave room in circular buffer
        
        start = time.perf_counter()
        for i in range(num_ops):
            queue.push(i)
        push_time = time.perf_counter() - start
        
        push_rate = num_ops / push_time
        print(f"Queue capacity {capacity}:")
        print(f"  Push: {push_rate:,.0f} ops/sec ({push_time*1000:.2f}ms for {num_ops} ops)")
        
        # Benchmark pop operations
        start = time.perf_counter()
        for _ in range(num_ops):
            queue.pop()
        pop_time = time.perf_counter() - start
        
        pop_rate = num_ops / pop_time
        print(f"  Pop:  {pop_rate:,.0f} ops/sec ({pop_time*1000:.2f}ms for {num_ops} ops)")
        print()
    
    # Clean up
    Memory.unlink("/bench_queue")


def benchmark_queue_types():
    """Benchmark different data types."""
    print("=== Queue Type Benchmark ===\n")
    
    mem = Memory("/bench_types", size=100*1024*1024)
    num_ops = 1000
    
    # Test different types
    types = [
        (np.int32, "int32"),
        (np.float64, "float64"),
        (np.dtype([('x', 'f4'), ('y', 'f4')]), "point2d"),
    ]
    
    for dtype, name in types:
        queue = Queue(mem, f"queue_{name}", capacity=2000, dtype=dtype)
        
        # Create test data
        if name == "point2d":
            data = np.array([(1.0, 2.0)], dtype=dtype)[0]
        else:
            data = dtype.type(42)
        
        # Benchmark
        start = time.perf_counter()
        for _ in range(num_ops):
            queue.push(data)
        push_time = time.perf_counter() - start
        
        start = time.perf_counter()
        for _ in range(num_ops):
            queue.pop()
        pop_time = time.perf_counter() - start
        
        total_time = push_time + pop_time
        ops_per_sec = (num_ops * 2) / total_time
        
        print(f"{name:10s}: {ops_per_sec:>10,.0f} ops/sec")
    
    # Clean up
    Memory.unlink("/bench_types")


if __name__ == "__main__":
    benchmark_queue_throughput()
    print()
    benchmark_queue_types()