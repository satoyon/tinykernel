// tinykernel: minimal preemptive task scheduler for RP2350 (Pico 2, Cortex-M33)
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "pico/runtime.h"  // PICO_RUNTIME_INIT_FUNC (static task registration)

#ifndef TK_TICKS_PER_SEC
#define TK_TICKS_PER_SEC 1000u
#endif

// Time slice per task at the same priority level, in milliseconds.
#ifndef TK_TIME_SLICE_MS
#define TK_TIME_SLICE_MS 10u
#endif

// Total number of task slots (slot 0 is reserved for the built-in idle task).
#ifndef TK_MAX_TASKS
#define TK_MAX_TASKS 16u
#endif

// Stack size per task, in bytes. Stacks come from a static pool (no malloc).
#ifndef TK_DEFAULT_STACK_SIZE
#define TK_DEFAULT_STACK_SIZE (4 * 1024)
#endif

namespace tk {

using TaskFn = void (*)(void* arg);

// Priority levels: numerically lower means higher priority. Level 15 is reserved for idle.
enum Prio : uint32_t {
    PRIO_REALTIME = 1,
    PRIO_HIGH     = 5,
    PRIO_NORMAL   = 9,
    PRIO_LOW      = 13,
};

struct TaskHandle {
    uint8_t slot{0xFF};  // 0xFF on failure
    bool is_valid() const { return slot != 0xFF; }
};

// Create a task. Must be called before tk::start() (Phase 1 restriction).
TaskHandle create(TaskFn fn, void* arg = nullptr, uint32_t prio = PRIO_NORMAL);

// Start the scheduler and run tasks preemptively. Never returns.
void start();

// Task API: must be called from a task context.
void yield();
void sleep_ms(uint32_t ms);
void exit(int code = 0);

// Millisecond clock, resolution is one tick (1/TK_TICKS_PER_SEC s).
uint64_t now_ms();

// Simple critical section based on disabling interrupts.
bool lock();
void unlock(bool was_enabled);

// Preemption disable/enable for the current core without disabling interrupts.
// Useful for peripheral transactions (I2C, SPI) that should not be interrupted by other tasks.
// Nesting is supported.
void preempt_disable();
void preempt_enable();

class PreemptGuard {
public:
    PreemptGuard() { preempt_disable(); }
    ~PreemptGuard() { preempt_enable(); }

    PreemptGuard(const PreemptGuard&) = delete;
    PreemptGuard& operator=(const PreemptGuard&) = delete;
};

namespace detail {
void static_register(const char* name, TaskFn fn, uint32_t prio);
uint32_t sched_lock();
void sched_unlock(uint32_t save);
void block_current_task_locked();
void wake_task_locked(TaskHandle handle);
uint8_t current_task_slot();
uint8_t find_highest_prio_waiter(uint16_t wait_mask);
}  // namespace detail

}  // namespace tk

// Register a task at link time: just write the function and use this macro.
// `fn` must be a plain (non-static) function named uniquely across the program.
#define TK_TASK(fn, prio)                                              \
    static void tk_register_##fn(void) {                              \
        ::tk::detail::static_register(#fn, fn, (prio));               \
    }                                                                 \
    PICO_RUNTIME_INIT_FUNC(tk_register_##fn, "ZZ900");
