# Raspberry Pi Pico 2 (RP2350) 用プリエンプティブマルチタスクカーネル ～ tinykernel

(English version follows the Japanese section)

Raspberry Pi Pico 2 (RP2350 / ARM Cortex-M33) 向けの、シンプルで実用的なリアルタイム・プリエンプティブマルチタスクカーネルです。

---

## 主な特徴

- **デュアルコア（SMP）対応**: RP2350 の 2 つの Cortex-M33 コアをフル活用し、タスクを自動で並行実行・マイグレーションします。
- **完全疎結合型のタスク間通信 (Pub/Sub)**: 受信側の Actor インスタンスを知らなくても、メッセージ型を指定するだけで安全に配信できるメッセージングシステムを装備。
- **値コピーによるメモリ安全性**: メッセージはキュー内に実体としてコピーされるため、スタック上のローカル変数でもライフタイム（ダングリングポインタ）の心配なく受け渡し可能。
- **CPU 消費ゼロのブロッキング待機**: メッセージ待ちやセマフォ/ミューテックス待ちの間はタスクが自動休止 (`State::Blocked`) し、CPU を他のタスクに明け渡します。
- **ハードウェア FPU の完全保護**: 複数コア・複数タスクで浮動小数点演算を同時実行しても、コンテキストスイッチによる FPU レジスタ破壊が起きません。
- **ゼロ動的確保 (No malloc)**: スタック、タスクスロット、メッセージキューに至るまで静的／固定長メモリで動作し、メモリ断片化を起こしません。

---

## 基本的な使い方

`tk::create()` を使ってタスクを登録します（最大 30 個のユーザータスクを登録可能）。

```cpp
#include "tinykernel/kernel.hpp"

static void your_task(void *arg) {
    for (;;) {
        // なにか仕事をする
        tk::sleep_ms(200); // スリープでCPUを譲渡
    }
}

int main(void) {
    stdio_init_all();

    // your_task を登録（タスク関数ポインタ、引数、優先度）
    tk::create(your_task, nullptr, tk::PRIO_NORMAL);

    // スケジューラ起動（Core 1 も自動ブート、これ以降は戻らない）
    tk::start();

    for (;;) {}
}
```

---

## 疎結合型タスク間通信 (Actor & EventHub)

メッセージを受け取る側は `tk::Actor<MsgType, QueueSize>` を継承します。

```cpp
#include "tinykernel/actor.hpp"

// 1. Actor が受け取るメッセージの構造体を定義
struct YourActionMessage {
    uint32_t msg;
    char str[16];
};

// 2. メッセージを待ち受ける Actor を作成
class YourActor : public tk::Actor<YourActionMessage, 8> {
protected:
    void on_start() override {
        // タスク起動時に1回だけ呼ばれる（GPIOやデバイスの初期化など）
    }

    void on_message(const YourActionMessage& msg) override {
        // メッセージを受信すると自動起床して実行される
        // msg はキューに値コピーされているため安全
    }
};

YourActor g_actor;

int main(void) {
    stdio_init_all();

    // タスクとして登録・起動
    g_actor.start(tk::PRIO_NORMAL);

    tk::start();
}
```

メッセージを送る側は `tk::publish()` を呼びます。

```cpp
static void your_sender(void *arg) {
    for (;;) {
        YourActionMessage msg{1, "Hello!"};

        // YourActionMessage を受け付けるすべての Actor に配信（1対多の同報可能）
        tk::publish(msg);

        tk::sleep_ms(1000);
    } // msg のスコープが外れてスタックが消えても、受信側は壊れない！
}

int main(void) {
    stdio_init_all();

    g_actor.start(tk::PRIO_NORMAL);
    tk::create(your_sender, nullptr, tk::PRIO_NORMAL);

    tk::start();
}
```

> **Point**: 送信側は Actor のインスタンスや名前を知る必要がありません。メッセージの型情報だけで `EventHub` が適切な Actor へ直接届けてくれます。

---

## 動的スレッドとシグナル通知 (`thread.hpp`)

`tk::Thread<StackSize>` を継承することで、**自身のメンバとしてスタックを内包し、シグナルによるスリープ早期中断と自動コールバックを備えたスレッド**を定義できます（`tk::Actor` もこの `tk::Thread` を継承しています）。

- **`tk::start()` の前でも後（走行中）でも起動可能**: 一時的なワーカーを動的に起動できます。
- **ユーザー定義シグナル**: `tk::SIG_STOP` や `tk::SIG_INTR` のシステムシグナルに加え、`tk::SIG_USER` (16) 以降の任意の値で独自シグナルを定義できます。
- **安全な RAII ライフサイクル**: `stop()` や `run()` 完了時は一時停止（`State::Terminated`）状態でスロットを保持し、**スコープを抜けるか `delete` でデストラクタが呼ばれた時点で初めてスロットが解放（`State::Free`）** されます。

```cpp
#include "tinykernel/thread.hpp"

// ユーザー定義シグナル (tk::SIG_USER 以降を使用)
enum AppSignal : uint32_t {
    SIG_WAKE_NOW = tk::SIG_USER,
    SIG_DUMP     = tk::SIG_USER + 1,
};

class MyWorker : public tk::Thread<1024> { // 1KB スタックを内包
protected:
    void run() override {
        while (!should_stop()) {
            // 5秒スリープ。途中でシグナルが来ると即座に起床して on_signal() が自動実行され、false が返る
            if (sleep_ms(5000)) {
                // 5秒満了時の処理
            }
        }
    }

    void on_signal(uint32_t sig) override {
        if (sig == SIG_WAKE_NOW) {
            // スリープを中断して即座に実行される処理
        }
    }
};

// 他タスク内での動的な生成・シグナル送信・破棄の例
void manager_task(void*) {
    {
        MyWorker worker;
        worker.start(tk::PRIO_NORMAL);       // 動的に起動

        tk::sleep_ms(500);
        worker.send_signal(SIG_WAKE_NOW);    // 5秒スリープを即座に中断して on_signal() を発火！

        worker.stop();                       // SIG_STOP 送信 -> 一時停止 (Terminated)
        worker.join();                       // 停止完了を待機
    } // スコープを抜けて ~MyWorker() が呼ばれ、タスクスロットが安全に解放 (Free) される
}
```

---

## セマフォ / ミューテックス (`sync.hpp`)

スレッド間の排他制御・同期プリミティブとして以下が用意されています。

```cpp
#include "tinykernel/sync.hpp"

tk::Mutex g_mutex;
tk::BinarySemaphore g_sem(0, 1);

// RAII による安全なロック（std::lock_guard 互換）
void safe_function() {
    tk::LockGuard<tk::Mutex> lock(g_mutex);
    // クリティカルセクション...
} // スコープを抜けると自動アンロック
```

待機タスクの中で**一番優先度（Priority）が高いタスクから順に起床**します。

---

## ペリフェラル通信の保護 (`tk::PreemptGuard`)

I2C や SPI など、途中でタスク切り替えを起こしたくないペリフェラルトランザクションを行う場合、**割り込み（時計やタイマー）を止めずに自コアのタスクスイッチだけを安全に一時抑止**できます。

```cpp
{
    tk::PreemptGuard guard;

    // 割り込み（SysTick やタイマー）は通常通り動き続け、他コアも影響を受けない。
    // しかし、自コアのタスクスイッチだけは確実にブロックされる！
    i2c_write_blocking(i2c0, addr, data, len, false);

} // スコープを抜けると自動でタスクスイッチが再開（保留中のPendSVが即発火）
```

---

## プロジェクトへの導入方法

独自の Pico プロジェクトの `CMakeLists.txt` で `tinykernel` ディレクトリを追加し、リンクするだけで即座に利用できます。

```cmake
add_subdirectory(tinykernel)

target_link_libraries(your_project_name
    pico_stdlib
    tinykernel_lib
)
```

### ビルドオプション（シングルコア構成の選択）

tinykernel はデフォルトで RP2350 のデュアルコア SMP（対称型マルチプロセッシング）構成で動作します。
もし Core 1 を独自の極めてビジーな処理（ベアメタル制御、専用の超高速ループなど）に専有し、tinykernel を Core 0 のみで動かしたい場合は、`TK_ENABLE_SMP` オプションを `OFF` に設定できます。

- **CMake オプション設定**:
  ```cmake
  # add_subdirectory(tinykernel) の前に設定する場合
  set(TK_ENABLE_SMP OFF CACHE BOOL "Enable SMP dual-core support" FORCE)
  add_subdirectory(tinykernel)
  ```
  またはコマンドラインから設定:
  ```bash
  cmake -B build -DTK_ENABLE_SMP=OFF
  ```

- **シングルコア構成時の動作**:
  - `multicore_launch_core1()` が実行されず、Core 1 は tinykernel から完全に解放されます（ユーザーコードから自由に `multicore_launch_core1(...)` などを利用可能）。
  - Core 1 用アイドルタスクが生成されないため、利用可能なユーザータスク数が 30 個から 31 個に拡張されます。

---

## English Description

A simple and practical real-time preemptive multitasking kernel for the Raspberry Pi Pico 2 (RP2350 / ARM Cortex-M33).

## Key Features

- SMP Support (Dual Core): Fully utilizes both Cortex-M33 cores of the RP2350 to automatically execute tasks in parallel and migrate them between cores.
- Loosely Coupled Inter-Task Communication (Pub/Sub): Equipped with a messaging system that allows safe delivery by specifying only the message type, without needing to know the receiver's Actor instance.
- Memory Safety via Pass-by-Value: Messages are copied into the queue by value. This eliminates concerns about lifetimes or dangling pointers, even when passing local variables from the stack.
- Zero-CPU Blocking Wait: Tasks automatically enter a blocked state (State::Blocked) while waiting for messages, semaphores, or mutexes, yielding CPU time to other tasks.
- Full Hardware FPU Protection: Prevents FPU register corruption during context switches, even when multiple cores and tasks perform floating-point operations simultaneously.
- Zero Dynamic Allocation (No malloc): All stacks, task slots, and message queues use static or fixed-length memory to prevent memory fragmentation.

## Basic Usage

Register tasks using `tk::create()` (up to 30 user tasks can be registered).

```cpp
#include "tinykernel/kernel.hpp"

static void your_task(void *arg) {
    for (;;) {
        // Do some work here
        tk::sleep_ms(200); // Yield CPU by sleeping
    }
}

int main(void) {
    stdio_init_all();

    // Register your_task (Task function pointer, argument, priority)
    tk::create(your_task, nullptr, tk::PRIO_NORMAL);

    // Start the scheduler (Core 1 is booted automatically; execution does not return from here)
    tk::start();

    for (;;) {}
}
```

## Loosely Coupled Communication (Actor & EventHub)

To receive messages, inherit from `tk::Actor<MsgType, QueueSize>`.

```cpp
#include "tinykernel/actor.hpp"

// 1. Define the message structure to be received by the Actor
struct YourActionMessage {
    uint32_t msg;
    char str[16];
};

// 2. Create an Actor that waits for messages
class YourActor : public tk::Actor<YourActionMessage, 8> {
protected:
    void on_start() override {
        // Called once when the task starts (e.g., GPIO or device initialization)
    }

    void on_message(const YourActionMessage& msg) override {
        // Automatically woken up and executed upon receiving a message
        // Safe because 'msg' is copied by value into the queue
    }
};

YourActor g_actor;

int main(void) {
    stdio_init_all();

    // Register and start as a task
    g_actor.start(tk::PRIO_NORMAL);

    tk::start();
}
```

To send messages, call `tk::publish()`.

```cpp
static void your_sender(void *arg) {
    for (;;) {
        YourActionMessage msg{1, "Hello!"};

        // Broadcast to all Actors that accept YourActionMessage (one-to-many)
        tk::publish(msg);

        tk::sleep_ms(1000);
    } // Even after 'msg' goes out of scope and its stack is reclaimed, the receiver remains safe!
}

int main(void) {
    stdio_init_all();

    g_actor.start(tk::PRIO_NORMAL);
    tk::create(your_sender, nullptr, tk::PRIO_NORMAL);

    tk::start();
}
```

> **Point**: The sender does not need to know the instance or name of the Actor. The EventHub delivers messages directly to the appropriate Actors based solely on the message type.

## Dynamic Threads and Signals (`thread.hpp`)

By inheriting from `tk::Thread<StackSize>`, you can define threads with an embedded member stack, early sleep interruption via signals, and automatic `on_signal()` callbacks (`tk::Actor` also inherits from `tk::Thread`).

- **Start anytime**: Can be started both before `tk::start()` (static) and dynamically after the scheduler is running.
- **User-defined signals**: In addition to system signals (`tk::SIG_STOP`, `tk::SIG_INTR`), custom signals can be defined starting from `tk::SIG_USER` (16).
- **Safe RAII lifecycle**: When stopped via `stop()` or when `run()` returns, the thread halts in `State::Terminated` while retaining its slot. The slot is safely reclaimed (`State::Free`) only when the instance goes out of scope or is deleted (`~Thread()`).

```cpp
#include "tinykernel/thread.hpp"

enum AppSignal : uint32_t {
    SIG_WAKE_NOW = tk::SIG_USER,
};

class MyWorker : public tk::Thread<1024> {
protected:
    void run() override {
        while (!should_stop()) {
            // Sleeps for 5s; if a signal arrives, wakes immediately, runs on_signal(), and returns false
            if (sleep_ms(5000)) {
                // Periodic work after full 5s sleep
            }
        }
    }

    void on_signal(uint32_t sig) override {
        if (sig == SIG_WAKE_NOW) {
            // Handled immediately upon breaking sleep
        }
    }
};
```

## Semaphores / Mutexes (`sync.hpp`)

The following primitives are provided for mutual exclusion and synchronization between threads:

```cpp
#include "tinykernel/sync.hpp"

tk::Mutex g_mutex;
tk::BinarySemaphore g_sem(0, 1);

// Safe locking using RAII (compatible with std::lock_guard)
void safe_function() {
    tk::LockGuard<tk::Mutex> lock(g_mutex);
    // Critical section...
} // Automatically unlocked when the scope ends
```

Waiting tasks are woken up in order of highest priority.

## Peripheral Communication Protection (`tk::PreemptGuard`)

When performing peripheral transactions (such as I2C or SPI) where you want to prevent task switching, `tk::PreemptGuard` allows you to temporarily suppress task switches on the current core without disabling interrupts (like system clocks or timers).

```cpp
{
    tk::PreemptGuard guard;

    // Interrupts (SysTick, Timers) continue to run normally, and other cores are unaffected.
    // However, task switching on this specific core is guaranteed to be blocked!
    i2c_write_blocking(i2c0, addr, data, len, false);

} // Task switching resumes automatically when the scope ends (pending PendSV is triggered immediately)
```

## Integration

To integrate `tinykernel` into your Pico project, simply add the directory to your `CMakeLists.txt` and link it:

```cmake
add_subdirectory(tinykernel)

target_link_libraries(your_project_name
    pico_stdlib
    tinykernel_lib
)
```

### Build Option (Single Core Configuration)

By default, `tinykernel` runs in SMP dual-core mode on RP2350.
If you want to dedicate Core 1 to bare-metal polling / high-frequency routines and restrict `tinykernel` to Core 0 only, disable `TK_ENABLE_SMP`:

```cmake
# When embedding via add_subdirectory
set(TK_ENABLE_SMP OFF CACHE BOOL "Enable SMP dual-core support" FORCE)
add_subdirectory(tinykernel)
```
or via CLI:
```bash
cmake -B build -DTK_ENABLE_SMP=OFF
```

When SMP is disabled:
- `multicore_launch_core1()` is not called; Core 1 is untouched and free for custom user control.
- Max user tasks increase from 30 to 31 (since the Core 1 idle task is omitted).
