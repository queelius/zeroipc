#pragma once

#include <chrono>
#include <thread>

namespace zeroipc::detail {

/// Spin-wait with exponential backoff until predicate returns true.
/// Predicate may be a pure check or a side-effecting CAS attempt.
template<typename Pred>
void spin_wait(Pred&& pred) {
    int backoff_us = 1;
    constexpr int max_backoff_us = 1000;  // Cap at 1ms

    while (!pred()) {
        std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
        if (backoff_us < max_backoff_us) {
            backoff_us *= 2;
        }
    }
}

/// Spin-wait with exponential backoff and timeout.
/// Returns true if predicate became true, false on timeout.
template<typename Pred, typename Rep, typename Period>
[[nodiscard]] bool spin_wait_for(Pred&& pred,
                                  const std::chrono::duration<Rep, Period>& timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    int backoff_us = 1;
    constexpr int max_backoff_us = 1000;

    while (!pred()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        auto remaining_us = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - now).count();
        int sleep_us = static_cast<int>(std::min(static_cast<long>(backoff_us), remaining_us));
        if (sleep_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        }
        if (backoff_us < max_backoff_us) {
            backoff_us *= 2;
        }
    }
    return true;
}

} // namespace zeroipc::detail
