---
title: tk::EventHub
summary: メッセージ型ごとの超軽量 Pub/Sub ディスパッチャ 

---

# tk::EventHub



メッセージ型ごとの超軽量 Pub/Sub ディスパッチャ  [More...](#detailed-description)


`#include <actor.hpp>`

## Public Functions

|                | Name           |
| -------------- | -------------- |
| bool | **[subscribe](Classes/classtk_1_1_event_hub.md#function-subscribe)**(void * actor, bool(*)(void *, const MsgType &) post_fn)<br>[Actor](Classes/classtk_1_1_actor.md) をこのメッセージ型の購読者として登録します  |
| bool | **[unsubscribe](Classes/classtk_1_1_event_hub.md#function-unsubscribe)**(void * actor)<br>[Actor](Classes/classtk_1_1_actor.md) の購読登録を解除します  |
| size_t | **[publish](Classes/classtk_1_1_event_hub.md#function-publish)**(const MsgType & msg)<br>このメッセージ型を購読しているすべての [Actor](Classes/classtk_1_1_actor.md) へ同報配信します  |
| size_t | **[subscriber_count](Classes/classtk_1_1_event_hub.md#function-subscriber-count)**() |

## Detailed Description

```cpp
template <typename MsgType ,
size_t MaxSubscribers =4>
class tk::EventHub;
```

メッセージ型ごとの超軽量 Pub/Sub ディスパッチャ 

**Template Parameters**: 

  * **MsgType** 配信するメッセージの型 
  * **MaxSubscribers** 同一メッセージ型を購読できる最大 [Actor](Classes/classtk_1_1_actor.md) 数 (デフォルト: 4) 


テンプレート引数 `MsgType` ごとにコンパイル時解決される静的レジストリです。 実行時の型検索オーバーヘッドゼロ、動的メモリ確保ゼロで動作します。

## Public Functions Documentation

### function subscribe

```cpp
static inline bool subscribe(
    void * actor,
    bool(*)(void *, const MsgType &) post_fn
)
```

[Actor](Classes/classtk_1_1_actor.md) をこのメッセージ型の購読者として登録します 

**Parameters**: 

  * **actor** [Actor](Classes/classtk_1_1_actor.md) インスタンスポインタ 
  * **post_fn** メッセージ投入関数ポインタ 


**Return**: true 登録成功, false 購読者上限に達した 

### function unsubscribe

```cpp
static inline bool unsubscribe(
    void * actor
)
```

[Actor](Classes/classtk_1_1_actor.md) の購読登録を解除します 

**Parameters**: 

  * **actor** 解除する [Actor](Classes/classtk_1_1_actor.md) インスタンスポインタ 


**Return**: true 解除成功, false 未登録 

### function publish

```cpp
static inline size_t publish(
    const MsgType & msg
)
```

このメッセージ型を購読しているすべての [Actor](Classes/classtk_1_1_actor.md) へ同報配信します 

**Parameters**: 

  * **msg** 配信するメッセージ実体 (各 [Actor](Classes/classtk_1_1_actor.md) のキューへ値コピー) 


**Return**: size_t 配信に成功した [Actor](Classes/classtk_1_1_actor.md) の数 

### function subscriber_count

```cpp
static inline size_t subscriber_count()
```


現在の購読者数を取得 


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900