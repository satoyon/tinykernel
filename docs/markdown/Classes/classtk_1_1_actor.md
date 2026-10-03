---
title: tk::Actor
summary: メッセージ駆動型タスク (アクター) の基底クラス 

---

# tk::Actor



メッセージ駆動型タスク (アクター) の基底クラス  [More...](#detailed-description)


`#include <actor.hpp>`

## Public Functions

|                | Name           |
| -------------- | -------------- |
| | **[Actor](Classes/classtk_1_1_actor.md#function-actor)**() =default |
| virtual | **[~Actor](Classes/classtk_1_1_actor.md#function-~actor)**() |
| bool | **[start](Classes/classtk_1_1_actor.md#function-start)**(uint32_t prio =[PRIO_NORMAL](Namespaces/namespacetk.md#enumvalue-prio-normal))<br>[Actor](Classes/classtk_1_1_actor.md) をタスクとして生成し、EventHub へ自動登録します  |
| bool | **[post](Classes/classtk_1_1_actor.md#function-post)**(const MsgType & msg)<br>この [Actor](Classes/classtk_1_1_actor.md) のメッセージキューへ直接メッセージを送信します (コピー渡し)  |
| bool | **[post](Classes/classtk_1_1_actor.md#function-post)**(MsgType && msg)<br>この [Actor](Classes/classtk_1_1_actor.md) のメッセージキューへ直接メッセージを送信します (ムーブ渡し)  |
| template <typename U  =MsgType\> <br>std::enable_if< std::is_same< U, [Message](Classes/structtk_1_1_message.md) >::value, bool >::type | **[post](Classes/classtk_1_1_actor.md#function-post)**(uint32_t id, uintptr_t arg =0)<br>[tk::Message]() 型の場合のコンビニエンス送信関数  |
| [TaskHandle](Classes/structtk_1_1_task_handle.md) | **[handle](Classes/classtk_1_1_actor.md#function-handle)**() const |

## Protected Functions

|                | Name           |
| -------------- | -------------- |
| virtual void | **[on_start](Classes/classtk_1_1_actor.md#function-on-start)**()<br>タスク起動時、イベントループ突入直前に1度だけ呼ばれる初期化関数  |
| virtual void | **[on_message](Classes/classtk_1_1_actor.md#function-on-message)**(const MsgType & msg) =0<br>メッセージ受信時に呼ばれるハンドラ関数  |

## Detailed Description

```cpp
template <typename MsgType  =Message,
size_t QueueSize =8>
class tk::Actor;
```

メッセージ駆動型タスク (アクター) の基底クラス 

**Template Parameters**: 

  * **MsgType** 受信するメッセージの型 (デフォルト: [tk::Message](Classes/structtk_1_1_message.md)) 
  * **QueueSize** メッセージキューの最大保持件数 (デフォルト: 8)


固定長リングバッファ付きのメッセージキューを持ち、メッセージ到着時に自動起床して `[on_message()](Classes/classtk_1_1_actor.md#function-on-message)` を実行します。メッセージが無い時は CPU を消費せずブロック待機します。



```cpp
struct SensorMsg { float temp; };

class SensorActor : public tk::Actor<SensorMsg, 8> {
protected:
    void on_start() override {
        // デバイスの初期化
    }
    void on_message(const SensorMsg& msg) override {
        printf("Temp: %.1f\n", msg.temp);
    }
};
```

## Public Functions Documentation

### function Actor

```cpp
Actor() =default
```


### function ~Actor

```cpp
inline virtual ~Actor()
```


### function start

```cpp
inline bool start(
    uint32_t prio =PRIO_NORMAL
)
```

[Actor](Classes/classtk_1_1_actor.md) をタスクとして生成し、EventHub へ自動登録します 

**Parameters**: 

  * **prio** タスク優先度 (省略時: [tk::PRIO_NORMAL](Namespaces/namespacetk.md#enumvalue-prio-normal)) 


**Return**: true 登録成功, false 登録失敗 (タスク枠上限など) 

`[tk::start()](Namespaces/namespacetk.md#function-start)` を呼び出す前に登録する必要があります。


### function post

```cpp
inline bool post(
    const MsgType & msg
)
```

この [Actor](Classes/classtk_1_1_actor.md) のメッセージキューへ直接メッセージを送信します (コピー渡し) 

**Parameters**: 

  * **msg** 送信するメッセージ 


**Return**: true 送信成功, false キューが満杯 

スレッドセーフ・割り込みセーフ・マルチコアセーフです。


### function post

```cpp
inline bool post(
    MsgType && msg
)
```

この [Actor](Classes/classtk_1_1_actor.md) のメッセージキューへ直接メッセージを送信します (ムーブ渡し) 

**Parameters**: 

  * **msg** 送信するメッセージ (ムーブ) 


**Return**: true 送信成功, false キューが満杯 

### function post

```cpp
template <typename U  =MsgType>
inline std::enable_if< std::is_same< U, Message >::value, bool >::type post(
    uint32_t id,
    uintptr_t arg =0
)
```

[tk::Message]() 型の場合のコンビニエンス送信関数 

**Parameters**: 

  * **id** メッセージID 
  * **arg** 引数 (省略時: 0) 


**Return**: true 送信成功, false キューが満杯 

### function handle

```cpp
inline TaskHandle handle() const
```


この [Actor](Classes/classtk_1_1_actor.md) のタスクハンドルを取得 


## Protected Functions Documentation

### function on_start

```cpp
inline virtual void on_start()
```

タスク起動時、イベントループ突入直前に1度だけ呼ばれる初期化関数 

タスクコンテキスト内で実行されるため、`[tk::sleep_ms()](Namespaces/namespacetk.md#function-sleep-ms)` を使った初期化ウェイトも安全に行えます。 必要に応じて派生クラスでオーバーライドします。 


### function on_message

```cpp
virtual void on_message(
    const MsgType & msg
) =0
```

メッセージ受信時に呼ばれるハンドラ関数 

**Parameters**: 

  * **msg** 受信したメッセージ 


キューから取り出されたメッセージの実体への参照が渡されます。 派生クラスで必ず実装します。


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900