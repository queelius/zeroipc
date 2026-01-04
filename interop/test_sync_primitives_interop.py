#!/usr/bin/env python3
"""
Cross-language interoperability tests for synchronization primitives.

Tests that C++ and Python implementations can share:
- Mutex
- RWLock
- Event
- Monitor
- Once
- Signal

Each test verifies binary compatibility by having one language create
the primitive and the other open and use it.
"""

import os
import sys
import subprocess
import time

# Add Python package to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from zeroipc import Memory
from zeroipc.mutex import Mutex
from zeroipc.rwlock import RWLock
from zeroipc.event import Event, EventMode
from zeroipc.monitor import Monitor
from zeroipc.once import Once
from zeroipc.signal import Signal


CPP_INCLUDE_PATH = os.path.join(os.path.dirname(__file__), '..', 'cpp', 'include')


def compile_cpp(code: str, output_path: str) -> bool:
    """Compile C++ code and return True if successful."""
    src_path = output_path + ".cpp"
    with open(src_path, "w") as f:
        f.write(code)

    result = subprocess.run(
        ["g++", "-std=c++23", f"-I{CPP_INCLUDE_PATH}", src_path,
         "-o", output_path, "-lrt", "-lpthread"],
        capture_output=True, text=True
    )

    if result.returncode != 0:
        print(f"C++ compilation failed: {result.stderr}")
        return False
    return True


def run_cpp(exe_path: str, timeout: float = 5.0) -> tuple[bool, str]:
    """Run C++ executable and return (success, output)."""
    try:
        result = subprocess.run([exe_path], capture_output=True, text=True, timeout=timeout)
        return result.returncode == 0, result.stdout.strip()
    except subprocess.TimeoutExpired:
        return False, "Timeout"


# =============================================================================
# Mutex Tests
# =============================================================================

def test_mutex_cpp_creates_python_uses():
    """Test C++ creates Mutex, Python opens and uses it."""
    print("\n=== Mutex: C++ creates, Python uses ===")

    shm_name = "/mutex_interop_cpp_py"

    cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/mutex.h>
#include <iostream>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}", 1024*1024);
    Mutex mtx(mem, "test_mutex");

    std::cout << "C++: Created mutex" << std::endl;
    mtx.lock();
    std::cout << "C++: Locked mutex" << std::endl;
    mtx.unlock();
    std::cout << "C++: Unlocked mutex" << std::endl;
    return 0;
}}
"""

    if not compile_cpp(cpp_code, "/tmp/mutex_create"):
        return False

    success, output = run_cpp("/tmp/mutex_create")
    print(output)
    if not success:
        return False

    try:
        memory = Memory(shm_name)
        mutex = Mutex(memory, "test_mutex")

        print("Python: Opened mutex")
        mutex.lock()
        print("Python: Locked mutex")
        mutex.unlock()
        print("Python: Unlocked mutex")

        print("OK Test PASSED")
        return True
    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


def test_mutex_python_creates_cpp_uses():
    """Test Python creates Mutex, C++ opens and uses it."""
    print("\n=== Mutex: Python creates, C++ uses ===")

    shm_name = "/mutex_interop_py_cpp"

    try:
        memory = Memory(shm_name, size=1024*1024)
        mutex = Mutex(memory, "test_mutex")

        print("Python: Created mutex")
        mutex.lock()
        print("Python: Locked and unlocked mutex")
        mutex.unlock()

        cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/mutex.h>
#include <iostream>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}");
    Mutex mtx(mem, "test_mutex");

    std::cout << "C++: Opened mutex" << std::endl;
    mtx.lock();
    std::cout << "C++: Locked mutex" << std::endl;
    mtx.unlock();
    std::cout << "C++: Unlocked mutex" << std::endl;
    return 0;
}}
"""

        if not compile_cpp(cpp_code, "/tmp/mutex_use"):
            return False

        success, output = run_cpp("/tmp/mutex_use")
        print(output)

        if success:
            print("OK Test PASSED")
        return success

    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


# =============================================================================
# RWLock Tests
# =============================================================================

def test_rwlock_cpp_creates_python_uses():
    """Test C++ creates RWLock, Python uses it."""
    print("\n=== RWLock: C++ creates, Python uses ===")

    shm_name = "/rwlock_interop_cpp_py"

    cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/rwlock.h>
#include <iostream>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}", 1024*1024);
    RWLock rwlock(mem, "test_rwlock");

    std::cout << "C++: Created rwlock" << std::endl;
    rwlock.reader_lock();
    std::cout << "C++: Acquired read lock" << std::endl;
    rwlock.reader_unlock();
    std::cout << "C++: Released read lock" << std::endl;

    rwlock.writer_lock();
    std::cout << "C++: Acquired write lock" << std::endl;
    rwlock.writer_unlock();
    std::cout << "C++: Released write lock" << std::endl;
    return 0;
}}
"""

    if not compile_cpp(cpp_code, "/tmp/rwlock_create"):
        return False

    success, output = run_cpp("/tmp/rwlock_create")
    print(output)
    if not success:
        return False

    try:
        memory = Memory(shm_name)
        rwlock = RWLock(memory, "test_rwlock")

        print("Python: Opened rwlock")
        rwlock.reader_lock()
        print("Python: Acquired read lock")
        rwlock.reader_unlock()
        print("Python: Released read lock")

        rwlock.writer_lock()
        print("Python: Acquired write lock")
        rwlock.writer_unlock()
        print("Python: Released write lock")

        print("OK Test PASSED")
        return True
    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


# =============================================================================
# Event Tests
# =============================================================================

def test_event_cpp_creates_python_uses():
    """Test C++ creates Event, Python uses it."""
    print("\n=== Event: C++ creates, Python uses ===")

    shm_name = "/event_interop_cpp_py"

    cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/event.h>
#include <iostream>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}", 1024*1024);
    Event event(mem, "test_event", EventMode::ManualReset);

    std::cout << "C++: Created event (manual reset)" << std::endl;
    event.signal();
    std::cout << "C++: Signaled event" << std::endl;
    std::cout << "C++: is_signaled=" << event.is_signaled() << std::endl;
    return 0;
}}
"""

    if not compile_cpp(cpp_code, "/tmp/event_create"):
        return False

    success, output = run_cpp("/tmp/event_create")
    print(output)
    if not success:
        return False

    try:
        memory = Memory(shm_name)
        event = Event(memory, "test_event")

        print("Python: Opened event")
        print(f"Python: is_signaled={event.is_signaled}")

        if not event.is_signaled:
            print("ERROR: Event should be signaled")
            return False

        # Reset and verify
        event.reset()
        print(f"Python: After reset, is_signaled={event.is_signaled}")

        print("OK Test PASSED")
        return True
    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


# =============================================================================
# Signal Tests
# =============================================================================

def test_signal_cpp_creates_python_uses():
    """Test C++ creates Signal, Python uses it."""
    print("\n=== Signal: C++ creates, Python uses ===")

    shm_name = "/signal_interop_cpp_py"

    cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/signal.h>
#include <iostream>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}", 1024*1024);
    Signal<double> sig(mem, "temperature", 25.5);

    std::cout << "C++: Created signal with value 25.5" << std::endl;
    sig.set(30.0);
    std::cout << "C++: Set value to 30.0" << std::endl;
    std::cout << "C++: version=" << sig.version() << std::endl;
    return 0;
}}
"""

    if not compile_cpp(cpp_code, "/tmp/signal_create"):
        return False

    success, output = run_cpp("/tmp/signal_create")
    print(output)
    if not success:
        return False

    try:
        memory = Memory(shm_name)
        signal = Signal(memory, "temperature", dtype='d')  # 'd' = double/float64

        print("Python: Opened signal")
        value = signal.get()
        print(f"Python: value={value}, version={signal.version}")

        if abs(value - 30.0) > 0.001:
            print(f"ERROR: Expected 30.0, got {value}")
            return False

        print("OK Test PASSED")
        return True
    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


def test_signal_python_creates_cpp_uses():
    """Test Python creates Signal, C++ uses it."""
    print("\n=== Signal: Python creates, C++ uses ===")

    shm_name = "/signal_interop_py_cpp"

    try:
        memory = Memory(shm_name, size=1024*1024)
        signal = Signal(memory, "temperature", initial_value=42.0, dtype='d')

        print("Python: Created signal with value 42.0")
        signal.set(100.5)
        print(f"Python: Set value to 100.5, version={signal.version}")

        cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/signal.h>
#include <iostream>
#include <cmath>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}");
    Signal<double> sig(mem, "temperature");

    double value = sig.get();
    std::cout << "C++: Opened signal, value=" << value << std::endl;
    std::cout << "C++: version=" << sig.version() << std::endl;

    if (std::abs(value - 100.5) > 0.001) {{
        std::cerr << "ERROR: Expected 100.5" << std::endl;
        return 1;
    }}
    return 0;
}}
"""

        if not compile_cpp(cpp_code, "/tmp/signal_use"):
            return False

        success, output = run_cpp("/tmp/signal_use")
        print(output)

        if success:
            print("OK Test PASSED")
        return success

    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


# =============================================================================
# Once Tests
# =============================================================================

def test_once_cpp_creates_python_uses():
    """Test C++ creates Once, Python uses it."""
    print("\n=== Once: C++ creates, Python uses ===")

    shm_name = "/once_interop_cpp_py"

    cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/once.h>
#include <iostream>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}", 1024*1024);
    Once once(mem, "test_once");

    std::cout << "C++: Created once" << std::endl;

    int call_count = 0;
    once.call([&]() {{
        call_count++;
        std::cout << "C++: First call executed" << std::endl;
    }});

    once.call([&]() {{
        call_count++;
        std::cout << "C++: Second call (should not execute)" << std::endl;
    }});

    std::cout << "C++: call_count=" << call_count << std::endl;
    return 0;
}}
"""

    if not compile_cpp(cpp_code, "/tmp/once_create"):
        return False

    success, output = run_cpp("/tmp/once_create")
    print(output)
    if not success:
        return False

    try:
        memory = Memory(shm_name)
        once = Once(memory, "test_once")

        print("Python: Opened once")
        print(f"Python: is_called={once.is_called}")

        call_count = 0
        def increment():
            nonlocal call_count
            call_count += 1
            print("Python: Should not execute!")

        once.call(increment)
        print(f"Python: call_count={call_count} (should be 0)")

        if call_count != 0:
            print("ERROR: Once should not have executed")
            return False

        print("OK Test PASSED")
        return True
    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


# =============================================================================
# Monitor Tests
# =============================================================================

def test_monitor_cpp_creates_python_uses():
    """Test C++ creates Monitor, Python uses it."""
    print("\n=== Monitor: C++ creates, Python uses ===")

    shm_name = "/monitor_interop_cpp_py"

    cpp_code = f"""
#include <zeroipc/memory.h>
#include <zeroipc/monitor.h>
#include <iostream>
using namespace zeroipc;

int main() {{
    Memory mem("{shm_name}", 1024*1024);
    Monitor mon(mem, "test_monitor");

    std::cout << "C++: Created monitor" << std::endl;
    mon.lock();
    std::cout << "C++: Locked monitor" << std::endl;
    mon.unlock();
    std::cout << "C++: Unlocked monitor" << std::endl;
    return 0;
}}
"""

    if not compile_cpp(cpp_code, "/tmp/monitor_create"):
        return False

    success, output = run_cpp("/tmp/monitor_create")
    print(output)
    if not success:
        return False

    try:
        memory = Memory(shm_name)
        monitor = Monitor(memory, "test_monitor")

        print("Python: Opened monitor")
        monitor.lock()
        print("Python: Locked monitor")
        monitor.unlock()
        print("Python: Unlocked monitor")

        print("OK Test PASSED")
        return True
    except Exception as e:
        print(f"Python failed: {e}")
        return False
    finally:
        Memory.unlink(shm_name)


# =============================================================================
# Main
# =============================================================================

def main():
    """Run all interop tests."""
    print("=" * 70)
    print("Synchronization Primitives Cross-Language Interoperability Tests")
    print("=" * 70)

    tests = [
        # Mutex
        test_mutex_cpp_creates_python_uses,
        test_mutex_python_creates_cpp_uses,
        # RWLock
        test_rwlock_cpp_creates_python_uses,
        # Event
        test_event_cpp_creates_python_uses,
        # Signal
        test_signal_cpp_creates_python_uses,
        test_signal_python_creates_cpp_uses,
        # Once
        test_once_cpp_creates_python_uses,
        # Monitor
        test_monitor_cpp_creates_python_uses,
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
    passed = sum(results)
    total = len(results)
    print(f"Results: {passed}/{total} tests passed")

    if passed == total:
        print("ALL TESTS PASSED!")
    else:
        failed_tests = [tests[i].__name__ for i, r in enumerate(results) if not r]
        print(f"Failed tests: {', '.join(failed_tests)}")

    print("=" * 70)

    return all(results)


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
