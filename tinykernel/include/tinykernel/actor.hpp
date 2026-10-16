#pragma once

#include "kernel.hpp"
#include "thread.hpp"
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace tk {

/**
 * @brief デフォルトの軽量メッセージ構造体
 */
struct Message {
    uint32_t id;    ///< メッセージ識別子 / コマンドID
    uintptr_t arg;  ///< 引数 (ポインタまたは整数値)
};

/**
 * @brief メッセージ型ごとの超軽量 Pub/Sub ディスパッチャ
 * 
 * テンプレート引数 `MsgType` ごとにコンパイル時解決される静的レジストリです。
 * 実行時の型検索オーバーヘッドゼロ、動的メモリ確保ゼロで動作します。
 * 
 * @tparam MsgType 配信するメッセージの型
 * @tparam MaxSubscribers 同一メッセージ型を購読できる最大 Actor 数 (デフォルト: 4)
 */
template <typename MsgType, size_t MaxSubscribers = 4>
class EventHub {
    struct Subscriber {
        void* actor{nullptr};
        bool (*post_fn)(void*, const MsgType&){nullptr};
    };

    static inline Subscriber subs_[MaxSubscribers]{};
    static inline size_t count_{0};

public:
    /**
     * @brief Actor をこのメッセージ型の購読者として登録します
     * @param actor Actor インスタンスポインタ
     * @param post_fn メッセージ投入関数ポインタ
     * @return true 登録成功, false 購読者上限に達した
     */
    static bool subscribe(void* actor, bool (*post_fn)(void*, const MsgType&)) {
        uint32_t save = detail::sched_lock();
        for (size_t i = 0; i < count_; ++i) {
            if (subs_[i].actor == actor) {
                detail::sched_unlock(save);
                return true;  // 登録済み
            }
        }
        if (count_ >= MaxSubscribers) {
            detail::sched_unlock(save);
            return false;
        }
        subs_[count_++] = {actor, post_fn};
        detail::sched_unlock(save);
        return true;
    }

    /**
     * @brief Actor の購読登録を解除します
     * @param actor 解除する Actor インスタンスポインタ
     * @return true 解除成功, false 未登録
     */
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

    /**
     * @brief このメッセージ型を購読しているすべての Actor へ同報配信します
     * 
     * @param msg 配信するメッセージ実体 (各 Actor のキューへ値コピー)
     * @return size_t 配信に成功した Actor の数
     */
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

    /** 現在の購読者数を取得 */
    static size_t subscriber_count() {
        uint32_t save = detail::sched_lock();
        size_t n = count_;
        detail::sched_unlock(save);
        return n;
    }
};

/**
 * @brief メッセージの型を指定して、購読しているすべての Actor へ同報配信します
 * 
 * 送信側は Actor のインスタンスや名前を知る必要がありません (完全疎結合)。
 * メッセージの実体は各 Actor のキューへ値コピーされるため、スタック上のローカル変数でも安全に渡せます。
 * 
 * @tparam MsgType 配信するメッセージ型
 * @param msg 送信するメッセージ
 * @return size_t 配信に成功した Actor の数
 * 
 * @code
 * MyEvent ev{1, "Button Pressed"};
 * tk::publish(ev); // MyEvent を受け付けるすべての Actor に届く
 * @endcode
 */
template <typename MsgType>
inline size_t publish(const MsgType& msg) {
    return EventHub<MsgType>::publish(msg);
}

/**
 * @brief tk::Message 型の同報配信ショートカット
 * 
 * @param id メッセージID
 * @param arg 引数 (省略時: 0)
 * @return size_t 配信に成功した Actor の数
 */
inline size_t publish(uint32_t id, uintptr_t arg = 0) {
    return publish(Message{id, arg});
}

/**
 * @brief メッセージ駆動型アクターの基底クラス (`tk::Thread` 派生)
 * 
 * `tk::Thread<StackSize>` を継承しており、固定長リングバッファ付きのメッセージキューを持ちます。
 * メッセージ到着時に自動起床して `on_message()` を実行し、メッセージが無い時は CPU を消費せずブロック待機します。
 * 
 * - `tk::start()` の前 (静的登録) でも、スケジューラ起動後 (動的生成) でも `start()` で起動できます。
 * - `send_signal()` による緊急シグナルの受信 (`on_signal()`) や、`stop()` による安全な停止にも対応しています。
 * - 停止時は `State::Terminated` で待機し、スコープ離脱や `delete` (デストラクタ) 時にスロットが解放されます。
 * 
 * @tparam MsgType 受信するメッセージの型 (デフォルト: tk::Message)
 * @tparam QueueSize メッセージキューの最大保持件数 (デフォルト: 8)
 * @tparam StackSize スタックサイズ (バイト単位、デフォルト: TK_DEFAULT_STACK_SIZE)
 * 
 * @code
 * struct SensorMsg { float temp; };
 * 
 * class SensorActor : public tk::Actor<SensorMsg, 8> {
 * protected:
 *     void on_start() override {
 *         // デバイスの初期化
 *     }
 *     void on_message(const SensorMsg& msg) override {
 *         printf("Temp: %.1f\n", msg.temp);
 *     }
 * };
 * @endcode
 */
template <typename MsgType = Message,
          size_t QueueSize = 8,
          size_t StackSize = TK_DEFAULT_STACK_SIZE>
class Actor : public Thread<StackSize> {
    static_assert(QueueSize > 0, "QueueSize must be greater than 0");

public:
    Actor() = default;

    /**
     * @brief デストラクタ
     * 
     * `EventHub` からの購読を解除し、アクターが実行中であれば停止 (`SIG_STOP`) と完了待ち (`join`) を
     * 行ってからタスクスロットを解放 (`State::Free`) します。
     */
    ~Actor() override {
        EventHub<MsgType>::unsubscribe(this);
        this->release_slot();
    }

    /**
     * @brief Actor をタスクとして起動 (または再起動) し、`EventHub` へ自動登録します
     * 
     * `tk::start()` の前でも、スケジューラ起動後でも動的に呼び出すことができます。
     * 
     * @param prio タスク優先度 (省略時: tk::PRIO_NORMAL)
     * @return true 起動成功, false 起動失敗 (すでに実行中、またはタスク枠上限など)
     */
    bool start(uint32_t prio = PRIO_NORMAL) override {
        EventHub<MsgType>::subscribe(this, [](void* obj, const MsgType& m) {
            return static_cast<Actor<MsgType, QueueSize, StackSize>*>(obj)->post(m);
        });
        if (!Thread<StackSize>::start(prio)) {
            EventHub<MsgType>::unsubscribe(this);
            return false;
        }
        return true;
    }

    /**
     * @brief Actor の購読を解除し、停止シグナル (`tk::SIG_STOP`) を送ってイベントループを終了させます
     * 
     * メッセージ待ち (`State::Blocked`) や `sleep_ms()` 中であっても即座に起床して停止 (`State::Terminated`) します。
     * スロット自体はデストラクタが呼ばれるまで保持され、スコープ内であれば再度 `start()` で再開可能です。
     */
    void stop() override {
        EventHub<MsgType>::unsubscribe(this);
        Thread<StackSize>::stop();
    }

    /**
     * @brief この Actor のメッセージキューへ直接メッセージを送信します (コピー渡し)
     * 
     * スレッドセーフ・割り込みセーフ・マルチコアセーフです。
     * 
     * @param msg 送信するメッセージ
     * @return true 送信成功, false キューが満杯
     */
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
            detail::wake_task_locked(this->handle_);
        }
        detail::sched_unlock(save);
        return true;
    }

    /**
     * @brief この Actor のメッセージキューへ直接メッセージを送信します (ムーブ渡し)
     * 
     * @param msg 送信するメッセージ (ムーブ)
     * @return true 送信成功, false キューが満杯
     */
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
            detail::wake_task_locked(this->handle_);
        }
        detail::sched_unlock(save);
        return true;
    }

    /**
     * @brief tk::Message 型の場合のコンビニエンス送信関数
     * 
     * @param id メッセージID
     * @param arg 引数 (省略時: 0)
     * @return true 送信成功, false キューが満杯
     */
    template <typename U = MsgType>
    typename std::enable_if<std::is_same<U, Message>::value, bool>::type
    post(uint32_t id, uintptr_t arg = 0) {
        return post(Message{id, arg});
    }

protected:
    /**
     * @brief メッセージ受信時に呼ばれるハンドラ関数
     * 
     * キューから取り出されたメッセージの実体への参照が渡されます。
     * 派生クラスで必ず実装します。
     * 
     * @param msg 受信したメッセージ
     */
    virtual void on_message(const MsgType& msg) = 0;

    /**
     * @brief `Thread::run()` の実装 (メッセージ待ち受け＆ディスパッチループ)
     * 
     * メッセージ到着、または `send_signal()` / `stop()` によって起床します。
     */
    void run() override {
        for (;;) {
            MsgType msg{};
            uint32_t save = detail::sched_lock();
            while (count_ == 0 && !detail::has_pending_signal_or_stop_locked(this->handle_)) {
                waiting_ = true;
                detail::block_current_task_locked();
                detail::sched_unlock(save);
                // post() または send_signal() / stop() で起床
                save = detail::sched_lock();
            }
            waiting_ = false;
            if (detail::has_pending_signal_or_stop_locked(this->handle_)) {
                detail::sched_unlock(save);
                this->check_signals();
                if (this->should_stop()) {
                    break;
                }
                continue;
            }
            msg = std::move(queue_[head_]);
            head_ = (head_ + 1) % QueueSize;
            count_--;
            detail::sched_unlock(save);

            // メッセージディスパッチ（ロック解除状態で実行）
            on_message(msg);
        }
    }

private:
    MsgType queue_[QueueSize]{};
    size_t head_{0};
    size_t tail_{0};
    size_t count_{0};
    bool waiting_{false};
};

}  // namespace tk
