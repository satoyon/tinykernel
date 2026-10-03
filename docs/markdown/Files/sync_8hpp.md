---
title: C:/Users/yoneda/Documents/Pico_Projects/tinykernel/tinykernel/include/tinykernel/sync.hpp

---

# C:/Users/yoneda/Documents/Pico_Projects/tinykernel/tinykernel/include/tinykernel/sync.hpp



## Namespaces

| Name           |
| -------------- |
| **[tk](Namespaces/namespacetk.md)**  |

## Classes

|                | Name           |
| -------------- | -------------- |
| class | **[tk::Semaphore](Classes/classtk_1_1_semaphore.md)** <br>計数セマフォ (Counting [Semaphore]()) / バイナリセマフォ  |
| class | **[tk::Mutex](Classes/classtk_1_1_mutex.md)** <br>所有権・優先度順ウェイクアップ・RAII 対応のミューテックス (排他ロック)  |
| class | **[tk::LockGuard](Classes/classtk_1_1_lock_guard.md)** <br>ミューテックスのロックをスコープ単位で安全に管理する RAII クラス (std::lock_guard 相当)  |




## Source code

```cpp
#pragma once

#include "kernel.hpp"
#include <cstdint>

namespace tk {

class Semaphore {
public:
    explicit Semaphore(int32_t initial_count = 1, int32_t max_count = 1)
        : count_(initial_count), max_count_(max_count) {}

    void acquire() {
        uint32_t save = detail::sched_lock();
        while (count_ <= 0) {
            uint8_t slot = detail::current_task_slot();
            wait_mask_ |= (task_mask_t{1} << slot);
            detail::block_current_task_locked();
            detail::sched_unlock(save);
            // 他タスクから release() されて再開したら再チェック
            save = detail::sched_lock();
        }
        count_--;
        detail::sched_unlock(save);
    }

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

    void release() {
        uint32_t save = detail::sched_lock();
        if (count_ < max_count_) {
            count_++;
        }
        if (wait_mask_ != 0) {
            uint8_t slot = detail::find_highest_prio_waiter(wait_mask_);
            if (slot != 0xFF) {
                wait_mask_ &= ~(task_mask_t{1} << slot);
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
    task_mask_t wait_mask_{0};
};

using BinarySemaphore = Semaphore;

class Mutex {
public:
    Mutex() = default;

    void lock() {
        uint32_t save = detail::sched_lock();
        uint8_t my_slot = detail::current_task_slot();
        while (locked_) {
            wait_mask_ |= (task_mask_t{1} << my_slot);
            detail::block_current_task_locked();
            detail::sched_unlock(save);
            save = detail::sched_lock();
        }
        locked_ = true;
        owner_slot_ = my_slot;
        detail::sched_unlock(save);
    }

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

    void unlock() {
        uint32_t save = detail::sched_lock();
        uint8_t my_slot = detail::current_task_slot();
        if (locked_ && owner_slot_ == my_slot) {
            locked_ = false;
            owner_slot_ = 0xFF;
            if (wait_mask_ != 0) {
                uint8_t slot = detail::find_highest_prio_waiter(wait_mask_);
                if (slot != 0xFF) {
                    wait_mask_ &= ~(task_mask_t{1} << slot);
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
    task_mask_t wait_mask_{0};
};

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
```


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900
