#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "tinykernel/kernel.hpp"

// Atomic printf helper: stdio is shared, so wrap each call with the kernel lock.
#define P(...)                                  \
    do {                                       \
        bool _lk = tk::lock();                 \
        printf(__VA_ARGS__);                   \
        tk::unlock(_lk);                       \
    } while (0)

// ---------------------------------------------------------------------------
// Static task (registered at link time via TK_TASK): toggles the onboard LED.
void task_led(void* arg) {
    (void)arg;
    for (;;) {
        gpio_put(25, !gpio_get(25));
        tk::sleep_ms(200);
    }
}
TK_TASK(task_led, tk::PRIO_NORMAL)

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
    auto h_hi = tk::create(task_hi, nullptr, tk::PRIO_HIGH);
    auto h_dy = tk::create(task_dying, nullptr, tk::PRIO_LOW);
    auto h_pr = tk::create(task_print, nullptr, tk::PRIO_NORMAL);
    if (h_hi.slot == 0xFF || h_dy.slot == 0xFF || h_pr.slot == 0xFF) {
        printf("tinykernel: task creation FAILED\n");
        for (;;) {
        }
    }

    printf("tinykernel: starting scheduler (preemptive, core M33 only)\n");
    tk::start();  // never returns
}
