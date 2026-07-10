#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/stack.h>
#include <atomic>

using namespace zeroipc;

// A peer that crashes mid-operation leaves its slot state permanently
// claimed (WRITING/READING). These tests simulate such a ghost by poking the
// slot-state array directly, then assert that push/pop/top fail within their
// bounded spin instead of hanging, that they undo their top reservation (no
// silent item loss), and that the stack works again once the slot is
// repaired.

class CrashSafetyTest : public ::testing::Test {
protected:
    void SetUp() override { Memory::unlink("/test_crash_safety"); }
    void TearDown() override { Memory::unlink("/test_crash_safety"); }

    // Pointer to slot i's state, computed from the spec layout formula.
    static std::atomic<uint32_t>* state_ptr(Memory& mem, const char* name,
                                            size_t elem_size, size_t cap,
                                            size_t i) {
        size_t offset = 0, size = 0;
        if (!mem.find(name, offset, size)) return nullptr;
        size_t side_off = 16 + align_up(elem_size * cap, 8);
        return reinterpret_cast<std::atomic<uint32_t>*>(
            static_cast<char*>(mem.base()) + offset + side_off + i * 4);
    }
};

TEST_F(CrashSafetyTest, PopBailsOutWhenSlotStuckInWriting) {
    Memory mem("/test_crash_safety", 1024 * 1024);
    Stack<int> s(mem, "s", 8);

    ASSERT_TRUE(s.push(1));
    ASSERT_TRUE(s.push(2));

    auto* st = state_ptr(mem, "s", sizeof(int), 8, 1);
    ASSERT_NE(st, nullptr);

    // Ghost: a pusher died mid-write, slot 1 stuck in WRITING.
    st->store(Stack<int>::SLOT_WRITING);

    // Pop must fail bounded (not hang) and restore top: nothing lost.
    EXPECT_FALSE(s.pop().has_value());
    EXPECT_EQ(s.size(), 2u);

    // Repair the slot; the stack works again and no item was dropped.
    st->store(Stack<int>::SLOT_READY);
    auto v = s.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 2);
    v = s.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1);
}

TEST_F(CrashSafetyTest, PushBailsOutWhenSlotStuckAboveTop) {
    Memory mem("/test_crash_safety", 1024 * 1024);
    Stack<int> s(mem, "s", 8);

    ASSERT_TRUE(s.push(1));
    ASSERT_TRUE(s.push(2));

    auto* st = state_ptr(mem, "s", sizeof(int), 8, 2);
    ASSERT_NE(st, nullptr);

    // Ghost: a popper died holding slot 2 (above the current top).
    st->store(Stack<int>::SLOT_READING);

    // Push must fail bounded and restore top.
    EXPECT_FALSE(s.push(3));
    EXPECT_EQ(s.size(), 2u);

    // Repair; push works again.
    st->store(Stack<int>::SLOT_EMPTY);
    EXPECT_TRUE(s.push(3));
    auto v = s.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 3);
}

TEST_F(CrashSafetyTest, TopBailsOutWhenSlotStuck) {
    Memory mem("/test_crash_safety", 1024 * 1024);
    Stack<int> s(mem, "s", 8);

    ASSERT_TRUE(s.push(42));

    auto* st = state_ptr(mem, "s", sizeof(int), 8, 0);
    ASSERT_NE(st, nullptr);

    st->store(Stack<int>::SLOT_WRITING);
    EXPECT_FALSE(s.top().has_value());

    st->store(Stack<int>::SLOT_READY);
    auto v = s.top();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}
