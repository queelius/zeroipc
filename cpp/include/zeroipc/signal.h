#pragma once

#include <atomic>
#include <functional>
#include <vector>
#include <chrono>
#include <string_view>
#include <stdexcept>
#include "memory.h"
#include "mutex.h"

namespace zeroipc {

/**
 * @brief Signal state for shared memory
 *
 * Binary layout:
 * - 8 bytes: version (increments on each change)
 * - T bytes: value
 */
template<typename T>
struct SignalState {
    std::atomic<uint64_t> version{0};
    T value;

    SignalState() : version(0), value() {}
    explicit SignalState(const T& val) : version(0), value(val) {}
};

/**
 * @brief Reactive signal for fine-grained reactivity across processes
 *
 * A Signal<T> stores a value that can be observed for changes. When the value
 * changes, the version number increments, allowing other processes to detect
 * and react to changes.
 *
 * Features:
 * - Fine-grained reactivity (SolidJS/Preact style)
 * - Version tracking for efficient polling
 * - Cross-process change detection
 * - Local callbacks via on_change()
 * - Atomic updates via update()
 *
 * This enables reactive programming patterns across shared memory!
 *
 * Example:
 * @code
 * zeroipc::Memory mem("/data", 1024 * 1024);
 * zeroipc::Signal<int> counter(mem, "counter", 0);
 *
 * // Process A - producer
 * counter.set(counter.get() + 1);
 *
 * // Process B - reactive consumer
 * uint64_t last_version = counter.version();
 * while (true) {
 *     if (counter.has_changed(last_version)) {
 *         std::cout << "Counter changed to: " << counter.get() << "\n";
 *         last_version = counter.version();
 *     }
 * }
 *
 * // Or wait for changes
 * counter.wait_for_change(last_version, std::chrono::seconds(1));
 * @endcode
 */
template<typename T>
class Signal {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "Signal type must be trivially copyable");

    /**
     * @brief Create or open a Signal
     * @param mem Memory region
     * @param name Unique name for this signal
     * @param initial_value Initial value (only used if creating new signal)
     * @param create_if_missing If true, creates new signal; if false, opens existing
     */
    Signal(Memory& mem, std::string_view name,
           const T& initial_value, bool create_if_missing)
        : mem_(mem) {

        std::string state_name = std::string(name) + "_state";
        std::string mutex_name = std::string(name) + "_mtx";

        auto entry = mem.table()->find(name);

        if (entry) {
            // Open existing
            auto state_entry = mem.table()->find(state_name);
            if (!state_entry) {
                throw std::runtime_error("Signal state not found");
            }

            state_ = reinterpret_cast<SignalState<T>*>(
                static_cast<char*>(mem.data()) + state_entry->offset
            );

            mutex_ = std::make_unique<Mutex>(mem, mutex_name);
        } else {
            if (!create_if_missing) {
                throw std::runtime_error("Signal not found: " + std::string(name));
            }

            // Create new
            size_t state_offset = mem.allocate(state_name, sizeof(SignalState<T>));
            state_ = reinterpret_cast<SignalState<T>*>(
                static_cast<char*>(mem.data()) + state_offset
            );
            new (state_) SignalState<T>(initial_value);

            mutex_ = std::make_unique<Mutex>(mem, mutex_name);

            // Add marker
            size_t marker_offset = mem.allocate(name, 1);
            (void)marker_offset;
        }
    }

    /**
     * @brief Create a new Signal
     * @param mem Memory region
     * @param name Unique name for this signal
     * @param initial_value Initial value
     */
    Signal(Memory& mem, std::string_view name, const T& initial_value = T())
        : Signal(mem, name, initial_value, true) {}

    /**
     * @brief Open an existing Signal (tag-based overload)
     */
    struct OpenExisting {};
    Signal(Memory& mem, std::string_view name, OpenExisting)
        : Signal(mem, name, T(), false) {}

    /**
     * @brief Get current value
     * @return Copy of current value
     */
    [[nodiscard]] T get() const {
        mutex_->lock();
        T value = state_->value;
        mutex_->unlock();
        return value;
    }

    /**
     * @brief Set new value
     *
     * Atomically sets the value and increments the version.
     * Triggers all registered callbacks.
     *
     * @param new_value New value to set
     */
    void set(const T& new_value) {
        mutex_->lock();
        state_->value = new_value;
        state_->version.fetch_add(1, std::memory_order_release);
        mutex_->unlock();

        // Trigger local callbacks
        for (auto& callback : callbacks_) {
            callback(new_value);
        }
    }

    /**
     * @brief Atomic update with function
     *
     * Applies a function to the current value atomically.
     *
     * @param func Function that takes current value and returns new value
     *
     * Example:
     * @code
     * counter.update([](int val) { return val + 1; });
     * @endcode
     */
    template<typename F>
    void update(F&& func) {
        mutex_->lock();
        state_->value = func(state_->value);
        state_->version.fetch_add(1, std::memory_order_release);
        T new_val = state_->value;
        mutex_->unlock();

        // Trigger local callbacks
        for (auto& callback : callbacks_) {
            callback(new_val);
        }
    }

    /**
     * @brief Get current version number
     *
     * Version increments on each set/update. Use for change detection.
     *
     * @return Current version number
     */
    [[nodiscard]] uint64_t version() const {
        return state_->version.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if signal has changed since a version
     *
     * @param old_version Previous version to compare against
     * @return true if version has incremented
     */
    [[nodiscard]] bool has_changed(uint64_t old_version) const {
        return version() != old_version;
    }

    /**
     * @brief Wait for signal to change
     *
     * Blocks until the signal's version changes from old_version.
     *
     * @param old_version Version to wait for change from
     * @param timeout Maximum time to wait
     * @return true if changed, false if timeout
     */
    template<typename Rep, typename Period>
    [[nodiscard]] bool wait_for_change(
            uint64_t old_version,
            const std::chrono::duration<Rep, Period>& timeout) {

        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            if (has_changed(old_version)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        return false;
    }

    /**
     * @brief Register a callback for changes (local process only)
     *
     * Callbacks are triggered when set() or update() is called in the
     * current process. Not cross-process.
     *
     * @param callback Function to call on change
     */
    void on_change(std::function<void(const T&)> callback) {
        callbacks_.push_back(std::move(callback));
    }

    // Prevent copying
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    // Allow moving
    Signal(Signal&&) = default;
    Signal& operator=(Signal&&) = default;

private:
    Memory& mem_;
    SignalState<T>* state_;
    std::unique_ptr<Mutex> mutex_;
    std::vector<std::function<void(const T&)>> callbacks_;
};

} // namespace zeroipc
