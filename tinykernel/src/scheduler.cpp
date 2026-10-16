#include "tinykernel/kernel.hpp"

#include <cstring>

#include "pico/platform.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

#include "port/m33/port_m33.h"

struct FpuCtx {
    float s[32];
};
static_assert(sizeof(FpuCtx) == 128, "FPU context layout must match context.S");

namespace {

enum class State : uint8_t { Free = 0, Ready, Running, Sleeping, Blocked, Terminated };

struct TaskSlot {
    State state;
    tk::TaskFn fn;
    void* arg;
    tk::detail::SignalHandlerFn sig_fn;
    const char* name;
    uint32_t prio;          // 0..15, lower is higher priority
    uintptr_t sp;           // saved SP (points at the core8 frame); 0 = not started yet
    uint8_t* stack_base;    // nullptr = use default g_stacks[slot]
    size_t stack_size;      // 0 = use TK_DEFAULT_STACK_SIZE
    int64_t wake_tick;
    int32_t slice_left;
    uint32_t pending_signal;// 0 = no pending signal
    bool sleep_interrupted; // true if sleep/block was broken early by a signal
    bool stop_requested;    // true if SIG_STOP was sent
    bool is_idle;
    FpuCtx fpu;
};

static_assert(TK_MAX_TASKS <= 255, "slot index must fit in a byte");

alignas(16) static TaskSlot g_slots[TK_MAX_TASKS];  // slot 0 = idle0, slot 1 = idle1 (if SMP)
alignas(8) static char g_stacks[TK_MAX_TASKS][TK_DEFAULT_STACK_SIZE];

#if TK_NUM_CORES > 1
static uint8_t g_cur[TK_NUM_CORES] = {0, 1};
#else
static uint8_t g_cur[TK_NUM_CORES] = {0};
#endif
static volatile uint32_t g_preempt_disabled[TK_NUM_CORES] = {0};
static int64_t g_tick = 0;
static bool g_started = false;
static uint8_t g_rr_last[16] = {0};

static spin_lock_t* g_sched_lock = nullptr;

static inline void ensure_sched_lock(void) {
    if (!g_sched_lock) {
        int lock_num = spin_lock_claim_unused(true);
        g_sched_lock = spin_lock_instance(lock_num);
    }
}

constexpr uint32_t kIdlePrio = 15u;
constexpr int32_t kSliceTicks = (int32_t)(((uint64_t)TK_TIME_SLICE_MS * TK_TICKS_PER_SEC + 999u) / 1000u);
static_assert(kSliceTicks >= 1, "time slice must be at least one tick");

static inline uint32_t sched_lock(void) {
    ensure_sched_lock();
    return spin_lock_blocking(g_sched_lock);
}

static inline void sched_unlock(uint32_t save) {
    spin_unlock(g_sched_lock, save);
    __asm__ volatile("isb" ::: "memory");
}

static inline uintptr_t init_task_frame(uint32_t slot_idx, TaskSlot& s) {
    uint8_t* base = s.stack_base ? s.stack_base : reinterpret_cast<uint8_t*>(&g_stacks[slot_idx][0]);
    uint32_t sz   = s.stack_size ? (uint32_t)s.stack_size : (uint32_t)TK_DEFAULT_STACK_SIZE;
    return tk_port_make_frame(base, sz, reinterpret_cast<void*>(s.fn), s.arg);
}

static inline bool is_slot_on_cpu_locked(uint8_t slot) {
    if (!g_started) return false;
    for (uint32_t c = 0; c < TK_NUM_CORES; ++c) {
        if (g_cur[c] == slot) return true;
    }
    return false;
}

static void idle_fn(void* arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static inline bool is_runnable(uint32_t slot_idx, uint32_t core) {
    const TaskSlot& s = g_slots[slot_idx];
    if (s.is_idle) {
        return slot_idx == core; // Core 0 only runs idle 0, Core 1 only runs idle 1
    }
#if TK_NUM_CORES > 1
    for (uint32_t c = 0; c < TK_NUM_CORES; ++c) {
        if (c != core && g_cur[c] == slot_idx) {
            return false; // Still on the other CPU core (e.g. entering PendSV)
        }
    }
#endif
    return s.state == State::Ready;
}

// Highest priority level with at least one runnable task for this core.
static int32_t find_prio_level(uint32_t core) {
    for (uint32_t p = 0; p < 16u; ++p) {
        for (uint32_t i = 0; i < TK_MAX_TASKS; ++i) {
            if (g_slots[i].prio == p && is_runnable(i, core)) return (int32_t)p;
        }
    }
    return (int32_t)kIdlePrio;
}

static uint32_t pick_round_robin(uint32_t level, uint32_t core) {
    if (level == kIdlePrio) return core;
    for (uint32_t k = 1; k <= TK_MAX_TASKS; ++k) {
        uint32_t i = (g_rr_last[level] + k) % TK_MAX_TASKS;
        if (i >= TK_NUM_CORES && g_slots[i].prio == level && is_runnable(i, core)) return i;
    }
    return core;
}

// Choose the next task for the given core.
static uint32_t select_next(uint32_t core) {
    int32_t level = find_prio_level(core);
    if (level < 0) return core;
    return pick_round_robin((uint32_t)level, core);
}

}  // namespace

// Referenced (unmangled) from context.S: scratch area per-core for FPU state transfer.
extern "C" {
    alignas(8) FpuCtx tk_fpu_scratch[2] = {};
}

// SysTick interrupt handler: advances ticks on Core 0, wakes sleeping tasks, and checks time slices.
extern "C" void isr_systick(void) {
    uint32_t core = get_core_num();
    uint32_t save = sched_lock();

    bool need_switch = false;
    if (core == 0) {
        ++g_tick;
        for (uint32_t i = TK_NUM_CORES; i < TK_MAX_TASKS; ++i) {
            TaskSlot& s = g_slots[i];
            if (s.state == State::Sleeping && g_tick >= s.wake_tick) {
                s.state = State::Ready;
                need_switch = true;
            }
        }
    }

    TaskSlot& cur = g_slots[g_cur[core]];
    if (cur.is_idle) {
        if (find_prio_level(core) < (int32_t)kIdlePrio) {
            need_switch = true;
        }
    } else {
        --cur.slice_left;
        if (cur.slice_left <= 0) {
            cur.slice_left = kSliceTicks;
            need_switch = true;
        }
    }

    sched_unlock(save);

    if (need_switch) {
        tk_port_trigger_pendsv();
    }
}

// PendSV context switch function: saves cur_sp, selects next task, and returns next SP.
extern "C" uintptr_t tk_switch_context(uintptr_t cur_sp) {
    uint32_t core = get_core_num();
    uint32_t save = sched_lock();

    TaskSlot& cur = g_slots[g_cur[core]];
    if (g_preempt_disabled[core] > 0 && cur.state == State::Running) {
        sched_unlock(save);
        return cur_sp;
    }

    cur.sp = cur_sp;
    memcpy(&cur.fpu, &tk_fpu_scratch[core], sizeof(cur.fpu));
    if (cur.state == State::Running) {
        cur.state = State::Ready;
    }

    uint32_t next = select_next(core);
    TaskSlot& nxt = g_slots[next];
    if (nxt.sp == 0) {
        nxt.sp = init_task_frame(next, nxt);
    }
    nxt.state = State::Running;
    memcpy(&tk_fpu_scratch[core], &nxt.fpu, sizeof(nxt.fpu));
    nxt.slice_left = kSliceTicks;
    g_rr_last[nxt.prio] = (uint8_t)next;
    g_cur[core] = (uint8_t)next;

    sched_unlock(save);
    return nxt.sp;
}

namespace tk::detail {

void static_register(const char* name, TaskFn fn, uint32_t prio) {
    TaskHandle h{0xFF};
    start_or_restart_task(&h, fn, nullptr, nullptr, prio, nullptr, 0, name);
}

bool start_or_restart_task(TaskHandle* inout_handle, TaskFn fn, void* arg,
                           SignalHandlerFn sig_fn, uint32_t prio,
                           uint8_t* stack_base, size_t stack_size,
                           const char* name) {
    if (!inout_handle) return false;
    if (prio >= kIdlePrio) prio = 14u;

    // Case 1: Instance already owns a slot (restarting a Terminated thread/actor)
    if (inout_handle->is_valid() && inout_handle->slot < TK_MAX_TASKS) {
        uint8_t slot = inout_handle->slot;
        for (;;) {
            uint32_t save = ::sched_lock();
            State st = g_slots[slot].state;
            if (st != State::Terminated && st != State::Free) {
                ::sched_unlock(save);
                return false;  // Already running
            }
            if (!is_slot_on_cpu_locked(slot)) {
                TaskSlot& s = g_slots[slot];
                s = TaskSlot{};
                s.fn = fn;
                s.arg = arg;
                s.sig_fn = sig_fn;
                s.name = name ? name : "thr";
                s.prio = prio;
                s.stack_base = stack_base;
                s.stack_size = stack_size;
                s.state = State::Ready;
                if (g_started) {
                    tk_port_trigger_pendsv();
                    __sev();
                }
                ::sched_unlock(save);
                return true;
            }
            ::sched_unlock(save);
            tk::yield();
        }
    }

    // Case 2: Allocate a free slot
    uint32_t save = ::sched_lock();
    for (uint32_t i = TK_NUM_CORES; i < TK_MAX_TASKS; ++i) {
        if (g_slots[i].state == State::Free && !is_slot_on_cpu_locked((uint8_t)i)) {
            TaskSlot& s = g_slots[i];
            s = TaskSlot{};
            s.fn = fn;
            s.arg = arg;
            s.sig_fn = sig_fn;
            s.name = name ? name : "thr";
            s.prio = prio;
            s.stack_base = stack_base;
            s.stack_size = stack_size;
            s.state = State::Ready;
            inout_handle->slot = (uint8_t)i;
            if (g_started) {
                tk_port_trigger_pendsv();
                __sev();
            }
            ::sched_unlock(save);
            return true;
        }
    }
    ::sched_unlock(save);
    return false;
}

bool send_task_signal(TaskHandle handle, uint32_t sig) {
    if (handle.slot >= TK_MAX_TASKS || sig == SIG_NONE) return false;
    uint32_t save = ::sched_lock();
    TaskSlot& s = g_slots[handle.slot];
    if (s.state == State::Free || s.state == State::Terminated) {
        ::sched_unlock(save);
        return false;
    }
    if (sig == SIG_STOP) {
        s.stop_requested = true;
    }
    if (s.pending_signal != SIG_STOP) {
        s.pending_signal = sig;
    }
    if (s.state == State::Sleeping || s.state == State::Blocked) {
        s.sleep_interrupted = true;
        s.state = State::Ready;
        if (g_started) {
            tk_port_trigger_pendsv();
            __sev();
        }
    }
    ::sched_unlock(save);
    return true;
}

void dispatch_pending_signal_current() {
    if (!g_started) return;
    uint32_t save = ::sched_lock();
    TaskSlot& cur = g_slots[g_cur[get_core_num()]];
    uint32_t sig = cur.pending_signal;
    cur.pending_signal = 0;
    SignalHandlerFn sig_fn = cur.sig_fn;
    void* arg = cur.arg;
    ::sched_unlock(save);

    if (sig != 0 && sig_fn != nullptr) {
        sig_fn(arg, sig);
    }
}

bool has_pending_signal_or_stop_locked(TaskHandle handle) {
    if (handle.slot >= TK_MAX_TASKS) return false;
    const TaskSlot& s = g_slots[handle.slot];
    return s.pending_signal != 0 || s.stop_requested;
}

bool is_task_stop_requested(TaskHandle handle) {
    if (handle.slot >= TK_MAX_TASKS) return true;
    uint32_t save = ::sched_lock();
    bool stopped = g_slots[handle.slot].stop_requested ||
                   g_slots[handle.slot].state == State::Terminated ||
                   g_slots[handle.slot].state == State::Free;
    ::sched_unlock(save);
    return stopped;
}

bool is_task_running(TaskHandle handle) {
    if (handle.slot >= TK_MAX_TASKS) return false;
    uint32_t save = ::sched_lock();
    State st = g_slots[handle.slot].state;
    bool running = (st != State::Free && st != State::Terminated) || is_slot_on_cpu_locked(handle.slot);
    ::sched_unlock(save);
    return running;
}

void join_task(TaskHandle handle) {
    if (!handle.is_valid() || handle.slot >= TK_MAX_TASKS) return;
    if (!g_started) return;
    if (g_cur[get_core_num()] == handle.slot) return;  // Avoid self-join

    for (;;) {
        uint32_t save = ::sched_lock();
        State st = g_slots[handle.slot].state;
        bool on_cpu = is_slot_on_cpu_locked(handle.slot);
        ::sched_unlock(save);

        if ((st == State::Terminated || st == State::Free) && !on_cpu) {
            break;
        }
        tk::sleep_ms(1);
    }
}

void release_task_slot(TaskHandle* inout_handle) {
    if (!inout_handle || !inout_handle->is_valid()) return;
    uint8_t slot = inout_handle->slot;
    if (slot >= TK_MAX_TASKS) {
        inout_handle->slot = 0xFF;
        return;
    }
    send_task_signal(*inout_handle, SIG_STOP);
    join_task(*inout_handle);

    uint32_t save = ::sched_lock();
    g_slots[slot] = TaskSlot{};  // State::Free
    ::sched_unlock(save);
    inout_handle->slot = 0xFF;
}

}  // namespace tk::detail

tk::TaskHandle tk::create(TaskFn fn, void* arg, uint32_t prio) {
    TaskHandle h{0xFF};
    detail::start_or_restart_task(&h, fn, arg, nullptr, prio, nullptr, 0, "dyn");
    return h;
}

static void core_start_routine(uint32_t core) {
    tk_port_init(TK_TICKS_PER_SEC);

    uint32_t save = sched_lock();
    uint32_t next = select_next(core);
    TaskSlot& nxt = g_slots[next];
    if (nxt.sp == 0) {
        nxt.sp = init_task_frame(next, nxt);
    }
    nxt.state = State::Running;
    memcpy(&tk_fpu_scratch[core], &nxt.fpu, sizeof(nxt.fpu));
    nxt.slice_left = kSliceTicks;
    g_rr_last[nxt.prio] = (uint8_t)next;
    g_cur[core] = (uint8_t)next;
    sched_unlock(save);

    tk_port_start(nxt.sp);  // never returns
    for (;;) __asm__ volatile("wfi");
}

static void core1_entry(void) {
    core_start_routine(1);
}

void tk::start() {
    if (g_started) for (;;) __asm__ volatile("wfi");
    g_started = true;

    ensure_sched_lock();

    // Core 0 Idle Task
    TaskSlot& idle0 = g_slots[0];
    idle0.fn = idle_fn;
    idle0.name = "idle0";
    idle0.prio = kIdlePrio;
    idle0.is_idle = true;
    idle0.state = State::Ready;

#if TK_NUM_CORES > 1
    // Core 1 Idle Task
    TaskSlot& idle1 = g_slots[1];
    idle1.fn = idle_fn;
    idle1.name = "idle1";
    idle1.prio = kIdlePrio;
    idle1.is_idle = true;
    idle1.state = State::Ready;

    // Launch Core 1
    multicore_launch_core1(core1_entry);
#endif

    // Launch Core 0
    core_start_routine(0);
}

void tk::yield() {
    uint32_t core = get_core_num();
    uint32_t save = sched_lock();
    g_slots[g_cur[core]].slice_left = 0;
    tk_port_trigger_pendsv();
    sched_unlock(save);
    detail::dispatch_pending_signal_current();
}

bool tk::sleep_ms(uint32_t ms) {
    uint64_t ticks = ((uint64_t)ms * TK_TICKS_PER_SEC + 999u) / 1000u;
    if (ticks < 1u) ticks = 1u;
    int32_t sleep_ticks = (int32_t)(ticks > 0x7FFFFFFFu ? 0x7FFFFFFFu : ticks);

    uint32_t save = sched_lock();
    TaskSlot& cur = g_slots[g_cur[get_core_num()]];
    if (cur.pending_signal != 0 || cur.stop_requested) {
        uint32_t sig = cur.pending_signal;
        cur.pending_signal = 0;
        cur.sleep_interrupted = false;
        detail::SignalHandlerFn sig_fn = cur.sig_fn;
        void* arg = cur.arg;
        sched_unlock(save);
        if (sig != 0 && sig_fn != nullptr) {
            sig_fn(arg, sig);
        }
        return false;
    }

    cur.sleep_interrupted = false;
    cur.state = State::Sleeping;
    cur.wake_tick = g_tick + sleep_ticks;
    tk_port_trigger_pendsv();
    sched_unlock(save);

    // Resumed after sleep or early signal wakeup (re-query core in case of SMP migration)
    save = sched_lock();
    TaskSlot& resumed = g_slots[g_cur[get_core_num()]];
    bool interrupted = resumed.sleep_interrupted;
    resumed.sleep_interrupted = false;
    uint32_t sig = resumed.pending_signal;
    resumed.pending_signal = 0;
    detail::SignalHandlerFn sig_fn = resumed.sig_fn;
    void* arg = resumed.arg;
    sched_unlock(save);

    if (sig != 0 && sig_fn != nullptr) {
        sig_fn(arg, sig);
    }
    return !interrupted && (sig == 0);
}

void tk::exit(int code) {
    (void)code;
    uint32_t core = get_core_num();
    uint32_t save = sched_lock();
    g_slots[g_cur[core]].state = State::Terminated;
    tk_port_trigger_pendsv();
    __sev();
    sched_unlock(save);
    for (;;) __asm__ volatile("wfi");
}

extern "C" void tk_task_return_trampoline(void) {
    tk::exit(0);
}

uint64_t tk::now_ms() {
    return (uint64_t)g_tick * 1000u / TK_TICKS_PER_SEC;
}

bool tk::lock() {
    uint32_t save = sched_lock();
    return save == 0; // PRIMASK was 0 (enabled)
}

void tk::unlock(bool was_enabled) {
    sched_unlock(was_enabled ? 0 : 1);
}

uint32_t tk::detail::sched_lock() {
    return ::sched_lock();
}

void tk::detail::sched_unlock(uint32_t save) {
    ::sched_unlock(save);
}

void tk::detail::block_current_task_locked() {
    uint32_t core = get_core_num();
    TaskSlot& cur = g_slots[g_cur[core]];
    cur.state = State::Blocked;
    tk_port_trigger_pendsv();
}

void tk::detail::wake_task_locked(TaskHandle handle) {
    if (handle.slot >= TK_MAX_TASKS) return;
    TaskSlot& s = g_slots[handle.slot];
    if (s.state == State::Blocked) {
        s.state = State::Ready;
        if (g_started) {
            tk_port_trigger_pendsv();
            __sev();
        }
    }
}

uint8_t tk::detail::current_task_slot() {
    return g_cur[get_core_num()];
}

static_assert(TK_MAX_TASKS <= sizeof(tk::task_mask_t) * 8, "TK_MAX_TASKS must fit in task_mask_t");

uint8_t tk::detail::find_highest_prio_waiter(task_mask_t wait_mask) {
    uint8_t best_slot = 0xFF;
    uint32_t best_prio = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < TK_MAX_TASKS; ++i) {
        if (wait_mask & (task_mask_t{1} << i)) {
            if (g_slots[i].prio < best_prio) {
                best_prio = g_slots[i].prio;
                best_slot = (uint8_t)i;
            }
        }
    }
    return best_slot;
}

void tk::preempt_disable() {
    uint32_t core = get_core_num();
    uint32_t save = sched_lock();
    ++g_preempt_disabled[core];
    sched_unlock(save);
}

void tk::preempt_enable() {
    uint32_t core = get_core_num();
    uint32_t save = sched_lock();
    if (g_preempt_disabled[core] > 0) {
        --g_preempt_disabled[core];
        if (g_preempt_disabled[core] == 0) {
            tk_port_trigger_pendsv();
        }
    }
    sched_unlock(save);
}
