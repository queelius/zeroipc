"""Tests for Barrier implementation."""

import os
import time
import threading
import pytest

from zeroipc import Memory, Barrier


class TestBarrierBasic:
    """Basic Barrier functionality tests."""

    def test_create_barrier(self):
        """Test creating a new barrier."""
        shm_name = f"/test_barrier_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "test", num_participants=4)

            assert barrier.arrived == 0
            assert barrier.generation == 0
            assert barrier.num_participants == 4
            assert barrier.name == "test"

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_barrier(self):
        """Test opening an existing barrier."""
        shm_name = f"/test_barrier_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create barrier
            barrier1 = Barrier(memory, "existing", num_participants=5)
            assert barrier1.num_participants == 5

            # Open existing barrier
            barrier2 = Barrier(memory, "existing")
            assert barrier2.num_participants == 5
            assert barrier2.arrived == 0

        finally:
            Memory.unlink(shm_name)

    def test_invalid_num_participants(self):
        """Test invalid num_participants values."""
        shm_name = f"/test_barrier_invalid_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Zero participants
            with pytest.raises(ValueError):
                Barrier(memory, "test", num_participants=0)

            # Negative participants
            with pytest.raises(ValueError):
                Barrier(memory, "test", num_participants=-1)

        finally:
            Memory.unlink(shm_name)

    def test_missing_num_participants(self):
        """Test creating barrier without num_participants."""
        shm_name = f"/test_barrier_missing_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # When barrier doesn't exist and num_participants is None, raises RuntimeError
            with pytest.raises(RuntimeError):
                Barrier(memory, "test")  # Missing num_participants

        finally:
            Memory.unlink(shm_name)

    def test_barrier_not_found(self):
        """Test opening non-existent barrier."""
        shm_name = f"/test_barrier_notfound_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError):
                Barrier(memory, "nonexistent")

        finally:
            Memory.unlink(shm_name)


class TestBarrierSynchronization:
    """Synchronization tests for Barrier."""

    def test_two_thread_barrier(self):
        """Test barrier with two threads."""
        shm_name = f"/test_barrier_two_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "sync", num_participants=2)

            phase = [0]

            def worker():
                # Phase 1
                assert phase[0] == 0
                barrier.wait()

                # Both threads should reach here together
                phase[0] += 1
                barrier.wait()

                # Phase 2
                assert phase[0] == 2

            t1 = threading.Thread(target=worker)
            t2 = threading.Thread(target=worker)

            t1.start()
            t2.start()

            t1.join()
            t2.join()

            assert phase[0] == 2
            assert barrier.arrived == 0  # Reset after release
            assert barrier.generation == 2  # Two passes

        finally:
            Memory.unlink(shm_name)

    def test_multiple_threads_barrier(self):
        """Test barrier with multiple threads."""
        shm_name = f"/test_barrier_multi_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            num_threads = 8
            barrier = Barrier(memory, "sync", num_participants=num_threads)

            counter = [0]
            thread_ids = [0] * num_threads
            lock = threading.Lock()

            def worker(thread_id):
                # All threads increment counter
                with lock:
                    counter[0] += 1

                # Wait at barrier
                barrier.wait()

                # All threads should see final count
                assert counter[0] == num_threads
                thread_ids[thread_id] = 1

            threads = []
            for i in range(num_threads):
                t = threading.Thread(target=worker, args=(i,))
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            # All threads completed
            for i in range(num_threads):
                assert thread_ids[i] == 1

        finally:
            Memory.unlink(shm_name)

    def test_barrier_reusability(self):
        """Test that barrier can be reused multiple times."""
        shm_name = f"/test_barrier_reuse_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            num_threads = 4
            num_iterations = 10
            barrier = Barrier(memory, "reusable", num_participants=num_threads)

            phase_counter = [0]
            lock = threading.Lock()

            def worker():
                for i in range(num_iterations):
                    with lock:
                        phase_counter[0] += 1
                    barrier.wait()

                    # All threads should see same phase count
                    assert phase_counter[0] == num_threads * (i + 1)

                    barrier.wait()  # Second barrier for verification

            threads = []
            for i in range(num_threads):
                t = threading.Thread(target=worker)
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            assert phase_counter[0] == num_threads * num_iterations
            assert barrier.generation == num_iterations * 2

        finally:
            Memory.unlink(shm_name)

    def test_generation_counter(self):
        """Test that generation counter increments."""
        shm_name = f"/test_barrier_gen_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "gen_test", num_participants=3)

            assert barrier.generation == 0

            gen_after_wait = [-1]

            def worker():
                barrier.wait()
                gen_after_wait[0] = barrier.generation

            t1 = threading.Thread(target=worker)
            t2 = threading.Thread(target=worker)
            t3 = threading.Thread(target=worker)

            t1.start()
            t2.start()
            t3.start()

            t1.join()
            t2.join()
            t3.join()

            # Generation should increment after all threads pass
            assert barrier.generation == 1
            assert gen_after_wait[0] == 1

        finally:
            Memory.unlink(shm_name)

    def test_arrived_counter(self):
        """Test arrived counter tracking."""
        shm_name = f"/test_barrier_arrived_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "arrived_test", num_participants=3)

            assert barrier.arrived == 0

            thread1_waiting = [False]
            thread2_waiting = [False]

            def worker1():
                thread1_waiting[0] = True
                barrier.wait()

            def worker2():
                thread2_waiting[0] = True
                barrier.wait()

            t1 = threading.Thread(target=worker1)
            t2 = threading.Thread(target=worker2)

            t1.start()
            t2.start()

            # Wait for threads to start waiting
            while not (thread1_waiting[0] and thread2_waiting[0]):
                time.sleep(0.001)

            # Give threads time to arrive at barrier
            time.sleep(0.01)

            # Two threads should be waiting
            assert barrier.arrived == 2

            # Release by having main thread arrive
            barrier.wait()

            t1.join()
            t2.join()

            # After release, arrived should reset to 0
            assert barrier.arrived == 0

        finally:
            Memory.unlink(shm_name)


class TestBarrierTimeout:
    """Timeout tests for Barrier."""

    def test_wait_timeout_success(self):
        """Test wait with timeout when barrier releases."""
        shm_name = f"/test_barrier_timeout_ok_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "timeout_test", num_participants=2)

            def worker():
                time.sleep(0.05)
                barrier.wait()

            t = threading.Thread(target=worker)
            t.start()

            # Should succeed before timeout
            result = barrier.wait(timeout=0.2)
            assert result is True

            t.join()

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_failure(self):
        """Test wait with timeout when barrier doesn't release."""
        shm_name = f"/test_barrier_timeout_fail_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "timeout_test", num_participants=3)

            start = time.time()
            result = barrier.wait(timeout=0.1)
            elapsed = time.time() - start

            assert result is False  # Should timeout
            assert elapsed >= 0.1

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_multiple_threads(self):
        """Test wait with timeout for multiple threads."""
        shm_name = f"/test_barrier_timeout_multi_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "timeout_multi", num_participants=4)

            success_count = [0]
            timeout_count = [0]
            lock = threading.Lock()

            def worker(delay_ms):
                time.sleep(delay_ms / 1000.0)
                result = barrier.wait(timeout=0.1)
                with lock:
                    if result:
                        success_count[0] += 1
                    else:
                        timeout_count[0] += 1

            # All arrive within timeout
            t1 = threading.Thread(target=worker, args=(10,))
            t2 = threading.Thread(target=worker, args=(20,))
            t3 = threading.Thread(target=worker, args=(30,))
            t4 = threading.Thread(target=worker, args=(40,))

            t1.start()
            t2.start()
            t3.start()
            t4.start()

            t1.join()
            t2.join()
            t3.join()
            t4.join()

            # All should succeed
            assert success_count[0] == 4
            assert timeout_count[0] == 0

        finally:
            Memory.unlink(shm_name)


class TestBarrierEdgeCases:
    """Edge case tests for Barrier."""

    def test_single_participant(self):
        """Test barrier with single participant."""
        shm_name = f"/test_barrier_single_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "single", num_participants=1)

            # Should pass through immediately
            barrier.wait()
            assert barrier.generation == 1

            barrier.wait()
            assert barrier.generation == 2

        finally:
            Memory.unlink(shm_name)

    def test_large_number_of_participants(self):
        """Test barrier with many participants."""
        shm_name = f"/test_barrier_large_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            num_threads = 50
            barrier = Barrier(memory, "large", num_participants=num_threads)

            counter = [0]
            lock = threading.Lock()

            def worker():
                with lock:
                    counter[0] += 1
                barrier.wait()
                assert counter[0] == num_threads

            threads = []
            for i in range(num_threads):
                t = threading.Thread(target=worker)
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            assert counter[0] == num_threads

        finally:
            Memory.unlink(shm_name)

    def test_parallel_phase_based_algorithm(self):
        """Test barrier for phase-based parallel algorithm."""
        shm_name = f"/test_barrier_phases_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            num_workers = 4
            array_size = 100
            barrier = Barrier(memory, "phase_sync", num_participants=num_workers)

            # Shared data
            data = [0] * array_size
            lock = threading.Lock()

            def worker(worker_id):
                start = worker_id * (array_size // num_workers)
                end = start + (array_size // num_workers)

                # Phase 1: Each worker initializes its section
                for i in range(start, end):
                    with lock:
                        data[i] = i

                barrier.wait()  # All workers must complete phase 1

                # Phase 2: Each worker doubles values in its section
                for i in range(start, end):
                    with lock:
                        data[i] *= 2

                barrier.wait()  # All workers must complete phase 2

                # Phase 3: Each worker verifies the entire array
                for i in range(array_size):
                    with lock:
                        assert data[i] == i * 2

                barrier.wait()  # All workers must complete verification

            threads = []
            for i in range(num_workers):
                t = threading.Thread(target=worker, args=(i,))
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            # Verify final state
            for i in range(array_size):
                assert data[i] == i * 2

            assert barrier.generation == 3  # Three barrier passes

        finally:
            Memory.unlink(shm_name)

    def test_stress_many_iterations(self):
        """Stress test with many iterations."""
        shm_name = f"/test_barrier_stress_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            num_threads = 8
            iterations = 100
            barrier = Barrier(memory, "stress", num_participants=num_threads)

            total_passes = [0]
            lock = threading.Lock()

            def worker():
                for i in range(iterations):
                    barrier.wait()
                    with lock:
                        total_passes[0] += 1

            threads = []
            for i in range(num_threads):
                t = threading.Thread(target=worker)
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            assert total_passes[0] == num_threads * iterations
            assert barrier.generation == iterations

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_barrier_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            barrier = Barrier(memory, "test", num_participants=5)

            repr_str = repr(barrier)
            assert "test" in repr_str
            assert "5" in repr_str
            assert "0" in repr_str  # arrived
            assert "generation" in repr_str

        finally:
            Memory.unlink(shm_name)
