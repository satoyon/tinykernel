#include "tinykernel/kernel.hpp"

#include <cstring>

#include "pico/platform.h"

#include "port/m33/port_m33.h"

// FPU state saved explicitly on every switch. Lazy stacking is disabled
// (FPCCR.LSPEN=0 at kernel start and never re-enabled), so hardware never pushes
// VFP frames onto task stacks; S0-S31 are the only floating point state that
// matters (see context.S).
struct FpuCtx {
    float s[32];
};
static_assert(sizeof(FpuCtx) == 128, "FPU context layout must match context.S");

namespace {

enum class State : uint8_t { Free = 0, Ready, Sleeping, Terminated };

struct TaskSlot {
    State state;
    tk::TaskFn fn;
    void* arg;
    const char* name;
    uint32_t prio;      // 0..15, lower is higher priority
    uintptr_t sp;       // saved SP (points at the core8 frame); 0 = not started yet
    int64_t wake_tick;
    int32_t slice_left;
    bool is_idle;
    FpuCtx fpu;
};

static_assert(TK_MAX_TASKS <= 255, "slot index must fit in a byte");

alignas(16) static TaskSlot g_slots[TK_MAX_TASKS];  // slot 0 = idle (reserved)
alignas(8) static char g_stacks[TK_MAX_TASKS][TK_DEFAULT_STACK_SIZE];

static uint8_t g_cur = 0;
static int64_t g_tick = 0;
static bool g_started = false;
static uint8_t g_rr_last[16] = {0};

constexpr uint32_t kIdlePrio = 15u;
constexpr int32_t kSliceTicks = (int32_t)(((uint64_t)TK_TIME_SLICE_MS * TK_TICKS_PER_SEC + 999u) / 1000u);
static_assert(kSliceTicks >= 1, "time slice must be at least one tick");

static void idle_fn(void* arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static inline bool is_runnable(const TaskSlot& s) {
    if (s.is_idle) return true;
    return s.state == State::Ready;
}

// Highest priority level (lowest number) that has at least one runnable task. Idle
// guarantees that level 15 always qualifies, so this never returns -1 after start().
static int32_t find_prio_level(void) {
    for (uint32_t p = 0; p < 16u; ++p) {
        for (uint32_t i = 0; i < TK_MAX_TASKS; ++i) {
            if (g_slots[i].prio == p && is_runnable(g_slots[i])) return (int32_t)p;
        }
    }
    return -1;
}

// Round-robin pick among runnable tasks of the given priority level, starting after
// g_rr_last[level] so that equal-priority tasks rotate fairly.
static uint32_t pick_round_robin(uint32_t level) {
    for (uint32_t k = 1; k <= TK_MAX_TASKS; ++k) {
        uint32_t i = (g_rr_last[level] + k) % TK_MAX_TASKS;
        if (g_slots[i].prio == level && is_runnable(g_slots[i])) return i;
    }
    return g_cur;
}

// Choose the next task to run. Returns a slot index, or g_cur to keep running.
static uint32_t select_next(bool force_switch) {
    int32_t level = find_prio_level();
    if (level < 0) return g_cur;
    const TaskSlot& cur = g_slots[g_cur];
    if (!force_switch && cur.prio == (uint32_t)level && is_runnable(cur)) return g_cur;
    // pick_round_robin only ever returns a runnable slot, so if the current task is no
    // longer runnable it can never be picked back here.
    return pick_round_robin((uint32_t)level);
}

}  // namespace

// Referenced (unmangled) from context.S: scratch area the port asm uses to move FPU
// state between hardware registers and the per-task copies in g_slots[].
extern "C" {
    alignas(8) FpuCtx tk_fpu_scratch = {};
}

// SysTick interrupt handler: advances ticks, wakes sleeping tasks, and triggers PendSV when needed.
extern "C" void isr_systick(void) {
    ++g_tick;
    bool need_switch = false;
    for (uint32_t i = 1; i < TK_MAX_TASKS; ++i) {
        TaskSlot& s = g_slots[i];
        if (s.state == State::Sleeping && g_tick >= s.wake_tick) {
            s.state = State::Ready;
            if (s.prio < g_slots[g_cur].prio) {
                need_switch = true;
            }
        }
    }
    TaskSlot& cur = g_slots[g_cur];
    if (!cur.is_idle) {
        --cur.slice_left;
        if (cur.slice_left <= 0) {
            cur.slice_left = kSliceTicks;
            need_switch = true;
        }
    }
    if (need_switch) {
        tk_port_trigger_pendsv();
    }
}

// PendSV context switch function: saves cur_sp, selects next task, and returns next SP.
extern "C" uintptr_t tk_switch_context(uintptr_t cur_sp) {
    TaskSlot& cur = g_slots[g_cur];
    cur.sp = cur_sp;
    memcpy(&cur.fpu, &tk_fpu_scratch, sizeof(cur.fpu));

    uint32_t next = select_next(/*force_switch=*/true);
    TaskSlot& nxt = g_slots[next];
    if (nxt.sp == 0) {
        nxt.sp = tk_port_make_frame(reinterpret_cast<uint8_t*>(&g_stacks[next][0]),
                                    TK_DEFAULT_STACK_SIZE, reinterpret_cast<void*>(nxt.fn), nxt.arg);
    }
    memcpy(&tk_fpu_scratch, &nxt.fpu, sizeof(nxt.fpu));
    nxt.slice_left = kSliceTicks;
    g_rr_last[nxt.prio] = (uint8_t)next;
    g_cur = (uint8_t)next;
    return nxt.sp;
}

namespace tk::detail {

void static_register(const char* name, TaskFn fn, uint32_t prio) {
    if (prio >= kIdlePrio) prio = 14u;
    for (uint32_t i = 1; i < TK_MAX_TASKS; ++i) {
        if (g_slots[i].state == State::Free) {
            g_slots[i] = TaskSlot{};
            g_slots[i].fn = fn;
            g_slots[i].name = name;
            g_slots[i].prio = prio;
            g_slots[i].state = State::Ready;
            return;
        }
    }
}

}  // namespace tk::detail

tk::TaskHandle tk::create(TaskFn fn, void* arg, uint32_t prio) {
    TaskHandle h{0xFF};
    if (prio >= kIdlePrio) prio = 14u;
    for (uint32_t i = 1; i < TK_MAX_TASKS; ++i) {
        if (g_slots[i].state == State::Free) {
            g_slots[i] = TaskSlot{};
            g_slots[i].fn = fn;
            g_slots[i].arg = arg;
            g_slots[i].name = "dyn";
            g_slots[i].prio = prio;
            g_slots[i].state = State::Ready;
            h.slot = (uint8_t)i;
            return h;
        }
    }
    return h;
}

void tk::start() {
    if (g_started) for (;;) __asm__ volatile("wfi");
    g_started = true;

    TaskSlot& idle = g_slots[0];
    idle.fn = idle_fn;
    idle.name = "idle";
    idle.prio = kIdlePrio;
    idle.is_idle = true;
    idle.state = State::Ready;

    tk_port_init(TK_TICKS_PER_SEC);  // FPU policy + SysTick (enabled at the end)

    uint32_t next = select_next(/*force_switch=*/true);
    TaskSlot& nxt = g_slots[next];
    if (nxt.sp == 0) {
        nxt.sp = tk_port_make_frame(reinterpret_cast<uint8_t*>(&g_stacks[next][0]),
                                    TK_DEFAULT_STACK_SIZE, reinterpret_cast<void*>(nxt.fn), nxt.arg);
    }
    memcpy(&tk_fpu_scratch, &nxt.fpu, sizeof(nxt.fpu));
    nxt.slice_left = kSliceTicks;
    g_rr_last[nxt.prio] = (uint8_t)next;
    g_cur = (uint8_t)next;

    tk_port_start(nxt.sp);  // never returns
    for (;;) __asm__ volatile("wfi");
}

void tk::yield() {
    bool lk = tk::lock();
    g_slots[g_cur].slice_left = 0;
    tk_port_trigger_pendsv();
    tk::unlock(lk);
}

void tk::sleep_ms(uint32_t ms) {
    uint64_t ticks = ((uint64_t)ms * TK_TICKS_PER_SEC + 999u) / 1000u;
    if (ticks < 1u) ticks = 1u;
    int32_t sleep_ticks = (int32_t)(ticks > 0x7FFFFFFFu ? 0x7FFFFFFFu : ticks);

    bool lk = tk::lock();
    TaskSlot& cur = g_slots[g_cur];
    cur.state = State::Sleeping;
    cur.wake_tick = g_tick + sleep_ticks;
    tk_port_trigger_pendsv();
    tk::unlock(lk);
}

void tk::exit(int code) {
    (void)code;
    bool lk = tk::lock();
    g_slots[g_cur].state = State::Terminated;
    tk_port_trigger_pendsv();
    tk::unlock(lk);
    for (;;) __asm__ volatile("wfi");  // unreachable: the scheduler switches away
}

extern "C" void tk_task_return_trampoline(void) {
    tk::exit(0);
}

uint64_t tk::now_ms() {
    return (uint64_t)g_tick * 1000u / TK_TICKS_PER_SEC;
}

bool tk::lock() {
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask) :: "memory");
    return primask == 0;
}

void tk::unlock(bool was_enabled) {
    if (was_enabled) {
        __asm__ volatile("cpsie i" ::: "memory");
    }
}
