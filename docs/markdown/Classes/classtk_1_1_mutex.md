---
title: tk::Mutex
summary: 所有権・優先度順ウェイクアップ・RAII 対応のミューテックス (排他ロック) 

---

# tk::Mutex



所有権・優先度順ウェイクアップ・RAII 対応のミューテックス (排他ロック)  [More...](#detailed-description)


`#include <sync.hpp>`

## Public Functions

|                | Name           |
| -------------- | -------------- |
| | **[Mutex](Classes/classtk_1_1_mutex.md#function-mutex)**() =default |
| void | **[lock](Classes/classtk_1_1_mutex.md#function-lock)**()<br>ミューテックスをロックします (空くまでブロック待機)  |
| bool | **[try_lock](Classes/classtk_1_1_mutex.md#function-try-lock)**()<br>ミューテックスのロックを試みます (ノンブロッキング)  |
| void | **[unlock](Classes/classtk_1_1_mutex.md#function-unlock)**()<br>ミューテックスのロックを解除します  |
| bool | **[is_locked](Classes/classtk_1_1_mutex.md#function-is-locked)**() const |

## Detailed Description

```cpp
class tk::Mutex;
```

所有権・優先度順ウェイクアップ・RAII 対応のミューテックス (排他ロック) 

共有リソースやペリフェラルへの同時アクセスを防止します。 ロックしたタスク本人以外の誤った `[unlock()](Classes/classtk_1_1_mutex.md#function-unlock)` は安全に無視されます。 C++ の `BasicLockable` を満たしており、`std::lock_guard` や `[tk::LockGuard](Classes/classtk_1_1_lock_guard.md)` が利用可能です。



```cpp
tk::Mutex mtx;

{
    tk::LockGuard<tk::Mutex> lock(mtx);
    // クリティカルセクション...
} // 自動でアンロック
```

## Public Functions Documentation

### function Mutex

```cpp
Mutex() =default
```


### function lock

```cpp
inline void lock()
```

ミューテックスをロックします (空くまでブロック待機) 

他のタスクが保持している場合は、解放されるまで自タスクを `State::Blocked` にして休止します。 


### function try_lock

```cpp
inline bool try_lock()
```

ミューテックスのロックを試みます (ノンブロッキング) 

**Return**: 

  * true ロック獲得成功 
  * false 他のタスクが保持しているため獲得失敗 (ブロックせず即座に復帰) 


### function unlock

```cpp
inline void unlock()
```

ミューテックスのロックを解除します 

ロックを保持しているタスク本人だけが解除できます。 待機中のタスクが存在する場合、最も優先度が高いタスクを 1 つ起床させます。 


### function is_locked

```cpp
inline bool is_locked() const
```


現在ロックされているかどうかを判定 


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900