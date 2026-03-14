#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/rwlock.h>
#include <zeroipc/array.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unistd.h>
#include "test_config.h"

using namespace zeroipc::test;

class RWLockTest : public SharedMemoryTestBase {
};

TEST_F(RWLockTest, BasicReaderLock) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");

    rwlock.reader_lock();
    // Critical read section
    int value = 42;
    EXPECT_EQ(value, 42);
    rwlock.reader_unlock();
}

TEST_F(RWLockTest, BasicWriterLock) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");

    rwlock.writer_lock();
    // Critical write section
    int value = 42;
    EXPECT_EQ(value, 42);
    rwlock.writer_unlock();
}

TEST_F(RWLockTest, MultipleReadersAllowed) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");
    zeroipc::Array<int> data(mem, "data", 1);
    data[0] = 100;

    std::atomic<int> concurrent_readers{0};
    std::atomic<int> max_concurrent{0};

    std::vector<std::thread> readers;
    for (int i = 0; i < 8; i++) {
        readers.emplace_back([&]() {
            rwlock.reader_lock();

            int current = concurrent_readers.fetch_add(1) + 1;

            // Update max
            int expected_max = max_concurrent.load();
            while (current > expected_max &&
                   !max_concurrent.compare_exchange_weak(expected_max, current));

            // Read data
            int value = data[0];
            EXPECT_EQ(value, 100);

            std::this_thread::sleep_for(std::chrono::milliseconds(5));

            concurrent_readers.fetch_sub(1);
            rwlock.reader_unlock();
        });
    }

    for (auto& t : readers) {
        t.join();
    }

    // Multiple readers should have been concurrent
    EXPECT_GT(max_concurrent.load(), 1);
}

TEST_F(RWLockTest, WriterExcludesReaders) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");
    zeroipc::Array<int> data(mem, "data", 1);
    data[0] = 0;

    std::atomic<bool> writer_active{false};
    std::atomic<bool> reader_saw_writer{false};

    std::thread writer([&]() {
        std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);

        rwlock.writer_lock();
        writer_active = true;
        data[0] = 42;
        std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);
        writer_active = false;
        rwlock.writer_unlock();
    });

    std::thread reader([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        rwlock.reader_lock();
        if (writer_active) {
            reader_saw_writer = true;
        }
        int value = data[0];
        rwlock.reader_unlock();

        EXPECT_FALSE(reader_saw_writer);  // Writer should be exclusive
    });

    writer.join();
    reader.join();
}

TEST_F(RWLockTest, WriterExcludesWriters) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");
    zeroipc::Array<int> counter(mem, "counter", 1);
    counter[0] = 0;

    std::vector<std::thread> writers;
    for (int i = 0; i < 4; i++) {
        writers.emplace_back([&]() {
            for (int j = 0; j < 25; j++) {
                rwlock.writer_lock();
                counter[0]++;
                rwlock.writer_unlock();
            }
        });
    }

    for (auto& t : writers) {
        t.join();
    }

    EXPECT_EQ(counter[0], 100);
}

TEST_F(RWLockTest, ReaderWriterCoordination) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");
    zeroipc::Array<int> data(mem, "data", 1);
    data[0] = 0;

    std::atomic<int> reads{0};
    std::atomic<int> writes{0};

    // Multiple readers
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&]() {
            for (int j = 0; j < 10; j++) {
                rwlock.reader_lock();
                int value = data[0];
                (void)value;
                reads++;
                rwlock.reader_unlock();
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // Single writer
    std::thread writer([&]() {
        for (int i = 0; i < 10; i++) {
            rwlock.writer_lock();
            data[0]++;
            writes++;
            rwlock.writer_unlock();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    for (auto& t : readers) {
        t.join();
    }
    writer.join();

    EXPECT_EQ(reads.load(), 40);
    EXPECT_EQ(writes.load(), 10);
    EXPECT_EQ(data[0], 10);
}

TEST_F(RWLockTest, SharedLockGuard) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");

    {
        zeroipc::SharedLock lock(rwlock);
        // Read operations
        int value = 42;
        EXPECT_EQ(value, 42);
    }  // Automatically unlocked

    // Should be able to acquire write lock
    rwlock.writer_lock();
    rwlock.writer_unlock();
}

TEST_F(RWLockTest, UniqueLockGuard) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");

    {
        zeroipc::UniqueLock lock(rwlock);
        // Write operations
        int value = 42;
        EXPECT_EQ(value, 42);
    }  // Automatically unlocked

    // Should be able to acquire read lock
    rwlock.reader_lock();
    rwlock.reader_unlock();
}

TEST_F(RWLockTest, CrossProcess) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");
    zeroipc::Array<int> data(mem, "data", 1);
    data[0] = 0;

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        // Child - reader
        zeroipc::Memory child_mem(shm_name_);
        zeroipc::RWLock child_rwlock(child_mem, "test_rwlock");
        zeroipc::Array<int> child_data(child_mem, "data");

        for (int i = 0; i < 50; i++) {
            child_rwlock.reader_lock();
            int value = child_data[0];
            (void)value;
            child_rwlock.reader_unlock();
        }
        exit(0);
    } else {
        // Parent - writer
        for (int i = 0; i < 50; i++) {
            rwlock.writer_lock();
            data[0]++;
            rwlock.writer_unlock();
        }

        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(data[0], 50);
    }
}

TEST_F(RWLockTest, TryReaderLock) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");

    // Should succeed when unlocked
    EXPECT_TRUE(rwlock.try_reader_lock());
    rwlock.reader_unlock();

    // Should succeed even with other readers
    rwlock.reader_lock();
    EXPECT_TRUE(rwlock.try_reader_lock());
    rwlock.reader_unlock();
    rwlock.reader_unlock();

    // Should fail when writer holds lock
    rwlock.writer_lock();
    EXPECT_FALSE(rwlock.try_reader_lock());
    rwlock.writer_unlock();
}

TEST_F(RWLockTest, TryWriterLock) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::RWLock rwlock(mem, "test_rwlock");

    // Should succeed when unlocked
    EXPECT_TRUE(rwlock.try_writer_lock());
    rwlock.writer_unlock();

    // Should fail when reader holds lock
    rwlock.reader_lock();
    EXPECT_FALSE(rwlock.try_writer_lock());
    rwlock.reader_unlock();

    // Should fail when writer holds lock
    rwlock.writer_lock();
    EXPECT_FALSE(rwlock.try_writer_lock());
    rwlock.writer_unlock();
}
