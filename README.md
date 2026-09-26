# Raspberry Pi Pico 2 (RP2350) 用プリエンプティブマルチタスクカーネル ～ tinykernel

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
