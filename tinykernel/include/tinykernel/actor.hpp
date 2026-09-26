#pragma once

#include "kernel.hpp"
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace tk {

struct Message {
    uint32_t id;
    uintptr_t arg;
};

// ===========================================================================
// EventHub: Ultra-lightweight type-based Pub/Sub dispatcher
// ===========================================================================
template <typename MsgType, size_t MaxSubscribers = 4>
class EventHub {
    struct Subscriber {
        void* actor{nullptr};
        bool (*post_fn)(void*, const MsgType&){nullptr};
    };

    static inline Subscriber subs_[MaxSubscribers]{};
    static inline size_t count_{0};

public:
    static bool subscribe(void* actor, bool (*post_fn)(void*, const MsgType&)) {
        uint32_t save = detail::sched_lock();
        if (count_ >= MaxSubscribers) {
            detail::sched_unlock(save);
            return false;
        }
        for (size_t i = 0; i < count_; ++i) {
            if (subs_[i].actor == actor) {
                detail::sched_unlock(save);
                return true;  // Already registered
            }
        }
        subs_[count_++] = {actor, post_fn};
        detail::sched_unlock(save);
        return true;
    }

    static bool unsubscribe(void* actor) {
        uint32_t save = detail::sched_lock();
        for (size_t i = 0; i < count_; ++i) {
            if (subs_[i].actor == actor) {
                for (size_t j = i; j + 1 < count_; ++j) {
                    subs_[j] = subs_[j + 1];
                }
                count_--;
                detail::sched_unlock(save);
                return true;
            }
        }
        detail::sched_unlock(save);
        return false;
    }

    // Publish message to all registered actors
    static size_t publish(const MsgType& msg) {
        Subscriber local_subs[MaxSubscribers];
        size_t n = 0;
        {
            uint32_t save = detail::sched_lock();
            n = count_;
            for (size_t i = 0; i < n; ++i) {
                local_subs[i] = subs_[i];
            }
            detail::sched_unlock(save);
        }

        size_t delivered = 0;
        for (size_t i = 0; i < n; ++i) {
            if (local_subs[i].post_fn && local_subs[i].post_fn(local_subs[i].actor, msg)) {
                delivered++;
            }
        }
        return delivered;
    }

    static size_t subscriber_count() {
        uint32_t save = detail::sched_lock();
        size_t n = count_;
        detail::sched_unlock(save);
        return n;
    }
};

// Global publish functions (No actor instance required)
template <typename MsgType>
inline size_t publish(const MsgType& msg) {
    return EventHub<MsgType>::publish(msg);
}

inline size_t publish(uint32_t id, uintptr_t arg = 0) {
    return publish(Message{id, arg});
}

// ===========================================================================
// Actor: Message-driven task base class
// ===========================================================================
template <typename MsgType = Message, size_t QueueSize = 8>
class Actor {
    static_assert(QueueSize > 0, "QueueSize must be greater than 0");

public:
    Actor() = default;
    virtual ~Actor() {
        EventHub<MsgType>::unsubscribe(this);
    }

    // タスクとして起動し、EventHub に自身を自動登録する
    bool start(uint32_t prio = PRIO_NORMAL) {
        EventHub<MsgType>::subscribe(this, [](void* obj, const MsgType& m) {
            return static_cast<Actor<MsgType, QueueSize>*>(obj)->post(m);
        });
        handle_ = tk::create(task_entry, this, prio);
        return handle_.is_valid();
    }

    // メッセージを送信する（非同期、スレッドセーフ、割り込みセーフ、値コピー）
    bool post(const MsgType& msg) {
        uint32_t save = detail::sched_lock();
        if (count_ >= QueueSize) {
            detail::sched_unlock(save);
            return false;
        }
        queue_[tail_] = msg;
        tail_ = (tail_ + 1) % QueueSize;
        count_++;
        if (waiting_) {
            waiting_ = false;
            detail::wake_task_locked(handle_);
        }
        detail::sched_unlock(save);
        return true;
    }

    // ムーブセマンティクスによる送信
    bool post(MsgType&& msg) {
        uint32_t save = detail::sched_lock();
        if (count_ >= QueueSize) {
            detail::sched_unlock(save);
            return false;
        }
        queue_[tail_] = std::move(msg);
        tail_ = (tail_ + 1) % QueueSize;
        count_++;
        if (waiting_) {
            waiting_ = false;
            detail::wake_task_locked(handle_);
        }
        detail::sched_unlock(save);
        return true;
    }

    // MsgType が tk::Message の場合のコンビニエンス関数
    template <typename U = MsgType>
    typename std::enable_if<std::is_same<U, Message>::value, bool>::type
    post(uint32_t id, uintptr_t arg = 0) {
        return post(Message{id, arg});
    }

    TaskHandle handle() const { return handle_; }

protected:
    // タスク起動時にイベントループ開始直前に呼ばれる初期化関数（タスクコンテキスト内）
    virtual void on_start() {}

    // 派生クラスでオーバーライドしてメッセージを処理する（値への参照で安全に渡される）
    virtual void on_message(const MsgType& msg) = 0;

private:
    static void task_entry(void* arg) {
        static_cast<Actor<MsgType, QueueSize>*>(arg)->event_loop();
    }

    void event_loop() {
        on_start();
        for (;;) {
            MsgType msg{};
            uint32_t save = detail::sched_lock();
            while (count_ == 0) {
                waiting_ = true;
                detail::block_current_task_locked();
                detail::sched_unlock(save);
                // コンテキストスイッチされ、他のタスクが走る。
                // wake_task_locked で起こされたらここに戻る。
                save = detail::sched_lock();
            }
            msg = std::move(queue_[head_]);
            head_ = (head_ + 1) % QueueSize;
            count_--;
            waiting_ = false;
            detail::sched_unlock(save);

            // メッセージディスパッチ（ロック解除状態で実行）
            on_message(msg);
        }
    }

    MsgType queue_[QueueSize]{};
    size_t head_{0};
    size_t tail_{0};
    size_t count_{0};
    bool waiting_{false};
    TaskHandle handle_{};
};

}  // namespace tk
