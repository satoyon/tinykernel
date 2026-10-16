// tinykernel: minimal preemptive task scheduler for RP2350 (Pico 2, Cortex-M33)
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "pico/runtime.h"  // PICO_RUNTIME_INIT_FUNC (static task registration)

/** 1秒あたりのタイマースケジューラtick数 (Hz) */
#ifndef TK_TICKS_PER_SEC
#define TK_TICKS_PER_SEC 1000u
#endif

/** 同一優先度タスク間のタイムスライス時間 (ミリ秒) */
#ifndef TK_TIME_SLICE_MS
#define TK_TIME_SLICE_MS 10u
#endif

/** 使用するコア数 (1 または 2)。デフォルトは 2 (SMP) */
#ifndef TK_NUM_CORES
#if defined(TK_ENABLE_SMP) && !TK_ENABLE_SMP
#define TK_NUM_CORES 1u
#else
#define TK_NUM_CORES 2u
#endif
#endif

/** 最大タスク数 (スロット0はCore0アイドル、SMP時はスロット1がCore1アイドル専用) */
#ifndef TK_MAX_TASKS
#define TK_MAX_TASKS 32u
#endif

/** 1タスクあたりのスタックサイズ (バイト) */
#ifndef TK_DEFAULT_STACK_SIZE
#define TK_DEFAULT_STACK_SIZE (4 * 1024)
#endif

namespace tk {

/** タスク関数のシグネチャ (void func(void* arg)) */
using TaskFn = void (*)(void* arg);

/**
 * @brief タスクの優先度レベル
 * 
 * 数値が小さいほど高優先度です。0..15 の範囲で指定します (15 はアイドルタスク専用)。
 */
enum Prio : uint32_t {
    PRIO_REALTIME = 1,   ///< リアルタイム最優先
    PRIO_HIGH     = 5,   ///< 高優先度
    PRIO_NORMAL   = 9,   ///< 通常優先度 (デフォルト)
    PRIO_LOW      = 13,  ///< 低優先度
};

/**
 * @brief スレッド・アクター制御用のシステム定義シグナル
 * 
 * ユーザー定義シグナルは `tk::SIG_USER` (16) 以降の任意の値 (uint32_t) を使用できます。
 * @code
 * enum MySignal : uint32_t {
 *     SIG_RELOAD = tk::SIG_USER,
 *     SIG_FLUSH  = tk::SIG_USER + 1,
 * };
 * @endcode
 */
enum Signal : uint32_t {
    SIG_NONE = 0,   ///< シグナルなし
    SIG_STOP = 1,   ///< スレッド・アクターの停止要求
    SIG_INTR = 2,   ///< スリープ・待機の中断要求 (終了はしない)
    SIG_USER = 16,  ///< ユーザー定義シグナルの開始番号 (16以降を自由に使用可能)
};

/**
 * @brief タスクの識別ハンドル
 */
struct TaskHandle {
    uint8_t slot{0xFF};  ///< タスクスロット番号 (0xFF は無効/失敗)

    /** ハンドルが有効かどうかを判定 */
    bool is_valid() const { return slot != 0xFF; }
};

/**
 * @brief タスクを生成・登録します
 * 
 * `tk::start()` の前でも、スケジューラ起動後でも動的に呼び出すことができます。
 * 
 * @param fn タスク関数ポインタ (void (*)(void* arg))
 * @param arg タスク関数に渡すユーザー引数ポインタ (省略時: nullptr)
 * @param prio タスクの優先度 (省略時: tk::PRIO_NORMAL)
 * @return TaskHandle 生成されたタスクのハンドル。登録失敗時は is_valid() が false
 */
TaskHandle create(TaskFn fn, void* arg = nullptr, uint32_t prio = PRIO_NORMAL);

/**
 * @brief tinykernel スケジューラを起動します
 * 
 * Core 0 のスケジューラを開始し、SMP有効時は Core 1 を自動ブートしてデュアルコア並行実行を開始します。
 * この関数から制御が戻ることはありません (Never returns)。
 */
void start();

/**
 * @brief 現在実行中のタスクを一時中断し、同優先度の他のタスクへ CPU を譲渡します
 * 
 * 保留中のシグナルがある場合は、復帰時にシグナルコールバック (`on_signal`) が自動実行されます。
 * 
 * @note タスクコンテキスト内から呼び出す必要があります。
 */
void yield();

/**
 * @brief 現在のタスクを指定時間スリープさせます
 * 
 * スリープ中はタスクが休止状態になり、CPU は他のタスクへ自動的に割り当てられます (CPU消費ゼロ)。
 * スリープ中に外部からシグナルが送信された場合は、即座にスリープを中断して起床し、
 * シグナルコールバック (`on_signal`) を自動実行した上で `false` を返します。
 * 
 * @param ms スリープする時間 (ミリ秒)
 * @return true 指定時間が満了して正常に起床した場合
 * @return false シグナルによってスリープが早期中断された場合
 * @note タスクコンテキスト内から呼び出す必要があります。
 */
bool sleep_ms(uint32_t ms);

/**
 * @brief 現在のタスクを終了し、停止状態 (`Terminated`) に遷移させます
 * 
 * @param code 終了コード (予約用、省略時: 0)
 * @note タスク関数の末尾に到達した場合は自動的に呼ばれます。
 */
void exit(int code = 0);

/**
 * @brief システム起動時からの経過時間をミリ秒単位で取得します
 * 
 * @return uint64_t 経過ミリ秒数 (分解能: 1 / TK_TICKS_PER_SEC 秒)
 */
uint64_t now_ms();

/**
 * @brief ハードウェアスピンロックと全割り込み禁止によるクリティカルセクションに入ります
 * 
 * @return true 呼び出し前の割り込みが有効だった場合
 * @return false 呼び出し前の割り込みが無効だった場合
 * @warning 長時間のロックは避けてください。数マイクロ秒以内のレジスタ操作等に使用します。
 */
bool lock();

/**
 * @brief tk::lock() で入ったクリティカルセクションを抜けます
 * 
 * @param was_enabled tk::lock() の戻り値
 */
void unlock(bool was_enabled);

/**
 * @brief 自コアのタスクスイッチ (プリエンプション) を一時的に禁止します
 * 
 * 割り込み (SysTick やタイマー、NVIC) は停止しないため、通信クロックや時刻更新を
 * 阻害することなく、自タスクの処理をアトミックに実行できます。ネスト (入れ子) 呼び出しに対応しています。
 * 
 * @see tk::PreemptGuard, tk::preempt_enable()
 */
void preempt_disable();

/**
 * @brief 自コアのタスクスイッチ (プリエンプション) を再開します
 * 
 * ネストカウンタが 0 に戻った時点で、保留されていたタスクスイッチが即座に実行されます。
 * 
 * @see tk::PreemptGuard, tk::preempt_disable()
 */
void preempt_enable();

/**
 * @brief プリエンプション禁止をスコープ単位で安全に管理する RAII クラス
 * 
 * 生成時に自動で `tk::preempt_disable()` を呼び、スコープを抜けた際に自動で `tk::preempt_enable()` を呼びます。
 * 
 * @code
 * {
 *     tk::PreemptGuard guard;
 *     // この区間はタスクスイッチが発生しない (I2C/SPI通信などに最適)
 *     i2c_write_blocking(...);
 * } // 自動でタスクスイッチが再開
 * @endcode
 */
class PreemptGuard {
public:
    PreemptGuard() { preempt_disable(); }
    ~PreemptGuard() { preempt_enable(); }

    PreemptGuard(const PreemptGuard&) = delete;
    PreemptGuard& operator=(const PreemptGuard&) = delete;
};

using task_mask_t = uint32_t;

namespace detail {
using SignalHandlerFn = void (*)(void* obj, uint32_t sig);

void static_register(const char* name, TaskFn fn, uint32_t prio);
uint32_t sched_lock();
void sched_unlock(uint32_t save);
void block_current_task_locked();
void wake_task_locked(TaskHandle handle);
uint8_t current_task_slot();
uint8_t find_highest_prio_waiter(task_mask_t wait_mask);

// Thread / Actor lifecycle and signal helpers
bool start_or_restart_task(TaskHandle* inout_handle, TaskFn fn, void* arg,
                           SignalHandlerFn sig_fn, uint32_t prio,
                           uint8_t* stack_base, size_t stack_size,
                           const char* name);
bool send_task_signal(TaskHandle handle, uint32_t sig);
void dispatch_pending_signal_current();
bool has_pending_signal_or_stop_locked(TaskHandle handle);
bool is_task_stop_requested(TaskHandle handle);
bool is_task_running(TaskHandle handle);
void join_task(TaskHandle handle);
void release_task_slot(TaskHandle* inout_handle);
}  // namespace detail

}  // namespace tk

/**
 * @brief リンク時にタスクを自動登録するマクロ (静的タスク登録)
 * 
 * main() の開始前に Pico SDK のランタイムイニシャライザを利用してタスクを登録します。
 * 
 * @param fn タスク関数名 (void fn(void* arg))
 * @param prio タスク優先度 (例: tk::PRIO_NORMAL)
 */
#define TK_TASK(fn, prio)                                              \
    static void tk_register_##fn(void) {                              \
        ::tk::detail::static_register(#fn, fn, (prio));               \
    }                                                                 \
    PICO_RUNTIME_INIT_FUNC(tk_register_##fn, "ZZ900");
