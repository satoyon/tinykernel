#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"  // clock_num_t (clk_sys)
#include "hardware/structs/m33.h"     // m33_hw->fpccr (memory-mapped FPU control)
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"

#include "port_m33.h"

#define FPCCR_ASPEN (1u << 31)  // FPU_FPCCR_ASPEN_Pos per CMSIS core_cm33.h
#define FPCCR_LSPEN (1u << 30)  // FPU_FPCCR_LSPEN_Pos per CMSIS core_cm33.h

void tk_port_init(uint32_t ticks_per_sec) {
    // Disable automatic state preservation and lazy stacking via FPCCR:
    // the kernel explicitly saves/restores S0-S31 in software on every switch,
    // so hardware must never push VFP frames onto task stacks.
    m33_hw->fpccr = (uint32_t)(m33_hw->fpccr & ~(FPCCR_ASPEN | FPCCR_LSPEN));

    uint32_t clk = clock_get_hz(clk_sys);
    systick_hw->rvr = ((clk + ticks_per_sec / 2u) / ticks_per_sec - 1u) & 0x00FFFFFFu;

    // Run PendSV at the lowest priority (15 of 15 on M33) so peripheral ISRs
    // and SysTick always preempt it, ensuring context switches happen only when all
    // higher-priority interrupt handling is complete. SysTick runs at priority 14.
    // Byte offsets follow CMSIS __NVIC_SetPriority:
    // SHPR base = SCB+0x18, byte index = ((IRQn) & 0xF) - 4.
    uint8_t* shpr = (uint8_t*)&scb_hw->shpr[0];
    shpr[((int32_t)(-2) & 0xF) - 4] = 15u << 4;  // PendSV (priority 15: lowest)
    shpr[((int32_t)(-1) & 0xF) - 4] = 14u << 4;  // SysTick (priority 14)

    // NOTE: SysTick is intentionally NOT enabled here. tk_port_start() enables it
    // (ENABLE|TICKINT|CLKSOURCE) in asm, right before jumping into the first task,
    // so a tick can never preempt the rest of tk::start() while scheduler state and
    // MSP are not yet established.
}
