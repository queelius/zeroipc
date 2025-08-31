#!/usr/bin/env python3
"""Basic example of using ZeroIPC data structures."""

import numpy as np
from zeroipc import Memory, Array, Queue, Stack


def main():
    print("=== ZeroIPC Python Example ===\n")
    
    # Create shared memory
    print("Creating shared memory '/example_data' (10MB)...")
    mem = Memory("/example_data", size=10*1024*1024)
    
    # Example 1: Array
    print("\n1. Array Example:")
    print("   Creating float array 'temperatures' with 1000 elements...")
    temps = Array(mem, "temperatures", capacity=1000, dtype=np.float32)
    
    # Write some temperature data
    temps[0:5] = [20.5, 21.0, 22.3, 23.1, 22.8]
    print(f"   Written: {temps[0:5]}")
    
    # Example 2: Queue (FIFO)
    print("\n2. Queue Example (FIFO):")
    print("   Creating queue 'events' with capacity 100...")
    events = Queue(mem, "events", capacity=100, dtype=np.int32)
    
    # Push events
    for i in range(5):
        events.push(100 + i)
        print(f"   Pushed: {100 + i}")
    
    # Pop events (FIFO order)
    print("   Popping from queue:")
    for _ in range(3):
        val = events.pop()
        print(f"   Popped: {val}")
    
    print(f"   Queue size: {events.size()}")
    
    # Example 3: Stack (LIFO)
    print("\n3. Stack Example (LIFO):")
    print("   Creating stack 'commands' with capacity 50...")
    commands = Stack(mem, "commands", capacity=50, dtype='U20')  # Unicode strings
    
    # Push commands
    commands.push("START")
    commands.push("PROCESS")
    commands.push("STOP")
    print("   Pushed: START, PROCESS, STOP")
    
    # Pop commands (LIFO order)
    print("   Popping from stack:")
    while not commands.empty():
        cmd = commands.pop()
        print(f"   Popped: {cmd}")
    
    # Example 4: Structured data
    print("\n4. Structured Data Example:")
    point_dtype = np.dtype([
        ('x', 'f4'),
        ('y', 'f4'),
        ('z', 'f4'),
        ('id', 'i4')
    ])
    
    points = Array(mem, "points", capacity=100, dtype=point_dtype)
    
    # Create some points
    points[0] = (1.0, 2.0, 3.0, 1001)
    points[1] = (4.0, 5.0, 6.0, 1002)
    
    print(f"   Point 0: x={points[0]['x']}, y={points[0]['y']}, "
          f"z={points[0]['z']}, id={points[0]['id']}")
    print(f"   Point 1: x={points[1]['x']}, y={points[1]['y']}, "
          f"z={points[1]['z']}, id={points[1]['id']}")
    
    # Show table contents
    print(f"\n5. Table Contents:")
    print(f"   Total entries: {len(mem.table.entries)}")
    for entry in mem.table.entries:
        if entry.name:
            print(f"   - '{entry.name}' at offset {entry.offset}, "
                  f"size {entry.size} bytes")
    
    print("\nShared memory '/example_data' is ready for other processes.")
    print("You can run this script in another terminal to see data sharing.")
    print("\nPress Enter to clean up...")
    input()
    
    # Clean up
    Memory.unlink("/example_data")
    print("Cleaned up.")


if __name__ == "__main__":
    main()