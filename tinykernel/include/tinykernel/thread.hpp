#pragma once

#include "kernel.hpp"
#include <cstdint>
#include <cstddef>

namespace tk {

/**
 * @brief 独自スタックとシグナル受信機構を備えたスレッド基底クラス
 * 
 * 派生クラスで `run()` をオーバーライドしてスレッドの処理を記述します。
 * シグナルを受信したい場合は `on_signal(uint32_t sig)` をオーバーライドします。
 * 
 * 主な特徴:
 * - **静的メンバスタック**: スタック (`StackSize` バイト) をインスタンス内部に保持するため `malloc` 不要・断片化ゼロ。
 * - **起動時期を問わない**: `tk::start()` の前 (静的登録) でも、スケジューラ起動後 (動的一時スレッド) でも `start()` 可能。
 * - **スリープ早期中断＋自動コールバック**: `sleep_ms()` 中に `send_signal()` を受けると即座に起床し、自動で `on_signal()` を実行。
 * - **安全なRAIIライフサイクル**: `stop()` や `run()` 完了時は一時停止 (`State::Terminated`) 状態となり、
 *   インスタンスがスコープから外れるか `delete` されてデストラクタが呼ばれた時点で初めてスロットが解放 (`State::Free`) されます。
 *   スコープ内であれば、停止したスレッドに対して再度 `start()` を呼んで再実行することも可能です。
 * - **終了クリーンアップの保証 (`on_stop`)**: 外部からの `stop()` や自然終了など、いかなる終了経路でも `on_stop()` が
 *   100% 確実に呼ばれます。終了処理（ハードウェア停止やリソース解放）は `on_signal()` ではなく必ず `on_stop()` で行います。
 * 
 * @tparam StackSize スレッドのスタックサイズ (バイト単位、8の倍数、デフォルト: TK_DEFAULT_STACK_SIZE)
 * 
 * @code
 * enum MySignals : uint32_t {
 *     SIG_PING = tk::SIG_USER,
 * };
 * 
 * class WorkerThread : public tk::Thread<2048> {
 * protected:
 *     void run() override {
 *         while (!should_stop()) {
 *             // シグナルが来たら5秒待たずに即起床し、自動で on_signal() が呼ばれてから false が返る
 *             if (sleep_ms(5000)) {
 *                 // 5秒経過時の定期処理
 *             }
 *         }
 *     }
 *     void on_signal(uint32_t sig) override {
 *         if (sig == SIG_PING) {
 *             // シグナル受信時の処理
 *         }
 *     }
 * };
 * @endcode
 */
template <size_t StackSize = TK_DEFAULT_STACK_SIZE>
class Thread {
    static_assert(StackSize >= 256, "StackSize must be at least 256 bytes");
    static_assert((StackSize % 8) == 0, "StackSize must be a multiple of 8 bytes");

public:
    Thread() = default;

    /**
     * @brief デストラクタ
     * 
     * スレッドが実行中の場合は停止シグナル (`SIG_STOP`) を送って完了 (`join`) を待ち、
     * CPU から完全に降りたことを確認してからカーネルのタスクスロットを解放 (`State::Free`) します。
     */
    virtual ~Thread() {
        release_slot();
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /**
     * @brief スレッドを起動 (または停止状態から再起動) します
     * 
     * `tk::start()` の前でも後でも呼び出すことができます。
     * 一度終了 (`Terminated`) したインスタンスに対して再度呼び出すと、同じスロットで最初から再実行されます。
     * 
     * @param prio タスク優先度 (省略時: tk::PRIO_NORMAL)
     * @return true 起動成功, false 起動失敗 (すでに実行中、または空きスロットなし)
     */
    virtual bool start(uint32_t prio = PRIO_NORMAL) {
        return detail::start_or_restart_task(
            &handle_,
            &Thread::task_entry,
            this,
            &Thread::signal_trampoline,
            prio,
            stack_,
            StackSize,
            "thr"
        );
    }

    /**
     * @brief このスレッドへシグナルを送信します
     * 
     * スレッドが `sleep_ms()` やメッセージ待機で休止中の場合、即座に休止を打ち切って起床させ、
     * スレッド自身のコンテキストで `on_signal(sig)` を自動実行します。
     * 
     * @param sig 送信するシグナル (`tk::SIG_STOP`, `tk::SIG_INTR`, または `tk::SIG_USER` 以降のユーザー定義値)
     * @return true 送信成功, false スレッドが未起動または既に停止済み
     */
    bool send_signal(uint32_t sig) {
        return detail::send_task_signal(handle_, sig);
    }

    /**
     * @brief スレッドに停止要求 (`tk::SIG_STOP`) を送ります
     * 
     * スリープやブロック待機を即座に解除し、`should_stop()` を `true` にします。
     * スレッドが終了してもスロットは `State::Terminated` として保持され、デストラクタが呼ばれるまで他タスクに横取りされません。
     */
    virtual void stop() {
        detail::send_task_signal(handle_, SIG_STOP);
    }

    /**
     * @brief スレッドの処理が完了して停止 (`State::Terminated`) するまで待機します
     */
    void join() {
        detail::join_task(handle_);
    }

    /**
     * @brief スレッドが現在稼働中かどうかを判定します
     * @return true 稼働中 (`Ready`, `Running`, `Sleeping`, `Blocked`)
     * @return false 未起動または停止済み (`Terminated`, `Free`)
     */
    bool is_running() const {
        return detail::is_task_running(handle_);
    }

    /**
     * @brief 停止要求 (`stop()` または `SIG_STOP`) が出されているかどうかを判定します
     * 
     * `run()` のループ条件 (`while (!should_stop())`) で使用します。
     */
    bool should_stop() const {
        return detail::is_task_stop_requested(handle_);
    }

    /** `should_stop()` の別名エイリアス */
    bool is_interrupted() const {
        return should_stop();
    }

    /** このスレッドのタスクハンドルを取得 */
    TaskHandle handle() const { return handle_; }

protected:
    /**
     * @brief スレッド開始時、`run()` の直前に1度だけ呼ばれる初期化フック
     */
    virtual void on_start() {}

    /**
     * @brief スレッドのメイン処理 (派生クラスで実装)
     */
    virtual void run() = 0;

    /**
     * @brief シグナル受信時に自動実行されるコールバック関数
     * 
     * `sleep_ms()` の早期中断時や `yield()`、`check_signals()` 呼び出し時に
     * このスレッド自身のコンテキストで実行されます。走行中の非同期イベント処理に使用します。
     * 
     * @param sig 受信したシグナル値
     * 
     * @warning **終了処理には使用しないでください**:
     * スレッド停止時 (`stop()` / `SIG_STOP`)、スレッドが `while (!should_stop())` の条件判定で
     * 即座にループを抜けて終了した場合など、`on_signal(SIG_STOP)` は呼ばれないことがあります。
     * また、`run()` が自然に終了した場合もシグナルは送られません。
     * **リソース解放や終了時の後始末は、必ず 100% 確実に呼び出される `on_stop()` で行ってください。**
     */
    virtual void on_signal(uint32_t sig) { (void)sig; }

    /**
     * @brief スレッド終了時にタスクコンテキストで1度だけ確実に呼ばれるクリーンアップフック
     * 
     * 外部からの `stop()` による停止であっても、`run()` の自然な正常終了であっても、
     * **どのような終了経路であってもタスク終了直前に 100% 確実に呼び出されます**。
     * ハードウェアの停止、ファイルのフラッシュ、バッファ解放などの終了処理は必ずこの関数をオーバーライドして記述してください。
     */
    virtual void on_stop() {}

    /**
     * @brief 指定ミリ秒スリープします (シグナルによる早期中断＆自動コールバック付き)
     * 
     * @param ms スリープ時間 (ミリ秒)
     * @return true 指定時間が満了して起床した
     * @return false シグナルを受信して早期起床した (`on_signal` 実行済み)
     */
    bool sleep_ms(uint32_t ms) {
        return tk::sleep_ms(ms);
    }

    /**
     * @brief 同優先度の他タスクへ CPU を譲渡し、保留中のシグナルがあれば `on_signal()` を実行します
     */
    void yield() {
        tk::yield();
    }

    /**
     * @brief スリープを挟まない計算ループ中に、保留中のシグナルがあれば `on_signal()` を実行します
     */
    void check_signals() {
        detail::dispatch_pending_signal_current();
    }

    /**
     * @brief 派生クラスのデストラクタから安全にスレッド停止・スロット解放を行うヘルパー
     */
    void release_slot() {
        if (handle_.is_valid()) {
            detail::release_task_slot(&handle_);
        }
    }

    TaskHandle handle_{};

private:
    static void task_entry(void* arg) {
        auto* self = static_cast<Thread<StackSize>*>(arg);
        self->on_start();
        if (!self->should_stop()) {
            self->run();
        }
        self->on_stop();
        tk::exit(0);
    }

    static void signal_trampoline(void* obj, uint32_t sig) {
        auto* self = static_cast<Thread<StackSize>*>(obj);
        self->on_signal(sig);
    }

    alignas(8) uint8_t stack_[StackSize]{};
};

}  // namespace tk

