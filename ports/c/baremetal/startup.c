// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — bare-metal Cortex-M startup (QEMU MPS2 boards).
// Copyright (C) 2026 James H. All rights reserved.
//
// No operating system: a vector table, FPU enable, then newlib's crt0
// (_start: zero .bss, run constructors, call main, exit via semihosting).
// Console output goes over ARM semihosting, so `qemu-system-arm
// -semihosting` prints exactly what the program prints.

#include <stdint.h>

extern uint32_t __stack_top;
extern void _start(void);

void Reset_Handler(void);
void Default_Handler(void);

void Default_Handler(void)
{
    for (;;)
    {
        /* any fault: hang so the harness timeout reports it */
    }
}

void Reset_Handler(void)
{
#if defined(__ARM_FP)
    /* CPACR: full access to CP10/CP11 (the FPU) before any FP instruction. */
    volatile uint32_t *const cpacr = (volatile uint32_t *)0xE000ED88U;
    *cpacr |= (0xFU << 20);
    __asm volatile("dsb\n\tisb" ::: "memory");
#endif
    _start();
    for (;;)
    {
    }
}

__attribute__((section(".vectors"), used))
static void (*const g_vectors[16])(void) = {
    (void (*)(void))(&__stack_top),
    Reset_Handler,
    Default_Handler,      /* NMI        */
    Default_Handler,      /* HardFault  */
    Default_Handler,      /* MemManage  */
    Default_Handler,      /* BusFault   */
    Default_Handler,      /* UsageFault */
    0, 0, 0, 0,
    Default_Handler,      /* SVCall     */
    Default_Handler,      /* DebugMon   */
    0,
    Default_Handler,      /* PendSV     */
    Default_Handler       /* SysTick    */
};
