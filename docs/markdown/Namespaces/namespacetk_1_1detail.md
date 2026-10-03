---
title: tk::detail

---

# tk::detail



## Functions

|                | Name           |
| -------------- | -------------- |
| void | **[static_register](Namespaces/namespacetk_1_1detail.md#function-static-register)**(const char * name, [TaskFn](Namespaces/namespacetk.md#using-taskfn) fn, uint32_t prio) |
| uint32_t | **[sched_lock](Namespaces/namespacetk_1_1detail.md#function-sched-lock)**() |
| void | **[sched_unlock](Namespaces/namespacetk_1_1detail.md#function-sched-unlock)**(uint32_t save) |
| void | **[block_current_task_locked](Namespaces/namespacetk_1_1detail.md#function-block-current-task-locked)**() |
| void | **[wake_task_locked](Namespaces/namespacetk_1_1detail.md#function-wake-task-locked)**([TaskHandle](Classes/structtk_1_1_task_handle.md) handle) |
| uint8_t | **[current_task_slot](Namespaces/namespacetk_1_1detail.md#function-current-task-slot)**() |
| uint8_t | **[find_highest_prio_waiter](Namespaces/namespacetk_1_1detail.md#function-find-highest-prio-waiter)**([task_mask_t](Namespaces/namespacetk.md#using-task-mask-t) wait_mask) |


## Functions Documentation

### function static_register

```cpp
void static_register(
    const char * name,
    TaskFn fn,
    uint32_t prio
)
```


### function sched_lock

```cpp
uint32_t sched_lock()
```


### function sched_unlock

```cpp
void sched_unlock(
    uint32_t save
)
```


### function block_current_task_locked

```cpp
void block_current_task_locked()
```


### function wake_task_locked

```cpp
void wake_task_locked(
    TaskHandle handle
)
```


### function current_task_slot

```cpp
uint8_t current_task_slot()
```


### function find_highest_prio_waiter

```cpp
uint8_t find_highest_prio_waiter(
    task_mask_t wait_mask
)
```






-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900