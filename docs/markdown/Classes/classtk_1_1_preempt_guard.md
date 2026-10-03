---
title: tk::PreemptGuard
summary: プリエンプション禁止をスコープ単位で安全に管理する RAII クラス 

---

# tk::PreemptGuard



プリエンプション禁止をスコープ単位で安全に管理する RAII クラス  [More...](#detailed-description)


`#include <kernel.hpp>`

## Public Functions

|                | Name           |
| -------------- | -------------- |
| | **[PreemptGuard](Classes/classtk_1_1_preempt_guard.md#function-preemptguard)**() |
| | **[~PreemptGuard](Classes/classtk_1_1_preempt_guard.md#function-~preemptguard)**() |
| | **[PreemptGuard](Classes/classtk_1_1_preempt_guard.md#function-preemptguard)**(const PreemptGuard & ) =delete |
| [PreemptGuard](Classes/classtk_1_1_preempt_guard.md#function-preemptguard) & | **[operator=](Classes/classtk_1_1_preempt_guard.md#function-operator=)**(const [PreemptGuard](Classes/classtk_1_1_preempt_guard.md#function-preemptguard) & ) =delete |

## Detailed Description

```cpp
class tk::PreemptGuard;
```

プリエンプション禁止をスコープ単位で安全に管理する RAII クラス 

生成時に自動で `[tk::preempt_disable()](Namespaces/namespacetk.md#function-preempt-disable)` を呼び、スコープを抜けた際に自動で `[tk::preempt_enable()](Namespaces/namespacetk.md#function-preempt-enable)` を呼びます。



```cpp
{
    tk::PreemptGuard guard;
    // この区間はタスクスイッチが発生しない (I2C/SPI通信などに最適)
    i2c_write_blocking(...);
} // 自動でタスクスイッチが再開
```

## Public Functions Documentation

### function PreemptGuard

```cpp
inline PreemptGuard()
```


### function ~PreemptGuard

```cpp
inline ~PreemptGuard()
```


### function PreemptGuard

```cpp
PreemptGuard(
    const PreemptGuard & 
) =delete
```


### function operator=

```cpp
PreemptGuard & operator=(
    const PreemptGuard & 
) =delete
```


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900