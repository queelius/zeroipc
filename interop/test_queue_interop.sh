#!/bin/bash

echo "=== Cross-Language Queue Interoperability Test ==="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Clean up any existing shared memory
rm -f /dev/shm/queue_test* 2>/dev/null

# Test 1: C++ writes, Python reads
echo "Test 1: C++ creates queue and pushes data..."
cat > cpp_queue_writer.cpp << 'EOF'
#include <iostream>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>

int main() {
    using namespace zeroipc;
    
    Memory mem("/queue_test", 1024*1024);
    Queue<int> queue(mem, "test_queue", 100);
    
    // Push some values
    for (int i = 1; i <= 5; i++) {
        queue.push(i * 10);
        std::cout << "C++ pushed: " << i * 10 << std::endl;
    }
    
    std::cout << "C++ done. Queue size: " << queue.size() << std::endl;
    return 0;
}
EOF

g++ -std=c++23 -I../cpp/include cpp_queue_writer.cpp -lrt -lpthread -o cpp_queue_writer
./cpp_queue_writer

echo ""
echo "Python reads from queue..."
python3 << 'EOF'
import sys
sys.path.insert(0, '../python')
from zeroipc import Memory, Queue
import numpy as np

mem = Memory("/queue_test")
queue = Queue(mem, "test_queue", dtype=np.int32)

print(f"Python found queue with size: {queue.size()}")

# Pop all values
values = []
while not queue.empty():
    val = queue.pop()
    values.append(val)
    print(f"Python popped: {val}")

# Verify
expected = [10, 20, 30, 40, 50]
if values == expected:
    print("✓ Test 1 PASSED: C++ -> Python")
else:
    print(f"✗ Test 1 FAILED: Expected {expected}, got {values}")
    
Memory.unlink("/queue_test")
EOF

echo ""
echo "---"
echo ""

# Test 2: Python writes, C reads
echo "Test 2: Python creates queue and pushes data..."
python3 << 'EOF'
import sys
sys.path.insert(0, '../python')
from zeroipc import Memory, Queue
import numpy as np

mem = Memory("/queue_test2", size=1024*1024)
queue = Queue(mem, "py_queue", capacity=50, dtype=np.float32)

# Push some values
for i in range(1, 6):
    val = i * 3.14
    queue.push(val)
    print(f"Python pushed: {val:.2f}")

print(f"Python done. Queue size: {queue.size()}")
EOF

echo ""
echo "C reads from queue..."
cat > c_queue_reader.c << 'EOF'
#include <stdio.h>
#include "../c/include/zeroipc.h"

int main() {
    zeroipc_memory_t* mem = zeroipc_memory_open("/queue_test2");
    if (!mem) {
        printf("Failed to open shared memory\n");
        return 1;
    }
    
    zeroipc_queue_t* queue = zeroipc_queue_open(mem, "py_queue");
    if (!queue) {
        printf("Failed to open queue\n");
        return 1;
    }
    
    printf("C found queue with size: %zu\n", zeroipc_queue_size(queue));
    
    // Pop all values
    float val;
    while (!zeroipc_queue_empty(queue)) {
        if (zeroipc_queue_pop(queue, &val) == ZEROIPC_OK) {
            printf("C popped: %.2f\n", val);
        }
    }
    
    printf("✓ Test 2 PASSED: Python -> C\n");
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/queue_test2");
    
    return 0;
}
EOF

gcc -I../c/include c_queue_reader.c ../c/libzeroipc.a -lrt -lpthread -o c_queue_reader
./c_queue_reader

echo ""
echo "---"
echo ""

# Test 3: All three languages using same queue
echo "Test 3: Multi-language concurrent access..."
echo "Creating queue in C..."

cat > c_queue_create.c << 'EOF'
#include <stdio.h>
#include "../c/include/zeroipc.h"

int main() {
    zeroipc_memory_t* mem = zeroipc_memory_create("/queue_test3", 10*1024*1024, 128);
    zeroipc_queue_t* queue = zeroipc_queue_create(mem, "shared_queue", sizeof(int), 1000);
    
    // Push initial values
    for (int i = 100; i < 105; i++) {
        zeroipc_queue_push(queue, &i);
        printf("C pushed: %d\n", i);
    }
    
    zeroipc_queue_close(queue);
    zeroipc_memory_close(mem);
    return 0;
}
EOF

gcc -I../c/include c_queue_create.c ../c/libzeroipc.a -lrt -lpthread -o c_queue_create
./c_queue_create

echo ""
echo "C++ adds more values..."
cat > cpp_queue_add.cpp << 'EOF'
#include <iostream>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>

int main() {
    using namespace zeroipc;
    
    Memory mem("/queue_test3");
    Queue<int> queue(mem, "shared_queue");
    
    for (int i = 200; i < 205; i++) {
        queue.push(i);
        std::cout << "C++ pushed: " << i << std::endl;
    }
    
    return 0;
}
EOF

g++ -std=c++23 -I../cpp/include cpp_queue_add.cpp -lrt -lpthread -o cpp_queue_add
./cpp_queue_add

echo ""
echo "Python reads all values..."
python3 << 'EOF'
import sys
sys.path.insert(0, '../python')
from zeroipc import Memory, Queue
import numpy as np

mem = Memory("/queue_test3")
queue = Queue(mem, "shared_queue", dtype=np.int32)

print(f"Python found queue with size: {queue.size()}")

# Read all values
all_values = []
while not queue.empty():
    val = queue.pop()
    all_values.append(val)

print(f"Python read {len(all_values)} values:")
print(f"  C values: {all_values[:5]}")
print(f"  C++ values: {all_values[5:10]}")

# Verify
c_values = list(range(100, 105))
cpp_values = list(range(200, 205))
expected = c_values + cpp_values

if all_values == expected:
    print("✓ Test 3 PASSED: Multi-language queue")
else:
    print(f"✗ Test 3 FAILED")
    
Memory.unlink("/queue_test3")
EOF

echo ""
echo -e "${GREEN}=== All Queue Interop Tests Complete ===${NC}"

# Clean up
rm -f cpp_queue_writer cpp_queue_add c_queue_reader c_queue_create *.cpp *.c