// context.S - Cortex-M33 port for tinykernel (RP2350 / Pico 2)
//
// Stack architecture:
//   - User tasks run in Thread mode using PSP (Process Stack Pointer).
//   - Kernel, SysTick, and exceptions run in Handler mode using MSP (Main Stack Pointer).
//
// Task stack layout on PSP (low -> high addresses):
//   [sp + 0  .. sp + 28] : R4-R11   (8 words pushed/popped by software in context.S)
//   [sp + 32 .. sp + 60] : Core8    (R0-R3, R12, LR, PC, xPSR automatically saved/restored by hardware)
//
// FPU state (S0-S31) is moved between hardware registers and tk_fpu_scratch by vstm/vldm.
// FPCCR.ASPEN and FPCCR.LSPEN are both disabled in port_m33.c, so hardware never pushes
// VFP frames onto task stacks.

    .syntax unified
    .thumb

    .extern tk_switch_context
    .extern tk_fpu_scratch
    .extern tk_task_return_trampoline
    .global isr_pendsv
    .global tk_port_start
    .global tk_port_make_frame

// clobbers r2, r3
.macro save_fpu_to_scratch
    ldr     r3, =0xd0000000           // SIO_BASE
    ldr     r3, [r3]                  // CPUID (0 or 1)
    lsls    r3, r3, #7                // core * 128 (sizeof(FpuCtx))
    ldr     r2, =tk_fpu_scratch
    adds    r3, r2, r3                // &tk_fpu_scratch[core]
    vstmia  r3!, {d0-d7}
    vstmia  r3!, {d8-d15}
.endm

// clobbers r2, r3
.macro restore_fpu_from_scratch
    ldr     r3, =0xd0000000           // SIO_BASE
    ldr     r3, [r3]                  // CPUID (0 or 1)
    lsls    r3, r3, #7                // core * 128 (sizeof(FpuCtx))
    ldr     r2, =tk_fpu_scratch
    adds    r3, r2, r3                // &tk_fpu_scratch[core]
    vldmia  r3!, {d0-d7}
    vldmia  r3!, {d8-d15}
.endm

// PendSV Handler: dedicated context switch handler running at the lowest priority.
// Hardware has already pushed the Core8 frame to the current task's PSP.
// Handler mode automatically runs on MSP.
    .type isr_pendsv,%function
    .thumb_func
isr_pendsv:
    // 1. Get current task stack pointer (PSP)
    mrs     r0, psp

    // 2. Save R4-R11 onto current task's stack (PSP)
    stmdb   r0!, {r4-r11}

    // 3. Save FPU state to global scratch buffer
    save_fpu_to_scratch

    // 4. Preserve EXC_RETURN (lr) on MSP (push 2 registers for 8-byte stack alignment)
    push    {r0, lr}

    // 5. Call scheduler: r0 = cur_sp
    // Returns next task's SP in r0.
    bl      tk_switch_context

    // 6. Restore FPU state for next task
    restore_fpu_from_scratch

    // 7. Restore EXC_RETURN (lr) from MSP
    pop     {r1, lr}

    // 8. Restore R4-R11 from new task's stack
    ldmia   r0!, {r4-r11}

    // 9. Update PSP with new task's SP (points to Core8 hardware frame)
    msr     psp, r0

    // 10. Exception return: hardware pops Core8 frame from PSP and returns to Thread mode
    bx      lr

// Start the first task.
// r0 = initial SP from tk_port_make_frame (pointing at R4-R11 area).
    .type tk_port_start,%function
    .thumb_func
tk_port_start:
    // 1. Pop initial R4-R11 (zeros)
    ldmia   r0!, {r4-r11}

    // 2. Set PSP to the start of the Core8 hardware frame
    msr     psp, r0

    // 3. Restore FPU state
    restore_fpu_from_scratch

    // 4. Switch Thread mode to use PSP (CONTROL.SPSEL = 1)
    mrs     r1, control
    movs    r2, #2
    orrs    r1, r2
    msr     control, r1
    isb

    // 5. Enable SysTick (ENABLE | TICKINT | CLKSOURCE)
    ldr     r1, =0xE000E010           // SysTick CTRL
    movs    r3, #7
    str     r3, [r1]

    // 6. Pop Core8 frame from PSP and start task:
    // Pop R0-R3 (arg in R0), R12, LR (trampoline)
    pop     {r0-r3, r12, lr}
    // Load PC (entry fn) and discard xPSR (+8 bytes total)
    ldr     pc, [sp], #8
    .pool

// Build a task's initial stack frame on its stack buffer.
//   r0 = base of stack buffer, r1 = size in bytes (multiple of 8),
//   r2 = entry fn, r3 = arg
//
// Stack Layout (low -> high):
//   [r0 + 0  .. r0 + 28] : R4-R11 (zeros, 32 bytes)
//   [r0 + 32]            : R0 (arg)
//   [r0 + 36 .. r0 + 48] : R1-R3, R12 (zeros)
//   [r0 + 52]            : LR (tk_task_return_trampoline)
//   [r0 + 56]            : PC (entry fn)
//   [r0 + 60]            : xPSR (0x01000000, Thumb bit set)
//
// Returns: Initial SP in r0 (pointing at R4).
    .type tk_port_make_frame,%function
    .thumb_func
tk_port_make_frame:
    adds    r0, r0, r1                // r0 = top of stack allocation
    subs    r0, #64                   // r0 = SP pointing at R4

    // Zero R4-R11
    movs    r1, #0
    str     r1, [r0, #0]              // R4
    str     r1, [r0, #4]              // R5
    str     r1, [r0, #8]              // R6
    str     r1, [r0, #12]             // R7
    str     r1, [r0, #16]             // R8
    str     r1, [r0, #20]             // R9
    str     r1, [r0, #24]             // R10
    str     r1, [r0, #28]             // R11

    // Core8 frame
    str     r3, [r0, #32]             // R0 = arg
    str     r1, [r0, #36]             // R1 = 0
    str     r1, [r0, #40]             // R2 = 0
    str     r1, [r0, #44]             // R3 = 0
    str     r1, [r0, #48]             // R12 = 0

    ldr     r1, =tk_task_return_trampoline
    orrs    r1, #1
    str     r1, [r0, #52]             // LR = trampoline (Thumb)

    orrs    r2, #1
    str     r2, [r0, #56]             // PC = entry fn (Thumb)

    movs    r1, #0x01
    lsls    r1, #24                   // 0x01000000 (Thumb bit)
    str     r1, [r0, #60]             // xPSR

    bx      lr
