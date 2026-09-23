#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One-time port setup: FPU context policy + SysTick. Called from tk::start().
void tk_port_init(uint32_t ticks_per_sec);

// Build a task's initial stack frame. Returns the SP to start with.
uintptr_t tk_port_make_frame(uint8_t* base, uint32_t size_bytes, void* entry_fn, void* arg);

// Jump into the first task (r0 = SP from tk_port_make_frame). Never returns.
void tk_port_start(uintptr_t sp);

#ifdef __cplusplus
}
#endif
