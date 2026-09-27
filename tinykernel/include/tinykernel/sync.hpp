#pragma once

#include "kernel.hpp"
#include <cstdint>

namespace tk {

/**
 * @brief 計数セマフォ (Counting Semaphore) / バイナリセマフォ
 * 
 * 資源の獲得・返却によるタスク間同期を行います。資源がない場合はタスクが
 * `State::Blocked` に遷移して休止し、返却時に優先度の高いタスクから順に起床します。
 * 
 * @code
 * tk::Semaphore sem(0, 5); // 初期値0、最大5
 * 
 * // タスク側 (資源が空くまでブロック待機)
 * sem.acquire();
 * 
 * // シグナル送信側 (割り込みや他タスクから返却・通知)
 * sem.release();
 * @endcode
 */
class Semaphore {
public:
    /**
     * @brief セマフォを初期化します
     * @param initial_count 初期資源カウント (省略時: 1)
     * @param max_count 最大資源カウント (省略時: 1)
     */
    explicit Semaphore(int32_t initial_count = 1, int32_t max_count = 1)
        : count_(initial_count), max_count_(max_count) {}

    /**
     * @brief 資源を獲得するまで現在のタスクをブロックします
     * 
     * 資源カウントが 0 以下の場合はタスクが休止状態になり、CPU は他タスクへ譲渡されます (CPU消費ゼロ)。
     */
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

    /**
     * @brief 資源の獲得を試みます (ノンブロッキング)
     * 
     * @return true 獲得成功 (カウントが 1 減算された)
     * @return false 資源がないため獲得失敗 (ブロックせず即座に復帰)
     */
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

    /**
     * @brief 資源を返却し、待機中タスクがあれば最優先度のタスクを 1 つ起床させます
     */
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

    /** 現在の資源カウントを取得 */
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

/**
 * @brief バイナリセマフォ (初期値1, 最大1) の型エイリアス
 */
using BinarySemaphore = Semaphore;

/**
 * @brief 所有権・優先度順ウェイクアップ・RAII 対応のミューテックス (排他ロック)
 * 
 * 共有リソースやペリフェラルへの同時アクセスを防止します。
 * ロックしたタスク本人以外の誤った `unlock()` は安全に無視されます。
 * C++ の `BasicLockable` を満たしており、`std::lock_guard` や `tk::LockGuard` が利用可能です。
 * 
 * @code
 * tk::Mutex mtx;
 * 
 * {
 *     tk::LockGuard<tk::Mutex> lock(mtx);
 *     // クリティカルセクション...
 * } // 自動でアンロック
 * @endcode
 */
class Mutex {
public:
    Mutex() = default;

    /**
     * @brief ミューテックスをロックします (空くまでブロック待機)
     * 
     * 他のタスクが保持している場合は、解放されるまで自タスクを `State::Blocked` にして休止します。
     */
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

    /**
     * @brief ミューテックスのロックを試みます (ノンブロッキング)
     * 
     * @return true ロック獲得成功
     * @return false 他のタスクが保持しているため獲得失敗 (ブロックせず即座に復帰)
     */
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

    /**
     * @brief ミューテックスのロックを解除します
     * 
     * ロックを保持しているタスク本人だけが解除できます。
     * 待機中のタスクが存在する場合、最も優先度が高いタスクを 1 つ起床させます。
     */
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

    /** 現在ロックされているかどうかを判定 */
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

/**
 * @brief ミューテックスのロックをスコープ単位で安全に管理する RAII クラス (std::lock_guard 相当)
 * 
 * 構築時に `lock()` を呼び、デストラクタで自動的に `unlock()` を呼び出します。
 * 途中で return や例外が発生しても確実にロックが解放されます。
 * 
 * @tparam Lockable lock() と unlock() を持つ型 (例: tk::Mutex)
 */
template <typename Lockable>
class LockGuard {
public:
    /**
     * @brief ロック対象を受け取り、即座に lock() します
     * @param m ロック対象のミューテックス参照
     */
    explicit LockGuard(Lockable& m) : m_(m) { m_.lock(); }

    /** スコープ終了時に自動的に unlock() します */
    ~LockGuard() { m_.unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Lockable& m_;
};

}  // namespace tk
