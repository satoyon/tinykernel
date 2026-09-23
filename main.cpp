#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "tinykernel/kernel.hpp"
#include "tinykernel/actor.hpp"

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
        tk::sleep_ms(500);
        P("Sender: posting MSG_LED_TOGGLE seq=%lu...\n", (unsigned long)seq);
        g_led_actor.post(MSG_LED_TOGGLE, seq++);
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
        tk::sleep_ms(300);
    }
}
TK_TASK(task_fpu, tk::PRIO_NORMAL)

// ---------------------------------------------------------------------------
// Dynamic task (higher priority): busy-spins for a few ms to demonstrate that it
// preempts the lower-priority tasks immediately.
static void task_hi(void* arg) {
    (void)arg;
    for (;;) {
        volatile uint32_t n = 0;
        while (n < 500000u) ++n;
        P("HI   prio=%u busy-spin done, now=%llu ms\n", (unsigned)tk::PRIO_HIGH, (unsigned long long)tk::now_ms());
        tk::sleep_ms(700);
    }
}

// ---------------------------------------------------------------------------
// Dynamic task: prints a few times and then exits.
static void task_dying(void* arg) {
    (void)arg;
    for (int i = 1; i <= 3; ++i) {
        P("DYING %d (now=%llu ms)\n", i, (unsigned long long)tk::now_ms());
        tk::sleep_ms(250);
    }
    P("DYING calling tk::exit()\n");
    tk::exit(0);
}

// ---------------------------------------------------------------------------
// Dynamic task: plain periodic printer at normal priority.
static void task_print(void* arg) {
    (void)arg;
    uint32_t count = 0;
    for (;;) {
        P("A    #%lu now=%llu ms\n", (unsigned long)count++, (unsigned long long)tk::now_ms());
        tk::sleep_ms(150);
    }
}

int main() {
    stdio_init_all();
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);

    printf("tinykernel: registering dynamic tasks...\n");
    g_led_actor.start(tk::PRIO_NORMAL);
    auto h_sender = tk::create(task_sender, nullptr, tk::PRIO_NORMAL);
    auto h_hi = tk::create(task_hi, nullptr, tk::PRIO_HIGH);
    auto h_dy = tk::create(task_dying, nullptr, tk::PRIO_LOW);
    auto h_pr = tk::create(task_print, nullptr, tk::PRIO_NORMAL);
    if (!g_led_actor.handle().is_valid() || !h_sender.is_valid() || !h_hi.is_valid() || !h_dy.is_valid() || !h_pr.is_valid()) {
        printf("tinykernel: task creation FAILED\n");
        for (;;) {
        }
    }

    printf("tinykernel: starting scheduler (preemptive, dual-core M33)\n");
    tk::start();  // never returns
}
