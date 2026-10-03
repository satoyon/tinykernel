---
title: tk::Semaphore
summary: 計数セマフォ (Counting Semaphore) / バイナリセマフォ 

---

# tk::Semaphore



計数セマフォ (Counting [Semaphore]()) / バイナリセマフォ  [More...](#detailed-description)


`#include <sync.hpp>`

## Public Functions

|                | Name           |
| -------------- | -------------- |
| | **[Semaphore](Classes/classtk_1_1_semaphore.md#function-semaphore)**(int32_t initial_count =1, int32_t max_count =1)<br>セマフォを初期化します  |
| void | **[acquire](Classes/classtk_1_1_semaphore.md#function-acquire)**()<br>資源を獲得するまで現在のタスクをブロックします  |
| bool | **[try_acquire](Classes/classtk_1_1_semaphore.md#function-try-acquire)**()<br>資源の獲得を試みます (ノンブロッキング)  |
| void | **[release](Classes/classtk_1_1_semaphore.md#function-release)**()<br>資源を返却し、待機中タスクがあれば最優先度のタスクを 1 つ起床させます  |
| int32_t | **[count](Classes/classtk_1_1_semaphore.md#function-count)**() const |

## Detailed Description

```cpp
class tk::Semaphore;
```

計数セマフォ (Counting [Semaphore]()) / バイナリセマフォ 

資源の獲得・返却によるタスク間同期を行います。資源がない場合はタスクが `State::Blocked` に遷移して休止し、返却時に優先度の高いタスクから順に起床します。



```cpp
tk::Semaphore sem(0, 5); // 初期値0、最大5

// タスク側 (資源が空くまでブロック待機)
sem.acquire();

// シグナル送信側 (割り込みや他タスクから返却・通知)
sem.release();
```

## Public Functions Documentation

### function Semaphore

```cpp
inline explicit Semaphore(
    int32_t initial_count =1,
    int32_t max_count =1
)
```

セマフォを初期化します 

**Parameters**: 

  * **initial_count** 初期資源カウント (省略時: 1) 
  * **max_count** 最大資源カウント (省略時: 1) 


### function acquire

```cpp
inline void acquire()
```

資源を獲得するまで現在のタスクをブロックします 

資源カウントが 0 以下の場合はタスクが休止状態になり、CPU は他タスクへ譲渡されます (CPU消費ゼロ)。 


### function try_acquire

```cpp
inline bool try_acquire()
```

資源の獲得を試みます (ノンブロッキング) 

**Return**: 

  * true 獲得成功 (カウントが 1 減算された) 
  * false 資源がないため獲得失敗 (ブロックせず即座に復帰) 


### function release

```cpp
inline void release()
```

資源を返却し、待機中タスクがあれば最優先度のタスクを 1 つ起床させます 

### function count

```cpp
inline int32_t count() const
```


現在の資源カウントを取得 


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900