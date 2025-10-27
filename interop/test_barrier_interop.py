#!/usr/bin/env python3
"""
Cross-language interoperability test for Barrier.

Tests that C++, Python, and C implementations can all use the same Barrier
in shared memory with correct binary compatibility.
"""

import os
import sys
import subprocess
import time

# Add Python package to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from zeroipc import Memory, Barrier


def test_cpp_creates_python_uses():
    """Test C++ creates Barrier, Python uses it."""
    print("\n=== Test 1: C++ creates, Python uses ===")

    shm_name = "/barrier_interop_cpp_py"

    # Create C++ program to create barrier
    cpp_create_code = """
#include <zeroipc/memory.h>
#include <zeroipc/barrier.h>
#include <iostream>
using namespace zeroipc;

int main() {
    Memory mem("/barrier_interop_cpp_py", 1024*1024);
    Barrier barrier(mem, "test_barrier", 3);  // 3 participants

    std::cout << "C++: Created barrier with " << barrier.num_participants()
              << " participants" << std::endl;
    return 0;
}
"""

    # Write and compile C++ program
    with open("/tmp/barrier_interop_create.cpp", "w") as f:
        f.write(cpp_create_code)

    result = subprocess.run(
        ["g++", "-std=c++23", "-I../cpp/include", "/tmp/barrier_interop_create.cpp",
         "-o", "/tmp/barrier_interop_create", "-lrt", "-lpthread"],
        capture_output=True, text=True
    )

    if result.returncode != 0:
        print(f"C++ compilation failed: {result.stderr}")
        return False

    # Run C++ program
    result = subprocess.run(["/tmp/barrier_interop_create"], capture_output=True, text=True)
    print(result.stdout.strip())

    if result.returncode != 0:
        print(f"C++ execution failed: {result.stderr}")
        return False

    # Python opens and verifies
    try:
        memory = Memory(shm_name)
        barrier = Barrier(memory, "test_barrier")

        print(f"Python: Opened barrier, num_participants={barrier.num_participants}")
        print(f"Python: arrived={barrier.arrived}, generation={barrier.generation}")

        if barrier.num_participants != 3:
            print(f"ERROR: Expected 3 participants, got {barrier.num_participants}")
            return False

        print("✓ Test 1 PASSED")
        return True

    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


def test_python_creates_cpp_uses():
    """Test Python creates Barrier, C++ uses it."""
    print("\n=== Test 2: Python creates, C++ uses ===")

    shm_name = "/barrier_interop_py_cpp"

    # Python creates barrier
    try:
        memory = Memory(shm_name, size=1024*1024)
        barrier = Barrier(memory, "test_barrier", num_participants=4)

        print(f"Python: Created barrier with {barrier.num_participants} participants")

        # Create C++ program to use barrier
        cpp_use_code = """
#include <zeroipc/memory.h>
#include <zeroipc/barrier.h>
#include <iostream>
using namespace zeroipc;

int main() {
    Memory mem("/barrier_interop_py_cpp");
    Barrier barrier(mem, "test_barrier");

    std::cout << "C++: Opened barrier" << std::endl;
    std::cout << "C++: num_participants=" << barrier.num_participants() << std::endl;
    std::cout << "C++: arrived=" << barrier.arrived() << std::endl;
    std::cout << "C++: generation=" << barrier.generation() << std::endl;

    if (barrier.num_participants() != 4) {
        std::cerr << "ERROR: Expected 4 participants" << std::endl;
        return 1;
    }

    return 0;
}
"""

        # Write and compile C++ program
        with open("/tmp/barrier_interop_use.cpp", "w") as f:
            f.write(cpp_use_code)

        result = subprocess.run(
            ["g++", "-std=c++23", "-I../cpp/include", "/tmp/barrier_interop_use.cpp",
             "-o", "/tmp/barrier_interop_use", "-lrt", "-lpthread"],
            capture_output=True, text=True
        )

        if result.returncode != 0:
            print(f"C++ compilation failed: {result.stderr}")
            return False

        # Run C++ program
        result = subprocess.run(["/tmp/barrier_interop_use"], capture_output=True, text=True)
        print(result.stdout.strip())

        if result.returncode != 0:
            print(f"C++ execution failed: {result.stderr}")
            return False

        print("✓ Test 2 PASSED")
        return True

    except Exception as e:
        print(f"Python failed: {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        Memory.unlink(shm_name)


def test_barrier_synchronization():
    """Test actual synchronization across C++ and Python processes."""
    print("\n=== Test 3: Cross-language synchronization ===")

    shm_name = "/barrier_interop_sync"

    # Python creates barrier
    try:
        memory = Memory(shm_name, size=1024*1024)
        barrier = Barrier(memory, "sync_barrier", num_participants=2)

        print("Python: Created barrier for 2 participants")

        # Create C++ program that waits at barrier
        cpp_wait_code = """
#include <zeroipc/memory.h>
#include <zeroipc/barrier.h>
#include <iostream>
using namespace zeroipc;

int main() {
    Memory mem("/barrier_interop_sync");
    Barrier barrier(mem, "sync_barrier");

    std::cout << "C++: Waiting at barrier..." << std::endl;
    barrier.wait();
    std::cout << "C++: Barrier released! generation=" << barrier.generation() << std::endl;

    return 0;
}
"""

        # Write and compile C++ program
        with open("/tmp/barrier_sync.cpp", "w") as f:
            f.write(cpp_wait_code)

        result = subprocess.run(
            ["g++", "-std=c++23", "-I../cpp/include", "/tmp/barrier_sync.cpp",
             "-o", "/tmp/barrier_sync", "-lrt", "-lpthread"],
            capture_output=True, text=True
        )

        if result.returncode != 0:
            print(f"C++ compilation failed: {result.stderr}")
            return False

        # Start C++ process in background
        cpp_proc = subprocess.Popen(["/tmp/barrier_sync"],
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    text=True)

        # Give C++ process time to arrive at barrier
        time.sleep(0.5)

        print(f"Python: arrived={barrier.arrived} (C++ should be waiting)")

        if barrier.arrived != 1:
            print(f"ERROR: Expected 1 process waiting, got {barrier.arrived}")
            cpp_proc.kill()
            return False

        print("Python: Arriving at barrier to release C++ process...")
        barrier.wait()

        print(f"Python: Barrier released! generation={barrier.generation}")

        # Wait for C++ to finish
        stdout, stderr = cpp_proc.communicate(timeout=2)
        print(stdout.strip())

        if cpp_proc.returncode != 0:
            print(f"C++ failed: {stderr}")
            return False

        print("✓ Test 3 PASSED")
        return True

    except Exception as e:
        print(f"Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        Memory.unlink(shm_name)


def main():
    """Run all interop tests."""
    print("=" * 70)
    print("Barrier Cross-Language Interoperability Tests")
    print("=" * 70)

    tests = [
        test_cpp_creates_python_uses,
        test_python_creates_cpp_uses,
        test_barrier_synchronization,
    ]

    results = []
    for test in tests:
        try:
            results.append(test())
        except Exception as e:
            print(f"\nTest failed with exception: {e}")
            import traceback
            traceback.print_exc()
            results.append(False)

    print("\n" + "=" * 70)
    print(f"Results: {sum(results)}/{len(results)} tests passed")
    print("=" * 70)

    return all(results)


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
