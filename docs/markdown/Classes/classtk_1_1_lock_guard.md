---
title: tk::LockGuard
summary: ミューテックスのロックをスコープ単位で安全に管理する RAII クラス (std::lock_guard 相当) 

---

# tk::LockGuard



ミューテックスのロックをスコープ単位で安全に管理する RAII クラス (std::lock_guard 相当)  [More...](#detailed-description)


`#include <sync.hpp>`

## Public Functions

|                | Name           |
| -------------- | -------------- |
| | **[LockGuard](Classes/classtk_1_1_lock_guard.md#function-lockguard)**(Lockable & m)<br>ロック対象を受け取り、即座に [lock()](Namespaces/namespacetk.md#function-lock) します  |
| | **[~LockGuard](Classes/classtk_1_1_lock_guard.md#function-~lockguard)**() |
| | **[LockGuard](Classes/classtk_1_1_lock_guard.md#function-lockguard)**(const LockGuard & ) =delete |
| [LockGuard](Classes/classtk_1_1_lock_guard.md#function-lockguard) & | **[operator=](Classes/classtk_1_1_lock_guard.md#function-operator=)**(const [LockGuard](Classes/classtk_1_1_lock_guard.md#function-lockguard) & ) =delete |

## Detailed Description

```cpp
template <typename Lockable >
class tk::LockGuard;
```

ミューテックスのロックをスコープ単位で安全に管理する RAII クラス (std::lock_guard 相当) 

**Template Parameters**: 

  * **Lockable** [lock()](Namespaces/namespacetk.md#function-lock) と [unlock()](Namespaces/namespacetk.md#function-unlock) を持つ型 (例: [tk::Mutex](Classes/classtk_1_1_mutex.md)) 


構築時に `[lock()](Namespaces/namespacetk.md#function-lock)` を呼び、デストラクタで自動的に `[unlock()](Namespaces/namespacetk.md#function-unlock)` を呼び出します。 途中で return や例外が発生しても確実にロックが解放されます。

## Public Functions Documentation

### function LockGuard

```cpp
inline explicit LockGuard(
    Lockable & m
)
```

ロック対象を受け取り、即座に [lock()](Namespaces/namespacetk.md#function-lock) します 

**Parameters**: 

  * **m** ロック対象のミューテックス参照 


### function ~LockGuard

```cpp
inline ~LockGuard()
```


スコープ終了時に自動的に [unlock()](Namespaces/namespacetk.md#function-unlock) します 


### function LockGuard

```cpp
LockGuard(
    const LockGuard & 
) =delete
```


### function operator=

```cpp
LockGuard & operator=(
    const LockGuard & 
) =delete
```


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900