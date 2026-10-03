---
title: tk::TaskHandle
summary: タスクの識別ハンドル 

---

# tk::TaskHandle



タスクの識別ハンドル 


`#include <kernel.hpp>`

## Public Functions

|                | Name           |
| -------------- | -------------- |
| bool | **[is_valid](Classes/structtk_1_1_task_handle.md#function-is-valid)**() const |

## Public Attributes

|                | Name           |
| -------------- | -------------- |
| uint8_t | **[slot](Classes/structtk_1_1_task_handle.md#variable-slot)** <br>タスクスロット番号 (0xFF は無効/失敗)  |

## Public Functions Documentation

### function is_valid

```cpp
inline bool is_valid() const
```


ハンドルが有効かどうかを判定 


## Public Attributes Documentation

### variable slot

```cpp
uint8_t slot {0xFF};
```

タスクスロット番号 (0xFF は無効/失敗) 

-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900