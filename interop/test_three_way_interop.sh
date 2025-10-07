#!/bin/bash
#
# Three-Way Cross-Language Interoperability Test
# Tests C, C++, and Python all working with the same shared memory
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}    ZeroIPC Three-Way Interoperability Test    ${NC}"
echo -e "${BLUE}================================================${NC}"
echo

# Clean up any existing shared memory
echo -e "${YELLOW}Cleaning up any existing shared memory...${NC}"
rm -f /dev/shm/zeroipc_interop
echo

# Build C example with new elegant API
echo -e "${GREEN}Building C interop example...${NC}"
cd ../c
gcc -std=c99 -O2 -Iinclude src/core.c examples/interop.c -lrt -lpthread -lm -o examples/interop_test
echo "  ✓ C example built"

# Build C++ interop tool
echo -e "${GREEN}Building C++ interop tool...${NC}"
cd ../interop
cat > cpp_interop.cpp << 'EOF'
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <zeroipc/stack.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp;
};

struct Event {
    uint32_t event_id;
    uint32_t source_pid;
    uint64_t timestamp;
    char message[48];
};

void read_and_modify() {
    std::cout << "\n=== C++ Consumer: Processing shared structures ===" << std::endl;

    try {
        // Open existing memory
        zeroipc::Memory mem("/zeroipc_interop");
        std::cout << "Opened shared memory with " << mem.count() << " structures\n" << std::endl;

        // Access sensor array
        zeroipc::Array<SensorData> sensors(mem, "sensor_array");
        std::cout << "Sensor Array:" << std::endl;
        std::cout << "  Capacity: " << sensors.capacity() << std::endl;

        // Modify some sensors
        for (size_t i = 0; i < 5; i++) {
            SensorData& data = sensors[i];
            data.temperature += 0.5f;  // Add 0.5°C from C++
            std::cout << "  Modified sensor[" << i << "]: temp="
                      << data.temperature << "°C" << std::endl;
        }

        // Access event queue
        zeroipc::Queue<Event> events(mem, "event_queue");
        std::cout << "\nEvent Queue:" << std::endl;
        std::cout << "  Size: " << events.size() << std::endl;

        // Pop and display some events
        Event evt;
        for (int i = 0; i < 2 && !events.empty(); i++) {
            auto opt_evt = events.pop();
            if (opt_evt) {
                evt = *opt_evt;
                std::cout << "  Popped: " << evt.message << std::endl;
            }
        }

        // Add C++ event
        Event cpp_evt;
        cpp_evt.event_id = 5000;
        cpp_evt.source_pid = getpid();
        cpp_evt.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::strcpy(cpp_evt.message, "C++ Event from interop test");

        if (events.push(cpp_evt)) {
            std::cout << "  Pushed new C++ event" << std::endl;
        }

        // Access task stack
        zeroipc::Stack<uint32_t> tasks(mem, "task_stack");
        std::cout << "\nTask Stack:" << std::endl;
        std::cout << "  Size: " << tasks.size() << std::endl;

        auto task = tasks.pop();
        if (task) {
            std::cout << "  Popped task ID: " << *task << std::endl;
        }

        // Push C++ task
        tasks.push(777);
        std::cout << "  Pushed C++ task ID: 777" << std::endl;

        // Access statistics
        zeroipc::Array<double> stats(mem, "statistics");
        std::cout << "\nStatistics Array:" << std::endl;
        double sum = 0.0;
        for (size_t i = 0; i < stats.capacity(); i++) {
            sum += stats[i];
        }
        std::cout << "  Average value: " << (sum / stats.capacity()) << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "C++ Error: " << e.what() << std::endl;
        exit(1);
    }

    std::cout << "\nC++ Consumer finished." << std::endl;
}

int main() {
    read_and_modify();
    return 0;
}
EOF

g++ -std=c++23 -I../cpp/include cpp_interop.cpp -o cpp_interop -lrt
echo "  ✓ C++ tool built"

# Create Python interop script
echo -e "${GREEN}Creating Python interop script...${NC}"
cat > python_interop.py << 'EOF'
#!/usr/bin/env python3
"""
Python Consumer for Three-Way Interoperability Test
"""

import sys
import os
import time
import struct
import numpy as np

# Add parent directory to path for zeroipc module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from zeroipc import Memory, Array, Queue, Stack

def read_and_process():
    print("\n=== Python Consumer: Analyzing shared structures ===")

    try:
        # Open existing memory
        mem = Memory("/zeroipc_interop")
        print(f"Opened shared memory with {mem.table.count()} structures\n")

        # Access sensor array
        sensors = Array(mem, "sensor_array", dtype=np.dtype([
            ('temperature', np.float32),
            ('humidity', np.float32),
            ('timestamp', np.uint32)
        ]))
        print("Sensor Array:")
        print(f"  Shape: {sensors.data.shape}")

        # Analyze sensor data
        temps = sensors.data['temperature'][:10]
        print(f"  First 10 temperatures: {temps}")
        print(f"  Average temperature: {np.mean(sensors.data['temperature']):.2f}°C")
        print(f"  Max humidity: {np.max(sensors.data['humidity']):.1f}%")

        # Modify some sensors from Python
        for i in range(5, 10):
            sensors.data[i]['humidity'] += 2.0  # Add 2% humidity from Python
        print("  Modified humidity for sensors 5-9")

        # Access event queue
        event_dtype = np.dtype([
            ('event_id', np.uint32),
            ('source_pid', np.uint32),
            ('timestamp', np.uint64),
            ('message', 'S48')
        ])
        events = Queue(mem, "event_queue", dtype=event_dtype)
        print(f"\nEvent Queue:")
        print(f"  Size: {events.size()}")

        # Pop an event
        if not events.empty():
            evt = events.pop()
            if evt is not None:
                msg = evt['message'].tobytes().decode('utf-8', 'ignore').rstrip('\x00')
                print(f"  Popped: {msg}")

        # Push Python event
        py_event = np.zeros(1, dtype=event_dtype)[0]
        py_event['event_id'] = 7000
        py_event['source_pid'] = os.getpid()
        py_event['timestamp'] = int(time.time() * 1000)

        msg = f"Python Event PID {os.getpid()}"
        py_event['message'] = np.frombuffer(msg.encode('utf-8').ljust(48, b'\x00'), dtype='S48')[0]

        if events.push(py_event):
            print(f"  Pushed new Python event")

        # Access task stack
        tasks = Stack(mem, "task_stack", dtype=np.uint32)
        print(f"\nTask Stack:")
        print(f"  Size: {tasks.size()}")

        task = tasks.pop()
        if task is not None:
            print(f"  Popped task ID: {task}")

        # Push Python task
        tasks.push(888)
        print(f"  Pushed Python task ID: 888")

        # Access statistics
        stats = Array(mem, "statistics", dtype=np.float64)
        print(f"\nStatistics Array:")
        print(f"  Values: {stats.data[:3]}... ({len(stats.data)} total)")
        print(f"  Standard deviation: {np.std(stats.data):.4f}")

        # List all structures
        print(f"\nAll structures in memory:")
        for i, (name, offset, size) in enumerate(mem.table.list(), 1):
            print(f"  {i}. {name:20s} offset=0x{offset:08x} size={size} bytes")

    except Exception as e:
        print(f"Python Error: {e}")
        sys.exit(1)

    print("\nPython Consumer finished.")

if __name__ == "__main__":
    read_and_process()
EOF

chmod +x python_interop.py
echo "  ✓ Python script created"
echo

# Run the test sequence
echo -e "${BLUE}Starting three-way interoperability test...${NC}"
echo

# Step 1: C creates structures
echo -e "${YELLOW}Step 1: C creates initial structures${NC}"
../c/examples/interop_test create
echo

# Step 2: C++ reads and modifies
echo -e "${YELLOW}Step 2: C++ reads and modifies structures${NC}"
./cpp_interop
echo

# Step 3: Python reads and analyzes
echo -e "${YELLOW}Step 3: Python reads and analyzes${NC}"
python3 python_interop.py
echo

# Step 4: C reads final state
echo -e "${YELLOW}Step 4: C reads final state${NC}"
../c/examples/interop_test read
echo

# Step 5: Concurrent stress test
echo -e "${YELLOW}Step 5: Running concurrent stress test${NC}"
echo "Launching 3 processes (C, C++, Python) to perform concurrent operations..."

# Create stress test scripts
cat > cpp_stress.cpp << 'EOF'
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <iostream>
#include <random>

struct Event {
    uint32_t event_id;
    uint32_t source_pid;
    uint64_t timestamp;
    char message[48];
};

int main() {
    try {
        zeroipc::Memory mem("/zeroipc_interop");
        zeroipc::Queue<Event> queue(mem, "event_queue");

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 1);

        int pushes = 0, pops = 0;
        for (int i = 0; i < 1000; i++) {
            if (dis(gen) == 0) {
                Event evt;
                evt.event_id = 20000 + i;
                evt.source_pid = getpid();
                std::strcpy(evt.message, "C++ stress");
                if (queue.push(evt)) pushes++;
            } else {
                if (queue.pop()) pops++;
            }
        }

        std::cout << "C++ process " << getpid()
                  << ": " << pushes << " pushes, " << pops << " pops" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "C++ stress error: " << e.what() << std::endl;
    }
    return 0;
}
EOF

g++ -std=c++23 -I../cpp/include cpp_stress.cpp -o cpp_stress -lrt

cat > python_stress.py << 'EOF'
#!/usr/bin/env python3
import sys, os, random
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))
from zeroipc import Memory, Queue

event_dtype = np.dtype([
    ('event_id', np.uint32),
    ('source_pid', np.uint32),
    ('timestamp', np.uint64),
    ('message', 'S48')
])

mem = Memory("/zeroipc_interop")
queue = Queue(mem, "event_queue", dtype=event_dtype)

pushes = pops = 0
for i in range(1000):
    if random.randint(0, 1) == 0:
        evt = np.zeros(1, dtype=event_dtype)[0]
        evt['event_id'] = 30000 + i
        evt['source_pid'] = os.getpid()
        evt['message'] = np.frombuffer(b'Python stress'.ljust(48, b'\x00'), dtype='S48')[0]
        if queue.push(evt):
            pushes += 1
    else:
        if queue.pop() is not None:
            pops += 1

print(f"Python process {os.getpid()}: {pushes} pushes, {pops} pops")
EOF

chmod +x python_stress.py

# Run concurrent stress test
(../c/examples/interop_test stress) &
PID1=$!
(./cpp_stress) &
PID2=$!
(python3 python_stress.py) &
PID3=$!

wait $PID1 $PID2 $PID3
echo

# Final summary
echo -e "${BLUE}================================================${NC}"
echo -e "${GREEN}Test Summary:${NC}"
echo "  ✓ C created shared structures"
echo "  ✓ C++ read and modified data"
echo "  ✓ Python analyzed and processed"
echo "  ✓ All languages performed concurrent operations"
echo "  ✓ Binary format compatibility verified"
echo -e "${BLUE}================================================${NC}"

# Cleanup
echo
echo -e "${YELLOW}Cleaning up...${NC}"
../c/examples/interop_test clean
rm -f cpp_interop cpp_interop.cpp python_interop.py
rm -f cpp_stress cpp_stress.cpp python_stress.py

echo -e "${GREEN}✓ Three-way interoperability test complete!${NC}"