package zeroipc

import (
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestQueueBasic(t *testing.T) {
	name := "/test_go_queue"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	q, err := NewQueue[int32](mem, "test_queue", 10)
	if err != nil {
		t.Fatalf("NewQueue failed: %v", err)
	}

	// Requested 10, rounded up to the next power of two (wrap-safety)
	if q.Capacity() != 16 {
		t.Errorf("Capacity = %d, want 16", q.Capacity())
	}

	// Test empty
	if !q.Empty() {
		t.Error("New queue should be empty")
	}

	// Push and pop
	if !q.Push(42) {
		t.Error("Push should succeed")
	}

	if q.Empty() {
		t.Error("Queue should not be empty after push")
	}

	val, ok := q.Pop()
	if !ok {
		t.Error("Pop should succeed")
	}
	if val != 42 {
		t.Errorf("Pop returned %d, expected 42", val)
	}

	if !q.Empty() {
		t.Error("Queue should be empty after pop")
	}

	// Fill queue to capacity
	for i := 0; i < q.Capacity(); i++ {
		if !q.Push(int32(i)) {
			t.Errorf("Push %d should succeed", i)
		}
	}

	if !q.Full() {
		t.Error("Queue should be full after pushing capacity items")
	}

	// Should not be able to push when full
	if q.Push(99) {
		t.Error("Push to full queue should fail")
	}
}

func TestQueueConcurrent(t *testing.T) {
	name := "/test_go_queue_concurrent"
	size := 4 * 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	q, err := NewQueue[int64](mem, "concurrent_queue", 1024)
	if err != nil {
		t.Fatalf("NewQueue failed: %v", err)
	}

	// 8 producers + 8 consumers, validate every item received exactly once
	numProducers := 8
	numConsumers := 8
	itemsPerProducer := 500
	totalItems := numProducers * itemsPerProducer

	var producerWg sync.WaitGroup
	var consumerWg sync.WaitGroup

	// Track which items were consumed
	consumed := make([]int32, totalItems)

	// Producers
	for p := 0; p < numProducers; p++ {
		producerWg.Add(1)
		go func(producerID int) {
			defer producerWg.Done()
			for i := 0; i < itemsPerProducer; i++ {
				val := int64(producerID*itemsPerProducer + i)
				for !q.Push(val) {
					// Retry if full
				}
			}
		}(p)
	}

	// Consumers — each records items into the consumed slice
	var consumedCount int64
	for c := 0; c < numConsumers; c++ {
		consumerWg.Add(1)
		go func() {
			defer consumerWg.Done()
			for {
				if val, ok := q.Pop(); ok {
					if val < 0 || val >= int64(totalItems) {
						t.Errorf("Out of range value: %d", val)
						return
					}
					atomic.AddInt32(&consumed[val], 1)
					if atomic.AddInt64(&consumedCount, 1) >= int64(totalItems) {
						return
					}
				} else if atomic.LoadInt64(&consumedCount) >= int64(totalItems) {
					return
				}
			}
		}()
	}

	producerWg.Wait()
	consumerWg.Wait()

	// Verify every item consumed exactly once
	for i := 0; i < totalItems; i++ {
		c := atomic.LoadInt32(&consumed[i])
		if c != 1 {
			t.Errorf("Item %d consumed %d times (expected 1)", i, c)
		}
	}
}

func TestStackBasic(t *testing.T) {
	name := "/test_go_stack"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	s, err := NewStack[float64](mem, "test_stack", 10)
	if err != nil {
		t.Fatalf("NewStack failed: %v", err)
	}

	// Test empty
	if !s.Empty() {
		t.Error("New stack should be empty")
	}

	// Push and pop (LIFO order)
	s.Push(1.0)
	s.Push(2.0)
	s.Push(3.0)

	val, ok := s.Pop()
	if !ok || val != 3.0 {
		t.Errorf("Pop returned %v, %v, expected 3.0, true", val, ok)
	}

	val, ok = s.Pop()
	if !ok || val != 2.0 {
		t.Errorf("Pop returned %v, %v, expected 2.0, true", val, ok)
	}

	val, ok = s.Pop()
	if !ok || val != 1.0 {
		t.Errorf("Pop returned %v, %v, expected 1.0, true", val, ok)
	}

	if !s.Empty() {
		t.Error("Stack should be empty")
	}
}

func TestStackConcurrentMPMC(t *testing.T) {
	name := "/test_go_stack_mpmc"
	size := 4 * 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	s, err := NewStack[int64](mem, "mpmc_stack", 2048)
	if err != nil {
		t.Fatalf("NewStack failed: %v", err)
	}

	// 4 pushers + 4 poppers, validate data integrity
	numPushers := 4
	numPoppers := 4
	itemsPerPusher := 500
	totalItems := numPushers * itemsPerPusher

	var pushWg sync.WaitGroup
	var popWg sync.WaitGroup
	consumed := make([]int32, totalItems)

	for p := 0; p < numPushers; p++ {
		pushWg.Add(1)
		go func(id int) {
			defer pushWg.Done()
			for i := 0; i < itemsPerPusher; i++ {
				val := int64(id*itemsPerPusher + i)
				for !s.Push(val) {
					// Retry if full
				}
			}
		}(p)
	}

	var poppedCount int64
	for c := 0; c < numPoppers; c++ {
		popWg.Add(1)
		go func() {
			defer popWg.Done()
			for {
				if val, ok := s.Pop(); ok {
					if val < 0 || val >= int64(totalItems) {
						t.Errorf("Out of range value: %d", val)
						return
					}
					atomic.AddInt32(&consumed[val], 1)
					if atomic.AddInt64(&poppedCount, 1) >= int64(totalItems) {
						return
					}
				} else if atomic.LoadInt64(&poppedCount) >= int64(totalItems) {
					return
				}
			}
		}()
	}

	pushWg.Wait()
	popWg.Wait()

	// Verify every item consumed exactly once
	for i := 0; i < totalItems; i++ {
		c := atomic.LoadInt32(&consumed[i])
		if c != 1 {
			t.Errorf("Item %d consumed %d times (expected 1)", i, c)
		}
	}
}

func TestSemaphoreBasic(t *testing.T) {
	name := "/test_go_semaphore"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	sem, err := NewSemaphore(mem, "test_sem", 3, 5)
	if err != nil {
		t.Fatalf("NewSemaphore failed: %v", err)
	}

	if sem.Count() != 3 {
		t.Errorf("Count = %d, expected 3", sem.Count())
	}

	// Acquire 3 times
	if !sem.TryAcquire() {
		t.Error("TryAcquire 1 should succeed")
	}
	if !sem.TryAcquire() {
		t.Error("TryAcquire 2 should succeed")
	}
	if !sem.TryAcquire() {
		t.Error("TryAcquire 3 should succeed")
	}

	// Should fail now
	if sem.TryAcquire() {
		t.Error("TryAcquire 4 should fail")
	}

	if sem.Count() != 0 {
		t.Errorf("Count = %d, expected 0", sem.Count())
	}

	// Release
	sem.Release()
	sem.Release()

	if sem.Count() != 2 {
		t.Errorf("Count = %d, expected 2", sem.Count())
	}
}

func TestSemaphoreConcurrentRelease(t *testing.T) {
	name := "/test_go_sem_release"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	maxCount := int32(10)
	sem, err := NewSemaphore(mem, "test_sem_rel", 0, maxCount)
	if err != nil {
		t.Fatalf("NewSemaphore failed: %v", err)
	}

	// 20 goroutines all try to Release() — count should never exceed maxCount
	var wg sync.WaitGroup
	var successCount int64
	var failCount int64

	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := sem.Release(); err != nil {
				atomic.AddInt64(&failCount, 1)
			} else {
				atomic.AddInt64(&successCount, 1)
			}
		}()
	}

	wg.Wait()

	count := sem.Count()
	if count > maxCount {
		t.Errorf("Count %d exceeds maxCount %d — TOCTOU race detected!", count, maxCount)
	}
	if successCount != int64(maxCount) {
		t.Errorf("Expected exactly %d successful releases, got %d", maxCount, successCount)
	}
	if failCount != int64(20-maxCount) {
		t.Errorf("Expected exactly %d failed releases, got %d", 20-int(maxCount), failCount)
	}
}

func TestMutexBasic(t *testing.T) {
	name := "/test_go_mutex"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	mtx, err := NewMutex(mem, "test_mutex")
	if err != nil {
		t.Fatalf("NewMutex failed: %v", err)
	}

	if mtx.IsLocked() {
		t.Error("New mutex should not be locked")
	}

	mtx.Lock()

	if !mtx.IsLocked() {
		t.Error("Mutex should be locked after Lock()")
	}

	// TryLock should fail
	if mtx.TryLock() {
		t.Error("TryLock should fail when locked")
	}

	mtx.Unlock()

	if mtx.IsLocked() {
		t.Error("Mutex should not be locked after Unlock()")
	}
}

func TestBarrierBasic(t *testing.T) {
	name := "/test_go_barrier"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	numParticipants := int32(3)
	barrier, err := NewBarrier(mem, "test_barrier", numParticipants)
	if err != nil {
		t.Fatalf("NewBarrier failed: %v", err)
	}

	var wg sync.WaitGroup
	results := make(chan int, numParticipants)

	for i := int32(0); i < numParticipants; i++ {
		wg.Add(1)
		go func(id int32) {
			defer wg.Done()
			// All goroutines wait at the barrier
			barrier.Wait()
			results <- int(id)
		}(i)
	}

	wg.Wait()
	close(results)

	count := 0
	for range results {
		count++
	}

	if count != int(numParticipants) {
		t.Errorf("Only %d goroutines completed, expected %d", count, numParticipants)
	}
}

func TestLatchBasic(t *testing.T) {
	name := "/test_go_latch"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	latch, err := NewLatch(mem, "test_latch", 3)
	if err != nil {
		t.Fatalf("NewLatch failed: %v", err)
	}

	if latch.Count() != 3 {
		t.Errorf("Count = %d, expected 3", latch.Count())
	}

	if latch.TryWait() {
		t.Error("TryWait should return false")
	}

	latch.CountDownOne()
	latch.CountDownOne()

	if latch.Count() != 1 {
		t.Errorf("Count = %d, expected 1", latch.Count())
	}

	latch.CountDownOne()

	if !latch.TryWait() {
		t.Error("TryWait should return true after countdown")
	}

	if latch.Count() != 0 {
		t.Errorf("Count = %d, expected 0", latch.Count())
	}
}

func TestOnceBasic(t *testing.T) {
	name := "/test_go_once"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	once, err := NewOnce(mem, "test_once")
	if err != nil {
		t.Fatalf("NewOnce failed: %v", err)
	}

	if once.IsCalled() {
		t.Error("New Once should not be called")
	}

	callCount := 0
	once.Do(func() {
		callCount++
	})

	if callCount != 1 {
		t.Errorf("Function called %d times, expected 1", callCount)
	}

	if !once.IsCalled() {
		t.Error("Once should be called after Do()")
	}

	// Second call should not execute
	once.Do(func() {
		callCount++
	})

	if callCount != 1 {
		t.Errorf("Function called %d times, expected 1", callCount)
	}
}

func TestOnceConcurrent(t *testing.T) {
	name := "/test_go_once_conc"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	once, err := NewOnce(mem, "test_once_conc")
	if err != nil {
		t.Fatalf("NewOnce failed: %v", err)
	}

	// 8 goroutines call Do() concurrently. The fn sleeps 5ms.
	// All goroutines must return only after fn completes.
	var callCount int64
	var fnDone int64
	var wg sync.WaitGroup

	numGoroutines := 8
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			once.Do(func() {
				atomic.AddInt64(&callCount, 1)
				time.Sleep(5 * time.Millisecond)
				atomic.StoreInt64(&fnDone, 1)
			})
			// After Do() returns, fn must have completed
			if atomic.LoadInt64(&fnDone) != 1 {
				t.Error("Do() returned before fn completed — 3-state protocol broken")
			}
		}()
	}

	wg.Wait()

	if count := atomic.LoadInt64(&callCount); count != 1 {
		t.Errorf("Function executed %d times, expected 1", count)
	}
	if !once.IsCalled() {
		t.Error("Once should be called after concurrent Do()")
	}
}
