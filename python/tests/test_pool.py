"""
Test suite for lock-free memory pool in shared memory.
"""

import os
import threading
import time
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.pool import Pool


class TestPoolBasic:
    """Basic Pool functionality tests."""

    def test_create_pool(self):
        """Test creating a new pool."""
        shm_name = f"/test_pool_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create pool with 10 blocks of 1024 bytes each
            pool = Pool(memory, "test_pool", block_size=1024, block_count=10)

            assert pool.name == "test_pool"
            assert pool.block_size == 1024
            assert pool.block_count == 10

        finally:
            Memory.unlink(shm_name)

    def test_allocate_and_deallocate(self):
        """Test basic allocation and deallocation."""
        shm_name = f"/test_pool_alloc_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "alloc_pool", block_size=512, block_count=5)

            # Allocate blocks
            block1 = pool.allocate()
            assert block1 is not None

            block2 = pool.allocate()
            assert block2 is not None
            assert block2 != block1  # Different blocks

            # Deallocate
            pool.deallocate(block1)
            pool.deallocate(block2)

            # Should be able to allocate again
            block3 = pool.allocate()
            assert block3 is not None

        finally:
            Memory.unlink(shm_name)

    def test_pool_exhaustion(self):
        """Test allocating all blocks until pool is exhausted."""
        shm_name = f"/test_pool_exhaust_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            block_count = 3
            pool = Pool(memory, "exhaust_pool", block_size=256, block_count=block_count)

            # Allocate all blocks
            blocks = []
            for i in range(block_count):
                block = pool.allocate()
                assert block is not None
                blocks.append(block)

            # Pool should be exhausted
            assert pool.allocate() is None

            # Free one block
            pool.deallocate(blocks[0])

            # Should be able to allocate again
            new_block = pool.allocate()
            assert new_block is not None

            # Clean up
            pool.deallocate(new_block)
            for block in blocks[1:]:
                pool.deallocate(block)

        finally:
            Memory.unlink(shm_name)

    def test_available_blocks(self):
        """Test tracking available blocks."""
        shm_name = f"/test_pool_available_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            block_count = 5
            pool = Pool(memory, "avail_pool", block_size=256, block_count=block_count)

            # Initially all blocks available
            assert pool.available() == block_count

            # Allocate some blocks
            block1 = pool.allocate()
            assert pool.available() == block_count - 1

            block2 = pool.allocate()
            assert pool.available() == block_count - 2

            # Deallocate
            pool.deallocate(block1)
            assert pool.available() == block_count - 1

            pool.deallocate(block2)
            assert pool.available() == block_count

        finally:
            Memory.unlink(shm_name)

    def test_reset_pool(self):
        """Test resetting the pool."""
        shm_name = f"/test_pool_reset_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            block_count = 4
            pool = Pool(memory, "reset_pool", block_size=256, block_count=block_count)

            # Allocate some blocks
            blocks = []
            for i in range(3):
                blocks.append(pool.allocate())

            assert pool.available() == 1

            # Reset pool
            pool.reset()

            # All blocks should be available again
            assert pool.available() == block_count

            # Can allocate all blocks again
            new_blocks = []
            for i in range(block_count):
                block = pool.allocate()
                assert block is not None
                new_blocks.append(block)

        finally:
            Memory.unlink(shm_name)


class TestPoolBlockOperations:
    """Test operations on allocated blocks."""

    def test_write_read_block(self):
        """Test writing and reading data from blocks."""
        shm_name = f"/test_pool_rw_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "rw_pool", block_size=1024, block_count=5)

            # Allocate a block
            block_idx = pool.allocate()
            assert block_idx is not None

            # Get buffer for the block
            buffer = pool.get_block_buffer(block_idx)
            assert len(buffer) == 1024

            # Write data
            data = b"Hello, Pool!" + b"\x00" * (1024 - 12)
            buffer[:] = data

            # Read data back
            read_data = bytes(buffer)
            assert read_data[:12] == b"Hello, Pool!"

            pool.deallocate(block_idx)

        finally:
            Memory.unlink(shm_name)

    def test_block_independence(self):
        """Test that blocks are independent."""
        shm_name = f"/test_pool_indep_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "indep_pool", block_size=512, block_count=5)

            # Allocate multiple blocks
            block1 = pool.allocate()
            block2 = pool.allocate()

            buffer1 = pool.get_block_buffer(block1)
            buffer2 = pool.get_block_buffer(block2)

            # Write different data
            buffer1[:4] = b"AAA\x00"
            buffer2[:4] = b"BBB\x00"

            # Verify independence
            assert bytes(buffer1[:3]) == b"AAA"
            assert bytes(buffer2[:3]) == b"BBB"

            pool.deallocate(block1)
            pool.deallocate(block2)

        finally:
            Memory.unlink(shm_name)


class TestPoolConcurrency:
    """Test concurrent access to Pool."""

    def test_concurrent_allocations(self):
        """Test concurrent allocation from multiple threads."""
        shm_name = f"/test_pool_concurrent_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "concurrent_pool", block_size=256, block_count=20)

            allocated_blocks = []
            lock = threading.Lock()

            def allocator():
                for _ in range(3):
                    block = pool.allocate()
                    if block is not None:
                        with lock:
                            allocated_blocks.append(block)
                    time.sleep(0.001)  # Small delay

            # Create threads
            threads = []
            for _ in range(5):
                t = threading.Thread(target=allocator)
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Check for duplicates
            unique_blocks = set(allocated_blocks)
            assert len(unique_blocks) == len(allocated_blocks)

            # Clean up
            for block in allocated_blocks:
                pool.deallocate(block)

        finally:
            Memory.unlink(shm_name)

    def test_concurrent_alloc_dealloc(self):
        """Test concurrent allocation and deallocation."""
        shm_name = f"/test_pool_mixed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "mixed_pool", block_size=256, block_count=10)

            def worker():
                local_blocks = []
                for _ in range(10):
                    # Allocate
                    block = pool.allocate()
                    if block is not None:
                        local_blocks.append(block)

                    # Sometimes deallocate
                    if len(local_blocks) > 2:
                        pool.deallocate(local_blocks.pop(0))

                    time.sleep(0.0001)

                # Clean up remaining
                for block in local_blocks:
                    pool.deallocate(block)

            # Create threads
            threads = []
            for _ in range(3):
                t = threading.Thread(target=worker)
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # All blocks should be available
            assert pool.available() == 10

        finally:
            Memory.unlink(shm_name)


class TestPoolEdgeCases:
    """Test edge cases and error conditions."""

    def test_zero_blocks_error(self):
        """Test that zero block count raises error."""
        shm_name = f"/test_pool_zero_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(ValueError, match="block_count must be greater than 0"):
                Pool(memory, "zero_pool", block_size=256, block_count=0)

        finally:
            Memory.unlink(shm_name)

    def test_zero_block_size_error(self):
        """Test that zero block size raises error."""
        shm_name = f"/test_pool_size_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(ValueError, match="block_size must be greater than 0"):
                Pool(memory, "size_pool", block_size=0, block_count=10)

        finally:
            Memory.unlink(shm_name)

    def test_deallocate_invalid_block(self):
        """Test deallocating invalid block index."""
        shm_name = f"/test_pool_invalid_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "invalid_pool", block_size=256, block_count=5)

            # Try to deallocate invalid indices
            pool.deallocate(-1)  # Should handle gracefully
            pool.deallocate(10)  # Beyond range
            pool.deallocate(None)  # None value

            # Pool should still work
            block = pool.allocate()
            assert block is not None
            pool.deallocate(block)

        finally:
            Memory.unlink(shm_name)

    def test_double_deallocate(self):
        """Test deallocating the same block twice."""
        shm_name = f"/test_pool_double_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "double_pool", block_size=256, block_count=5)

            block = pool.allocate()
            assert block is not None

            # First deallocate
            pool.deallocate(block)

            # Second deallocate (should handle gracefully)
            pool.deallocate(block)

            # Pool should still be functional
            assert pool.available() <= pool.block_count

        finally:
            Memory.unlink(shm_name)


class TestPoolPersistence:
    """Test pool persistence across processes."""

    def test_reopen_pool(self):
        """Test reopening an existing pool."""
        shm_name = f"/test_pool_persist_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create and allocate some blocks
            pool1 = Pool(memory, "persist_pool", block_size=512, block_count=8)
            block1 = pool1.allocate()
            block2 = pool1.allocate()

            buffer1 = pool1.get_block_buffer(block1)
            buffer1[:5] = b"TEST\x00"

            # Open existing pool
            pool2 = Pool(memory, "persist_pool")

            # Should see same state
            assert pool2.available() == 6  # 8 - 2 allocated

            # Can read the data
            buffer2 = pool2.get_block_buffer(block1)
            assert bytes(buffer2[:4]) == b"TEST"

            # Clean up
            pool2.deallocate(block1)
            pool2.deallocate(block2)

        finally:
            Memory.unlink(shm_name)


class TestPoolStatistics:
    """Test pool statistics and monitoring."""

    def test_pool_stats(self):
        """Test getting pool statistics."""
        shm_name = f"/test_pool_stats_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            pool = Pool(memory, "stats_pool", block_size=256, block_count=10)

            # Get initial stats
            stats = pool.get_stats()
            assert stats['total_blocks'] == 10
            assert stats['available_blocks'] == 10
            assert stats['allocated_blocks'] == 0
            assert stats['block_size'] == 256

            # Allocate some blocks
            blocks = []
            for i in range(3):
                blocks.append(pool.allocate())

            stats = pool.get_stats()
            assert stats['available_blocks'] == 7
            assert stats['allocated_blocks'] == 3

            # Deallocate
            for block in blocks:
                pool.deallocate(block)

            stats = pool.get_stats()
            assert stats['available_blocks'] == 10
            assert stats['allocated_blocks'] == 0

        finally:
            Memory.unlink(shm_name)