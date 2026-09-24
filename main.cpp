#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "tinykernel/kernel.hpp"
#include "tinykernel/actor.hpp"
#include "tinykernel/sync.hpp"

#include "pico/multicore.h"

// Atomic printf helper: stdio is shared, so wrap each call with the kernel lock.
#define P(...)                                  \
    do {                                       \
        bool _lk = tk::lock();                 \
        printf("[C%u] ", (unsigned)get_core_num()); \
        printf(__VA_ARGS__);                   \
        tk::unlock(_lk);                       \
    } while (0)

// ---------------------------------------------------------------------------
// Actor demonstration: Receiver Actor waiting for messages.
// It consumes 0 CPU cycles while waiting, and immediately wakes up on post().
enum AppMsgId : uint32_t {
    MSG_LED_TOGGLE = 1,
    MSG_PING       = 2,
};

class LedActor : public tk::Actor<8> {
protected:
    void on_start() override {
        // Hardware initialization within task context
        gpio_init(25);
        gpio_set_dir(25, GPIO_OUT);
        gpio_put(25, 0);
        P("LedActor: initialized GPIO 25 in on_start()\n");
    }

    void on_message(const tk::Message& msg) override {
        if (msg.id == MSG_LED_TOGGLE) {
            bool state = !gpio_get(25);
            gpio_put(25, state);
            P("LedActor: received MSG_LED_TOGGLE (seq=%lu) -> LED %s, now=%llu ms\n",
              (unsigned long)msg.arg, state ? "ON" : "OFF", (unsigned long long)tk::now_ms());
        } else {
            P("LedActor: received unknown msg id=%lu, arg=%lu\n",
              (unsigned long)msg.id, (unsigned long)msg.arg);
        }
    }
};

static LedActor g_led_actor;

// Sender task: periodically posts messages to LedActor
static void task_sender(void* arg) {
    (void)arg;
    uint32_t seq = 0;
    for (;;) {
        tk::sleep_ms(600);
        P("Sender: posting MSG_LED_TOGGLE seq=%lu...\n", (unsigned long)seq);
        g_led_actor.post(MSG_LED_TOGGLE, seq++);
    }
}

// ---------------------------------------------------------------------------
// Mutex demonstration: Two tasks contend for a shared counter protected by Mutex
static tk::Mutex g_counter_mutex;
static uint32_t g_shared_counter = 0;

static void task_mutex_worker(void* arg) {
    const char* tag = static_cast<const char*>(arg);
    for (;;) {
        {
            tk::LockGuard<tk::Mutex> guard(g_counter_mutex);
            g_shared_counter++;
            P("[%s] holds mutex: counter=%lu\n", tag, (unsigned long)g_shared_counter);
            volatile uint32_t n = 0;
            while (n < 100000u) ++n;
        }  // guard destructor automatically calls unlock()
        tk::sleep_ms(350);
    }
}

// ---------------------------------------------------------------------------
// Semaphore demonstration: Signaler wakes up Waiter task
static tk::BinarySemaphore g_demo_sem(0, 1);  // Initially 0 (blocked)

static void task_sem_waiter(void* arg) {
    (void)arg;
    for (;;) {
        g_demo_sem.acquire();  // Blocks until release() is called
        P("SemWaiter: >>> WOKEN UP by semaphore! now=%llu ms\n", (unsigned long long)tk::now_ms());
    }
}

static void task_sem_signaler(void* arg) {
    (void)arg;
    for (;;) {
        tk::sleep_ms(1000);
        P("SemSignaler: releasing semaphore...\n");
        g_demo_sem.release();
    }
}

// ---------------------------------------------------------------------------
// Static task: FPU self-check. Runs the same deterministic float/double sequence
// twice; any corruption of FPU state across context switches shows up as a
// mismatch or a drifting result.
static float fpu_run_f(float v) {
    for (int i = 0; i < 10000; ++i) v = v * 1.000001f + 0.5f;
    return v;
}

static double fpu_run_d(double v) {
    for (long i = 0; i < 10000; ++i) v = v * 1.0000000002 + 0.5;
    return v;
}

void task_fpu(void* arg) {
    (void)arg;
    for (;;) {
        float fa = fpu_run_f(1.5f);
        float fb = fpu_run_f(1.5f);
        double da = fpu_run_d(2.5);
        double db = fpu_run_d(2.5);
        if (fa != fb || da != db) {
            P("FPU FAIL  f: %8.6f vs %8.6f   d: %.9f vs %.9f\n", (double)fa, (double)fb, da, db);
        } else {
            P("FPU ok    f: %8.6f   d: %.9f\n", (double)fa, da);
        }
        tk::sleep_ms(500);
    }
}
// ---------------------------------------------------------------------------
// Preemption Guard demonstration: Simulates an atomic peripheral transaction (e.g. I2C)
// During PreemptGuard, no task switch occurs even if time slices expire or interrupts fire.
static void task_preempt_demo(void* arg) {
    (void)arg;
    for (;;) {
        tk::sleep_ms(2000);
        {
            tk::PreemptGuard guard;
            uint64_t t0 = tk::now_ms();
            P("PreemptDemo: >>> START atomic section (preemption disabled), t0=%llu ms\n", (unsigned long long)t0);
            // Busy wait 60ms (exceeds the 20ms time slice, but preemption is blocked!)
            // Meanwhile SysTick interrupt keeps running, so now_ms() keeps updating!
            while (tk::now_ms() - t0 < 60) {
            }
            P("PreemptDemo: <<< END atomic section, now=%llu ms (SysTick kept advancing!)\n", (unsigned long long)tk::now_ms());
        }  // guard destructor calls preempt_enable() -> deferred task switches happen now!
    }
}

int main() {
    stdio_init_all();

    printf("tinykernel: registering dynamic tasks...\n");
    g_led_actor.start(tk::PRIO_NORMAL);
    auto h_sender  = tk::create(task_sender, nullptr, tk::PRIO_NORMAL);
    auto h_mtx_a   = tk::create(task_mutex_worker, const_cast<char*>("Mtx-A"), tk::PRIO_NORMAL);
    auto h_mtx_b   = tk::create(task_mutex_worker, const_cast<char*>("Mtx-B"), tk::PRIO_NORMAL);
    auto h_waiter  = tk::create(task_sem_waiter, nullptr, tk::PRIO_HIGH);
    auto h_sig     = tk::create(task_sem_signaler, nullptr, tk::PRIO_NORMAL);
    auto h_preempt = tk::create(task_preempt_demo, nullptr, tk::PRIO_NORMAL);

    if (!g_led_actor.handle().is_valid() || !h_sender.is_valid() ||
        !h_mtx_a.is_valid() || !h_mtx_b.is_valid() ||
        !h_waiter.is_valid() || !h_sig.is_valid() || !h_preempt.is_valid()) {
        printf("tinykernel: task creation FAILED\n");
        for (;;) {
        }
    }

    printf("tinykernel: starting scheduler (preemptive, dual-core M33)\n");
    tk::start();  // never returns
}
