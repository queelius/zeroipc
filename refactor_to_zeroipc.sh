#!/bin/bash

# Refactoring script from posix_shm to zeroipc
set -e

echo "Starting refactoring from posix_shm to zeroipc..."

# Step 1: Rename header files
echo "Step 1: Renaming header files..."
cd include/

# Rename main file
mv posix_shm.h zeroipc.h || true

# Rename shm_* files to remove prefix (they'll be in namespace)
mv shm_array.h array.h || true
mv shm_queue.h queue.h || true
mv shm_stack.h stack.h || true
mv shm_table.h table.h || true
mv shm_hash_map.h map.h || true
mv shm_set.h set.h || true
mv shm_bitset.h bitset.h || true
mv shm_ring_buffer.h ring.h || true
mv shm_object_pool.h pool.h || true
mv shm_atomic.h atomic.h || true
mv shm_span.h span.h || true
mv shm_simd_utils.h simd_utils.h || true

cd ..

echo "Step 2: Update include statements..."
# Update all #include statements
find . -type f \( -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -exec sed -i \
    -e 's|#include "posix_shm.h"|#include "zeroipc.h"|g' \
    -e 's|#include "shm_array.h"|#include "array.h"|g' \
    -e 's|#include "shm_queue.h"|#include "queue.h"|g' \
    -e 's|#include "shm_stack.h"|#include "stack.h"|g' \
    -e 's|#include "shm_table.h"|#include "table.h"|g' \
    -e 's|#include "shm_hash_map.h"|#include "map.h"|g' \
    -e 's|#include "shm_set.h"|#include "set.h"|g' \
    -e 's|#include "shm_bitset.h"|#include "bitset.h"|g' \
    -e 's|#include "shm_ring_buffer.h"|#include "ring.h"|g' \
    -e 's|#include "shm_object_pool.h"|#include "pool.h"|g' \
    -e 's|#include "shm_atomic.h"|#include "atomic.h"|g' \
    -e 's|#include "shm_span.h"|#include "span.h"|g' \
    -e 's|#include "shm_simd_utils.h"|#include "simd_utils.h"|g' {} \;

echo "Step 3: Update include guards..."
# Update include guards
find include/ -type f -name "*.h" -exec sed -i \
    -e 's|POSIX_SHM_|ZEROIPC_|g' \
    -e 's|SHM_ARRAY_H|ZEROIPC_ARRAY_H|g' \
    -e 's|SHM_QUEUE_H|ZEROIPC_QUEUE_H|g' \
    -e 's|SHM_STACK_H|ZEROIPC_STACK_H|g' \
    -e 's|SHM_TABLE_H|ZEROIPC_TABLE_H|g' \
    -e 's|SHM_HASH_MAP_H|ZEROIPC_MAP_H|g' \
    -e 's|SHM_SET_H|ZEROIPC_SET_H|g' \
    -e 's|SHM_BITSET_H|ZEROIPC_BITSET_H|g' \
    -e 's|SHM_RING_BUFFER_H|ZEROIPC_RING_H|g' \
    -e 's|SHM_OBJECT_POOL_H|ZEROIPC_POOL_H|g' \
    -e 's|SHM_ATOMIC_H|ZEROIPC_ATOMIC_H|g' \
    -e 's|SHM_SPAN_H|ZEROIPC_SPAN_H|g' \
    -e 's|SHM_SIMD_UTILS_H|ZEROIPC_SIMD_UTILS_H|g' {} \;

echo "Step 4: Rename test files..."
cd tests/
for file in test_shm_*.cpp; do
    if [ -f "$file" ]; then
        newname="${file/test_shm_/test_}"
        mv "$file" "$newname" || true
    fi
done

# Also rename the posix_shm test
mv test_posix_shm.cpp test_zeroipc.cpp || true

cd ..

echo "Step 5: Update CMakeLists.txt files..."
# Main CMakeLists.txt
sed -i 's/posix_shm/zeroipc/g' CMakeLists.txt || true
sed -i 's/POSIX_SHM/ZEROIPC/g' CMakeLists.txt || true

# Tests CMakeLists.txt
sed -i 's/posix_shm/zeroipc/g' tests/CMakeLists.txt || true
sed -i 's/test_shm_/test_/g' tests/CMakeLists.txt || true

echo "Script complete! Manual changes still needed for:"
echo "  - Class name changes (posix_shm -> zeroipc::memory)"
echo "  - Namespace wrapping"
echo "  - Python package renaming"