---
title: C:/Users/yoneda/Documents/Pico_Projects/tinykernel/tinykernel/include/tinykernel/kernel.hpp

---

# C:/Users/yoneda/Documents/Pico_Projects/tinykernel/tinykernel/include/tinykernel/kernel.hpp



## Namespaces

| Name           |
| -------------- |
| **[tk](Namespaces/namespacetk.md)**  |
| **[tk::detail](Namespaces/namespacetk_1_1detail.md)**  |

## Classes

|                | Name           |
| -------------- | -------------- |
| struct | **[tk::TaskHandle](Classes/structtk_1_1_task_handle.md)** <br>タスクの識別ハンドル  |
| class | **[tk::PreemptGuard](Classes/classtk_1_1_preempt_guard.md)** <br>プリエンプション禁止をスコープ単位で安全に管理する RAII クラス  |

## Defines

|                | Name           |
| -------------- | -------------- |
|  | **[TK_TICKS_PER_SEC](Files/kernel_8hpp.md#define-tk-ticks-per-sec)**  |
|  | **[TK_TIME_SLICE_MS](Files/kernel_8hpp.md#define-tk-time-slice-ms)**  |
|  | **[TK_MAX_TASKS](Files/kernel_8hpp.md#define-tk-max-tasks)**  |
|  | **[TK_DEFAULT_STACK_SIZE](Files/kernel_8hpp.md#define-tk-default-stack-size)**  |
|  | **[TK_TASK](Files/kernel_8hpp.md#define-tk-task)**(fn, prio) <br>リンク時にタスクを自動登録するマクロ (静的タスク登録)  |




## Macros Documentation

### define TK_TICKS_PER_SEC

```cpp
#define TK_TICKS_PER_SEC 1000u
```


1秒あたりのタイマースケジューラtick数 (Hz) 


### define TK_TIME_SLICE_MS

```cpp
#define TK_TIME_SLICE_MS 10u
```


同一優先度タスク間のタイムスライス時間 (ミリ秒) 


### define TK_MAX_TASKS

```cpp
#define TK_MAX_TASKS 32u
```


最大タスク数 (スロット0はCore0アイドル、スロット1はCore1アイドル専用) 


### define TK_DEFAULT_STACK_SIZE

```cpp
#define TK_DEFAULT_STACK_SIZE (4 * 1024)
```


1タスクあたりのスタックサイズ (バイト) 


### define TK_TASK

```cpp
#define TK_TASK(
    fn,
    prio
)
static void tk_register_##fn(void) {                              \
    ::tk::detail::static_register(#fn, fn, (prio));               \
}                                                                 \
PICO_RUNTIME_INIT_FUNC(tk_register_##fn, "ZZ900");
```

リンク時にタスクを自動登録するマクロ (静的タスク登録) 

**Parameters**: 

  * **fn** タスク関数名 (void fn(void* arg)) 
  * **prio** タスク優先度 (例: [tk::PRIO_NORMAL](Namespaces/namespacetk.md#enumvalue-prio-normal)) 


main() の開始前に Pico SDK のランタイムイニシャライザを利用してタスクを登録します。


## Source code

```cpp
// tinykernel: minimal preemptive task scheduler for RP2350 (Pico 2, Cortex-M33)
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "pico/runtime.h"  // PICO_RUNTIME_INIT_FUNC (static task registration)

#ifndef TK_TICKS_PER_SEC
#define TK_TICKS_PER_SEC 1000u
#endif

#ifndef TK_TIME_SLICE_MS
#define TK_TIME_SLICE_MS 10u
#endif

#ifndef TK_MAX_TASKS
#define TK_MAX_TASKS 32u
#endif

#ifndef TK_DEFAULT_STACK_SIZE
#define TK_DEFAULT_STACK_SIZE (4 * 1024)
#endif

namespace tk {

using TaskFn = void (*)(void* arg);

enum Prio : uint32_t {
    PRIO_REALTIME = 1,   
    PRIO_HIGH     = 5,   
    PRIO_NORMAL   = 9,   
    PRIO_LOW      = 13,  
};

struct TaskHandle {
    uint8_t slot{0xFF};  

    bool is_valid() const { return slot != 0xFF; }
};

TaskHandle create(TaskFn fn, void* arg = nullptr, uint32_t prio = PRIO_NORMAL);

void start();

void yield();

void sleep_ms(uint32_t ms);

void exit(int code = 0);

uint64_t now_ms();

bool lock();

void unlock(bool was_enabled);

void preempt_disable();

void preempt_enable();

class PreemptGuard {
public:
    PreemptGuard() { preempt_disable(); }
    ~PreemptGuard() { preempt_enable(); }

    PreemptGuard(const PreemptGuard&) = delete;
    PreemptGuard& operator=(const PreemptGuard&) = delete;
};

using task_mask_t = uint32_t;

namespace detail {
void static_register(const char* name, TaskFn fn, uint32_t prio);
uint32_t sched_lock();
void sched_unlock(uint32_t save);
void block_current_task_locked();
void wake_task_locked(TaskHandle handle);
uint8_t current_task_slot();
uint8_t find_highest_prio_waiter(task_mask_t wait_mask);
}  // namespace detail

}  // namespace tk

#define TK_TASK(fn, prio)                                              \
    static void tk_register_##fn(void) {                              \
        ::tk::detail::static_register(#fn, fn, (prio));               \
    }                                                                 \
    PICO_RUNTIME_INIT_FUNC(tk_register_##fn, "ZZ900");
```


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900
