#pragma once

#include "kernel.hpp"
#include <cstdint>

namespace tk {

// ===========================================================================
// Semaphore: Counting / Binary Semaphore
// ===========================================================================
class Semaphore {
public:
    explicit Semaphore(int32_t initial_count = 1, int32_t max_count = 1)
        : count_(initial_count), max_count_(max_count) {}

    // 資源を獲得するまでブロック（待機中はCPU消費ゼロ）
    void acquire() {
        uint32_t save = detail::sched_lock();
        while (count_ <= 0) {
            uint8_t slot = detail::current_task_slot();
            wait_mask_ |= (1u << slot);
            detail::block_current_task_locked();
            detail::sched_unlock(save);
            // 他タスクから release() されて再開したら再チェック
            save = detail::sched_lock();
        }
        count_--;
        detail::sched_unlock(save);
    }

    // ノンブロッキング獲得。獲得できれば true、できなければ false
    bool try_acquire() {
        uint32_t save = detail::sched_lock();
        bool success = false;
        if (count_ > 0) {
            count_--;
            success = true;
        }
        detail::sched_unlock(save);
        return success;
    }

    // 資源を返却し、待機中タスクがあれば最優先度のタスクを1つ起床
    void release() {
        uint32_t save = detail::sched_lock();
        if (count_ < max_count_) {
            count_++;
        }
        if (wait_mask_ != 0) {
            uint8_t slot = detail::find_highest_prio_waiter(wait_mask_);
            if (slot != 0xFF) {
                wait_mask_ &= ~(1u << slot);
                detail::wake_task_locked(TaskHandle{slot});
            }
        }
        detail::sched_unlock(save);
    }

    int32_t count() const {
        uint32_t save = detail::sched_lock();
        int32_t c = count_;
        detail::sched_unlock(save);
        return c;
    }

private:
    int32_t count_;
    int32_t max_count_;
    uint16_t wait_mask_{0};
};

using BinarySemaphore = Semaphore;

// ===========================================================================
// Mutex: Mutual Exclusion Lock with Ownership and RAII Support
// ===========================================================================
class Mutex {
public:
    Mutex() = default;

    // C++ BasicLockable: lock()
    void lock() {
        uint32_t save = detail::sched_lock();
        uint8_t my_slot = detail::current_task_slot();
        while (locked_) {
            wait_mask_ |= (1u << my_slot);
            detail::block_current_task_locked();
            detail::sched_unlock(save);
            save = detail::sched_lock();
        }
        locked_ = true;
        owner_slot_ = my_slot;
        detail::sched_unlock(save);
    }

    // C++ Lockable: try_lock()
    bool try_lock() {
        uint32_t save = detail::sched_lock();
        uint8_t my_slot = detail::current_task_slot();
        bool success = false;
        if (!locked_) {
            locked_ = true;
            owner_slot_ = my_slot;
            success = true;
        }
        detail::sched_unlock(save);
        return success;
    }

    // C++ BasicLockable: unlock()
    // 所有者以外のタスクが解除しようとした場合は安全に無視
    void unlock() {
        uint32_t save = detail::sched_lock();
        uint8_t my_slot = detail::current_task_slot();
        if (locked_ && owner_slot_ == my_slot) {
            locked_ = false;
            owner_slot_ = 0xFF;
            if (wait_mask_ != 0) {
                uint8_t slot = detail::find_highest_prio_waiter(wait_mask_);
                if (slot != 0xFF) {
                    wait_mask_ &= ~(1u << slot);
                    detail::wake_task_locked(TaskHandle{slot});
                }
            }
        }
        detail::sched_unlock(save);
    }

    bool is_locked() const {
        uint32_t save = detail::sched_lock();
        bool lk = locked_;
        detail::sched_unlock(save);
        return lk;
    }

private:
    bool locked_{false};
    uint8_t owner_slot_{0xFF};
    uint16_t wait_mask_{0};
};

// ===========================================================================
// LockGuard: Lightweight RAII helper (std::lock_guard equivalent)
// ===========================================================================
template <typename Lockable>
class LockGuard {
public:
    explicit LockGuard(Lockable& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Lockable& m_;
};

}  // namespace tk

