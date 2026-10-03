---
title: C:/Users/yoneda/Documents/Pico_Projects/tinykernel/tinykernel/src/port/m33/port_m33.h

---

# C:/Users/yoneda/Documents/Pico_Projects/tinykernel/tinykernel/src/port/m33/port_m33.h



## Functions

|                | Name           |
| -------------- | -------------- |
| void | **[tk_port_init](Files/port__m33_8h.md#function-tk-port-init)**(uint32_t ticks_per_sec) |
| uintptr_t | **[tk_port_make_frame](Files/port__m33_8h.md#function-tk-port-make-frame)**(uint8_t * base, uint32_t size_bytes, void * entry_fn, void * arg) |
| void | **[tk_port_start](Files/port__m33_8h.md#function-tk-port-start)**(uintptr_t sp) |
| void | **[tk_port_trigger_pendsv](Files/port__m33_8h.md#function-tk-port-trigger-pendsv)**(void ) |


## Functions Documentation

### function tk_port_init

```cpp
void tk_port_init(
    uint32_t ticks_per_sec
)
```


### function tk_port_make_frame

```cpp
uintptr_t tk_port_make_frame(
    uint8_t * base,
    uint32_t size_bytes,
    void * entry_fn,
    void * arg
)
```


### function tk_port_start

```cpp
void tk_port_start(
    uintptr_t sp
)
```


### function tk_port_trigger_pendsv

```cpp
static inline void tk_port_trigger_pendsv(
    void 
)
```




## Source code

```cpp
#pragma once

#include <stdint.h>

#include "hardware/structs/scb.h"

#ifdef __cplusplus
extern "C" {
#endif

// One-time port setup: FPU context policy + SysTick + PendSV priority. Called from tk::start().
void tk_port_init(uint32_t ticks_per_sec);

// Build a task's initial stack frame. Returns the SP to start with.
uintptr_t tk_port_make_frame(uint8_t* base, uint32_t size_bytes, void* entry_fn, void* arg);

// Jump into the first task (r0 = SP from tk_port_make_frame). Never returns.
void tk_port_start(uintptr_t sp);

// Trigger a PendSV exception to initiate context switching.
static inline void tk_port_trigger_pendsv(void) {
    scb_hw->icsr = 1u << 28;  // SCB_ICSR_PENDSVSET_Msk
    __asm__ volatile("isb" ::: "memory");
}

#ifdef __cplusplus
}
#endif
```


-------------------------------

Updated on 2026-10-02 at 14:01:32 +0900
