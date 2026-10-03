---
title: tk

---

# tk



## Namespaces

| Name           |
| -------------- |
| **[tk::detail](Namespaces/namespacetk_1_1detail.md)**  |

## Classes

|                | Name           |
| -------------- | -------------- |
| class | **[tk::Actor](Classes/classtk_1_1_actor.md)** <br>メッセージ駆動型タスク (アクター) の基底クラス  |
| class | **[tk::EventHub](Classes/classtk_1_1_event_hub.md)** <br>メッセージ型ごとの超軽量 Pub/Sub ディスパッチャ  |
| class | **[tk::LockGuard](Classes/classtk_1_1_lock_guard.md)** <br>ミューテックスのロックをスコープ単位で安全に管理する RAII クラス (std::lock_guard 相当)  |
| struct | **[tk::Message](Classes/structtk_1_1_message.md)** <br>デフォルトの軽量メッセージ構造体  |
| class | **[tk::Mutex](Classes/classtk_1_1_mutex.md)** <br>所有権・優先度順ウェイクアップ・RAII 対応のミューテックス (排他ロック)  |
| class | **[tk::PreemptGuard](Classes/classtk_1_1_preempt_guard.md)** <br>プリエンプション禁止をスコープ単位で安全に管理する RAII クラス  |
| class | **[tk::Semaphore](Classes/classtk_1_1_semaphore.md)** <br>計数セマフォ (Counting [Semaphore]()) / バイナリセマフォ  |
| struct | **[tk::TaskHandle](Classes/structtk_1_1_task_handle.md)** <br>タスクの識別ハンドル  |

## Types

|                | Name           |
| -------------- | -------------- |
| enum uint32_t | **[Prio](Namespaces/namespacetk.md#enum-prio)** { PRIO_REALTIME = 1, PRIO_HIGH = 5, PRIO_NORMAL = 9, PRIO_LOW = 13}<br>タスクの優先度レベル  |
| using void(*)(void *arg) | **[TaskFn](Namespaces/namespacetk.md#using-taskfn)**  |
| using uint32_t | **[task_mask_t](Namespaces/namespacetk.md#using-task-mask-t)**  |
| using [Semaphore](Classes/classtk_1_1_semaphore.md) | **[BinarySemaphore](Namespaces/namespacetk.md#using-binarysemaphore)** <br>バイナリセマフォ (初期値1, 最大1) の型エイリアス  |

## Functions

|                | Name           |
| -------------- | -------------- |
| template <typename MsgType \> <br>size_t | **[publish](Namespaces/namespacetk.md#function-publish)**(const MsgType & msg)<br>メッセージの型を指定して、購読しているすべての [Actor]() へ同報配信します  |
| size_t | **[publish](Namespaces/namespacetk.md#function-publish)**(uint32_t id, uintptr_t arg =0)<br>[tk::Message]() 型の同報配信ショートカット  |
| [TaskHandle](Classes/structtk_1_1_task_handle.md) | **[create](Namespaces/namespacetk.md#function-create)**([TaskFn](Namespaces/namespacetk.md#using-taskfn) fn, void * arg =nullptr, uint32_t prio =[PRIO_NORMAL](Namespaces/namespacetk.md#enumvalue-prio-normal))<br>タスクを生成・登録します  |
| void | **[start](Namespaces/namespacetk.md#function-start)**()<br>tinykernel スケジューラを起動します  |
| void | **[yield](Namespaces/namespacetk.md#function-yield)**()<br>現在実行中のタスクを一時中断し、同優先度の他のタスクへ CPU を譲渡します  |
| void | **[sleep_ms](Namespaces/namespacetk.md#function-sleep-ms)**(uint32_t ms)<br>現在のタスクを指定時間スリープさせます  |
| void | **[exit](Namespaces/namespacetk.md#function-exit)**(int code =0)<br>現在のタスクを終了・破棄します  |
| uint64_t | **[now_ms](Namespaces/namespacetk.md#function-now-ms)**()<br>システム起動時からの経過時間をミリ秒単位で取得します  |
| bool | **[lock](Namespaces/namespacetk.md#function-lock)**()<br>ハードウェアスピンロックと全割り込み禁止によるクリティカルセクションに入ります  |
| void | **[unlock](Namespaces/namespacetk.md#function-unlock)**(bool was_enabled)<br>[tk::lock()](Namespaces/namespacetk.md#function-lock) で入ったクリティカルセクションを抜けます  |
| void | **[preempt_disable](Namespaces/namespacetk.md#function-preempt-disable)**()<br>自コアのタスクスイッチ (プリエンプション) を一時的に禁止します  |
| void | **[preempt_enable](Namespaces/namespacetk.md#function-preempt-enable)**()<br>自コアのタスクスイッチ (プリエンプション) を再開します  |

## Types Documentation

### enum Prio

| Enumerator | Value | Description |
| ---------- | ----- | ----------- |
| PRIO_REALTIME | 1| リアルタイム最優先   |
| PRIO_HIGH | 5| 高優先度   |
| PRIO_NORMAL | 9| 通常優先度 (デフォルト)   |
| PRIO_LOW | 13| 低優先度   |



タスクの優先度レベル 

数値が小さいほど高優先度です。0..15 の範囲で指定します (15 はアイドルタスク専用)。 


### using TaskFn

```cpp
using tk::TaskFn = void (*)(void* arg);
```


タスク関数のシグネチャ (void func(void* arg)) 


### using task_mask_t

```cpp
using tk::task_mask_t = uint32_t;
```


### using BinarySemaphore

```cpp
using tk::BinarySemaphore = Semaphore;
```

バイナリセマフォ (初期値1, 最大1) の型エイリアス 


## Functions Documentation

### function publish

```cpp
template <typename MsgType >
inline size_t publish(
    const MsgType & msg
)
```

メッセージの型を指定して、購読しているすべての [Actor]() へ同報配信します 

**Parameters**: 

  * **msg** 送信するメッセージ 


**Template Parameters**: 

  * **MsgType** 配信するメッセージ型 


**Return**: size_t 配信に成功した [Actor](Classes/classtk_1_1_actor.md) の数

送信側は [Actor](Classes/classtk_1_1_actor.md) のインスタンスや名前を知る必要がありません (完全疎結合)。 メッセージの実体は各 [Actor](Classes/classtk_1_1_actor.md) のキューへ値コピーされるため、スタック上のローカル変数でも安全に渡せます。



```cpp
MyEvent ev{1, "Button Pressed"};
tk::publish(ev); // MyEvent を受け付けるすべての Actor に届く
```


### function publish

```cpp
inline size_t publish(
    uint32_t id,
    uintptr_t arg =0
)
```

[tk::Message]() 型の同報配信ショートカット 

**Parameters**: 

  * **id** メッセージID 
  * **arg** 引数 (省略時: 0) 


**Return**: size_t 配信に成功した [Actor](Classes/classtk_1_1_actor.md) の数 

### function create

```cpp
TaskHandle create(
    TaskFn fn,
    void * arg =nullptr,
    uint32_t prio =PRIO_NORMAL
)
```

タスクを生成・登録します 

**Parameters**: 

  * **fn** タスク関数ポインタ (void (*)(void* arg)) 
  * **arg** タスク関数に渡すユーザー引数ポインタ (省略時: nullptr) 
  * **prio** タスクの優先度 (省略時: [tk::PRIO_NORMAL](Namespaces/namespacetk.md#enumvalue-prio-normal)) 


**Return**: [TaskHandle](Classes/structtk_1_1_task_handle.md) 生成されたタスクのハンドル。登録失敗時は is_valid() が false 

[tk::start()](Namespaces/namespacetk.md#function-start) を呼び出す前に登録する必要があります。


### function start

```cpp
void start()
```

tinykernel スケジューラを起動します 

Core 0 のスケジューラを開始し、Core 1 を自動ブートしてデュアルコア並行実行を開始します。 この関数から制御が戻ることはありません (Never returns)。 


### function yield

```cpp
void yield()
```

現在実行中のタスクを一時中断し、同優先度の他のタスクへ CPU を譲渡します 

**Note**: タスクコンテキスト内から呼び出す必要があります。 

### function sleep_ms

```cpp
void sleep_ms(
    uint32_t ms
)
```

現在のタスクを指定時間スリープさせます 

**Parameters**: 

  * **ms** スリープする時間 (ミリ秒) 


**Note**: タスクコンテキスト内から呼び出す必要があります。 

スリープ中はタスクが休止状態になり、CPU は他のタスクへ自動的に割り当てられます (CPU消費ゼロ)。


### function exit

```cpp
void exit(
    int code =0
)
```

現在のタスクを終了・破棄します 

**Parameters**: 

  * **code** 終了コード (予約用、省略時: 0) 


**Note**: タスク関数の末尾に到達した場合は自動的に呼ばれます。 

### function now_ms

```cpp
uint64_t now_ms()
```

システム起動時からの経過時間をミリ秒単位で取得します 

**Return**: uint64_t 経過ミリ秒数 (分解能: 1 / TK_TICKS_PER_SEC 秒) 

### function lock

```cpp
bool lock()
```

ハードウェアスピンロックと全割り込み禁止によるクリティカルセクションに入ります 

**Return**: 

  * true 呼び出し前の割り込みが有効だった場合 
  * false 呼び出し前の割り込みが無効だった場合 


**Warning**: 長時間のロックは避けてください。数マイクロ秒以内のレジスタ操作等に使用します。 

### function unlock

```cpp
void unlock(
    bool was_enabled
)
```

[tk::lock()](Namespaces/namespacetk.md#function-lock) で入ったクリティカルセクションを抜けます 

**Parameters**: 

  * **was_enabled** [tk::lock()](Namespaces/namespacetk.md#function-lock) の戻り値 


### function preempt_disable

```cpp
void preempt_disable()
```

自コアのタスクスイッチ (プリエンプション) を一時的に禁止します 

**See**: [tk::PreemptGuard](Classes/classtk_1_1_preempt_guard.md), [tk::preempt_enable()](Namespaces/namespacetk.md#function-preempt-enable)

割り込み (SysTick やタイマー、NVIC) は停止しないため、通信クロックや時刻更新を 阻害することなく、自タスクの処理をアトミックに実行できます。ネスト (入れ子) 呼び出しに対応しています。


### function preempt_enable

```cpp
void preempt_enable()
```

自コアのタスクスイッチ (プリエンプション) を再開します 

**See**: [tk::PreemptGuard](Classes/classtk_1_1_preempt_guard.md), [tk::preempt_disable()](Namespaces/namespacetk.md#function-preempt-disable)

ネストカウンタが 0 に戻った時点で、保留されていたタスクスイッチが即座に実行されます。






-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900