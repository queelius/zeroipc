#!/bin/bash

echo "=== Cross-Language Data Type Compatibility Test ==="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Clean up any existing shared memory
rm -f /dev/shm/dtype_test* 2>/dev/null

# ========== TEST 1: Basic numeric types ==========
echo "Test 1: Basic numeric types (int8, int32, float, double)"

# C++ writes various numeric types
cat > cpp_dtype_writer.cpp << 'EOF'
#include <iostream>
#include <cstdint>
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

int main() {
    using namespace zeroipc;
    
    Memory mem("/dtype_test", 10*1024*1024);
    
    // Different numeric types
    Array<int8_t> arr_i8(mem, "int8_array", 10);
    Array<int32_t> arr_i32(mem, "int32_array", 10);
    Array<float> arr_f32(mem, "float_array", 10);
    Array<double> arr_f64(mem, "double_array", 10);
    
    // Write test values
    for (int i = 0; i < 10; i++) {
        arr_i8[i] = -128 + i * 25;  // Range: -128 to 97
        arr_i32[i] = -1000000 + i * 200000;
        arr_f32[i] = 3.14159f * i;
        arr_f64[i] = 2.718281828 * i;
    }
    
    std::cout << "C++ wrote numeric arrays" << std::endl;
    return 0;
}
EOF

g++ -std=c++23 -I../cpp/include cpp_dtype_writer.cpp -lrt -lpthread -o cpp_dtype_writer
./cpp_dtype_writer

# Python reads and verifies
python3 << 'EOF'
import sys
import numpy as np
sys.path.insert(0, '../python')
from zeroipc import Memory, Array

mem = Memory("/dtype_test")

# Read arrays with correct dtypes
arr_i8 = Array(mem, "int8_array", dtype=np.int8)
arr_i32 = Array(mem, "int32_array", dtype=np.int32)
arr_f32 = Array(mem, "float_array", dtype=np.float32)
arr_f64 = Array(mem, "double_array", dtype=np.float64)

print("Python reads:")
errors = 0

# Verify int8
for i in range(10):
    expected = -128 + i * 25
    if expected > 127:
        expected = expected - 256  # Handle overflow
    if arr_i8[i] != expected:
        print(f"  int8[{i}] mismatch: got {arr_i8[i]}, expected {expected}")
        errors += 1

# Verify int32
for i in range(10):
    expected = -1000000 + i * 200000
    if arr_i32[i] != expected:
        print(f"  int32[{i}] mismatch: got {arr_i32[i]}, expected {expected}")
        errors += 1

# Verify float32
for i in range(10):
    expected = 3.14159 * i
    if abs(arr_f32[i] - expected) > 0.001:
        print(f"  float[{i}] mismatch: got {arr_f32[i]}, expected {expected}")
        errors += 1

# Verify float64
for i in range(10):
    expected = 2.718281828 * i
    if abs(arr_f64[i] - expected) > 0.000001:
        print(f"  double[{i}] mismatch: got {arr_f64[i]}, expected {expected}")
        errors += 1

if errors == 0:
    print("✓ Test 1 PASSED: All numeric types match")
else:
    print(f"✗ Test 1 FAILED: {errors} mismatches")

Memory.unlink("/dtype_test")
EOF

echo ""
echo "---"
echo ""

# ========== TEST 2: Structured data types ==========
echo "Test 2: Structured data types (complex structs)"

# C writes a complex struct
cat > c_struct_writer.c << 'EOF'
#include <stdio.h>
#include <string.h>
#include "../c/include/zeroipc.h"

typedef struct {
    int32_t id;
    float x, y, z;
    double timestamp;
    char name[32];
    uint8_t flags;
    uint8_t padding[3];  // Alignment padding
} ComplexStruct;

int main() {
    zeroipc_memory_t* mem = zeroipc_memory_create("/dtype_test2", 10*1024*1024, 64);
    zeroipc_array_t* array = zeroipc_array_create(mem, "struct_array", sizeof(ComplexStruct), 5);
    
    ComplexStruct data[5] = {
        {1001, 1.0f, 2.0f, 3.0f, 1234567890.123, "Alice", 0x01, {0}},
        {1002, 4.0f, 5.0f, 6.0f, 1234567891.456, "Bob", 0x02, {0}},
        {1003, 7.0f, 8.0f, 9.0f, 1234567892.789, "Charlie", 0x04, {0}},
        {1004, 10.0f, 11.0f, 12.0f, 1234567893.012, "David", 0x08, {0}},
        {1005, 13.0f, 14.0f, 15.0f, 1234567894.345, "Eve", 0x10, {0}}
    };
    
    for (int i = 0; i < 5; i++) {
        zeroipc_array_set(array, i, &data[i]);
        printf("C wrote: id=%d, name=%s, pos=(%.1f,%.1f,%.1f)\n", 
               data[i].id, data[i].name, data[i].x, data[i].y, data[i].z);
    }
    
    zeroipc_array_close(array);
    zeroipc_memory_close(mem);
    return 0;
}
EOF

gcc -I../c/include c_struct_writer.c ../c/libzeroipc.a -lrt -lpthread -lm -o c_struct_writer
./c_struct_writer

echo ""

# Python reads the struct
python3 << 'EOF'
import sys
import numpy as np
sys.path.insert(0, '../python')
from zeroipc import Memory, Array

# Define matching structured dtype
dtype = np.dtype([
    ('id', 'i4'),
    ('x', 'f4'),
    ('y', 'f4'),
    ('z', 'f4'),
    ('timestamp', 'f8'),
    ('name', 'S32'),
    ('flags', 'u1'),
    ('padding', 'u1', 3)
])

mem = Memory("/dtype_test2")
array = Array(mem, "struct_array", dtype=dtype)

print("Python reads:")
expected_data = [
    (1001, 1.0, 2.0, 3.0, 1234567890.123, b'Alice', 0x01),
    (1002, 4.0, 5.0, 6.0, 1234567891.456, b'Bob', 0x02),
    (1003, 7.0, 8.0, 9.0, 1234567892.789, b'Charlie', 0x04),
    (1004, 10.0, 11.0, 12.0, 1234567893.012, b'David', 0x08),
    (1005, 13.0, 14.0, 15.0, 1234567894.345, b'Eve', 0x10)
]

errors = 0
for i in range(5):
    item = array[i]
    expected = expected_data[i]
    
    # Verify each field
    if item['id'] != expected[0]:
        errors += 1
    if abs(item['x'] - expected[1]) > 0.01:
        errors += 1
    if abs(item['y'] - expected[2]) > 0.01:
        errors += 1
    if abs(item['z'] - expected[3]) > 0.01:
        errors += 1
    if abs(item['timestamp'] - expected[4]) > 0.001:
        errors += 1
    
    # Clean up name comparison (remove null bytes)
    name = item['name'].split(b'\x00')[0]
    if name != expected[5]:
        errors += 1
    if item['flags'] != expected[6]:
        errors += 1
    
    print(f"  id={item['id']}, name={name.decode()}, "
          f"pos=({item['x']:.1f},{item['y']:.1f},{item['z']:.1f}), "
          f"flags=0x{item['flags']:02x}")

if errors == 0:
    print("✓ Test 2 PASSED: Complex struct matches")
else:
    print(f"✗ Test 2 FAILED: {errors} field mismatches")

Memory.unlink("/dtype_test2")
EOF

echo ""
echo "---"
echo ""

# ========== TEST 3: Endianness and alignment ==========
echo "Test 3: Multi-byte values and alignment"

# C++ writes carefully aligned data
cat > cpp_alignment_writer.cpp << 'EOF'
#include <iostream>
#include <cstdint>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>

struct AlignedStruct {
    uint8_t  byte1;
    uint8_t  padding1[7];  // Force 8-byte alignment
    uint64_t value64;
    uint16_t value16;
    uint8_t  padding2[6];  // Maintain alignment
    uint32_t value32;
    uint8_t  padding3[4];  // Total size = 32 bytes
};

int main() {
    using namespace zeroipc;
    
    Memory mem("/dtype_test3", 10*1024*1024);
    Queue<AlignedStruct> queue(mem, "aligned_queue", 100);
    
    AlignedStruct test;
    test.byte1 = 0xAB;
    test.value64 = 0x123456789ABCDEF0ULL;
    test.value16 = 0x1234;
    test.value32 = 0xDEADBEEF;
    
    // Clear padding
    for (int i = 0; i < 7; i++) test.padding1[i] = 0;
    for (int i = 0; i < 6; i++) test.padding2[i] = 0;
    for (int i = 0; i < 4; i++) test.padding3[i] = 0;
    
    queue.push(test);
    
    std::cout << "C++ wrote aligned struct:" << std::endl;
    std::cout << "  byte1: 0x" << std::hex << (int)test.byte1 << std::endl;
    std::cout << "  value64: 0x" << test.value64 << std::endl;
    std::cout << "  value16: 0x" << test.value16 << std::endl;
    std::cout << "  value32: 0x" << test.value32 << std::endl;
    std::cout << "  Total size: " << sizeof(AlignedStruct) << " bytes" << std::endl;
    
    return 0;
}
EOF

g++ -std=c++23 -I../cpp/include cpp_alignment_writer.cpp -lrt -lpthread -o cpp_alignment_writer
./cpp_alignment_writer

echo ""

# Python reads with proper alignment
python3 << 'EOF'
import sys
import numpy as np
sys.path.insert(0, '../python')
from zeroipc import Memory, Queue

# Define dtype with explicit alignment
dtype = np.dtype([
    ('byte1', 'u1'),
    ('padding1', 'u1', 7),
    ('value64', 'u8'),
    ('value16', 'u2'),
    ('padding2', 'u1', 6),
    ('value32', 'u4'),
    ('padding3', 'u1', 4)
])

mem = Memory("/dtype_test3")
queue = Queue(mem, "aligned_queue", dtype=dtype)

print("Python reads aligned struct:")
item = queue.pop()

if item is not None:
    print(f"  byte1: 0x{item['byte1']:02x}")
    print(f"  value64: 0x{item['value64']:016x}")
    print(f"  value16: 0x{item['value16']:04x}")
    print(f"  value32: 0x{item['value32']:08x}")
    print(f"  Total dtype size: {dtype.itemsize} bytes")
    
    # Verify values
    errors = 0
    if item['byte1'] != 0xAB:
        errors += 1
    if item['value64'] != 0x123456789ABCDEF0:
        errors += 1
    if item['value16'] != 0x1234:
        errors += 1
    if item['value32'] != 0xDEADBEEF:
        errors += 1
    
    if errors == 0:
        print("✓ Test 3 PASSED: Alignment preserved")
    else:
        print(f"✗ Test 3 FAILED: {errors} value mismatches")
else:
    print("✗ Test 3 FAILED: Could not read queue")

Memory.unlink("/dtype_test3")
EOF

echo ""
echo "---"
echo ""

# ========== TEST 4: Arrays and matrices ==========
echo "Test 4: Multi-dimensional arrays"

# Python writes matrix data
python3 << 'EOF'
import sys
import numpy as np
sys.path.insert(0, '../python')
from zeroipc import Memory, Array

# Create 2D matrix dtype (stored as flat array)
matrix_dtype = np.dtype([
    ('rows', 'i4'),
    ('cols', 'i4'),
    ('data', 'f8', 9)  # 3x3 matrix flattened
])

mem = Memory("/dtype_test4", size=10*1024*1024)
array = Array(mem, "matrix_array", capacity=3, dtype=matrix_dtype)

# Write test matrices
matrices = [
    np.array([[1, 2, 3], [4, 5, 6], [7, 8, 9]], dtype=np.float64),
    np.array([[9, 8, 7], [6, 5, 4], [3, 2, 1]], dtype=np.float64),
    np.eye(3, dtype=np.float64)
]

for i, mat in enumerate(matrices):
    item = np.zeros(1, dtype=matrix_dtype)[0]
    item['rows'] = 3
    item['cols'] = 3
    item['data'] = mat.flatten()
    array[i] = item
    print(f"Python wrote matrix {i}:")
    print(mat)

print("Python wrote 3 matrices")
EOF

echo ""

# C reads the matrices
cat > c_matrix_reader.c << 'EOF'
#include <stdio.h>
#include <math.h>
#include "../c/include/zeroipc.h"

typedef struct {
    int32_t rows;
    int32_t cols;
    double data[9];  // 3x3 matrix
} Matrix3x3;

int main() {
    zeroipc_memory_t* mem = zeroipc_memory_open("/dtype_test4");
    if (!mem) {
        printf("Failed to open memory\n");
        return 1;
    }
    
    zeroipc_array_t* array = zeroipc_array_open(mem, "matrix_array");
    if (!array) {
        printf("Failed to open array\n");
        return 1;
    }
    
    printf("C reads matrices:\n");
    
    for (int i = 0; i < 3; i++) {
        Matrix3x3* mat = (Matrix3x3*)zeroipc_array_get(array, i);
        if (mat) {
            printf("Matrix %d (%dx%d):\n", i, mat->rows, mat->cols);
            for (int r = 0; r < mat->rows; r++) {
                printf("  ");
                for (int c = 0; c < mat->cols; c++) {
                    printf("%.1f ", mat->data[r * mat->cols + c]);
                }
                printf("\n");
            }
        }
    }
    
    printf("✓ Test 4 PASSED: Matrices read successfully\n");
    
    zeroipc_array_close(array);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/dtype_test4");
    
    return 0;
}
EOF

gcc -I../c/include c_matrix_reader.c ../c/libzeroipc.a -lrt -lpthread -lm -o c_matrix_reader
./c_matrix_reader

echo ""
echo -e "${GREEN}=== All Data Type Interop Tests Complete ===${NC}"

# Clean up
rm -f cpp_dtype_writer cpp_alignment_writer c_struct_writer c_matrix_reader *.cpp *.c