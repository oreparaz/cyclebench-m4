/* m4/startup.c — vector table + Reset_Handler for Cortex-M4 bare-metal.
 *
 * Cortex-M cores load the initial SP from address 0x0 and jump to the reset
 * vector at 0x4. Everything between text/data init and main() is here.
 *
 * We don't need NVIC fault routing for the bench — anything that traps just
 * spins forever and QEMU prints the lockup, which is what we want during
 * development anyway.
 */
#include <stdint.h>

extern uint32_t _data_load_start, _data_start, _data_end;
extern uint32_t _bss_start,  _bss_end;
extern uint32_t _stack_top;
extern int  main(int, char **);
extern void bm_exit(int) __attribute__((noreturn));

void Reset_Handler(void)   __attribute__((noreturn));
void Default_Handler(void) __attribute__((noreturn));

/* The first 16 entries of the M-profile vector table. We only wire SP and
 * Reset; everything else parks at Default_Handler (infinite loop). */
__attribute__((section(".vectors"), used))
void (* const g_vectors[16])(void) = {
    (void (*)(void)) &_stack_top, /* 0  Initial SP            */
    Reset_Handler,                /* 1  Reset                 */
    Default_Handler,              /* 2  NMI                   */
    Default_Handler,              /* 3  HardFault             */
    Default_Handler,              /* 4  MemManage             */
    Default_Handler,              /* 5  BusFault              */
    Default_Handler,              /* 6  UsageFault            */
    0, 0, 0, 0,                   /* 7-10 reserved            */
    Default_Handler,              /* 11 SVCall                */
    Default_Handler,              /* 12 DebugMon              */
    0,                            /* 13 reserved              */
    Default_Handler,              /* 14 PendSV                */
    Default_Handler,              /* 15 SysTick               */
};

/* CPACR — Coprocessor Access Control Register. Bits [23:20] gate CP10/CP11
 * (the FPU). Cortex-M4 leaves them clear at reset, so any VFP instruction
 * traps to UsageFault. gcc emits VLDR.64 for plain 8-byte loads at -O2
 * (e.g. cloning the K[] table), so we must enable the FPU before main(). */
#define CPACR_ADDR 0xE000ED88u

void Reset_Handler(void) {
    /* Enable CP10 + CP11 = full FPU access for both privileged and unpriv. */
    *(volatile uint32_t *)CPACR_ADDR |= (0xFu << 20);
    __asm__ volatile ("dsb; isb" ::: "memory");

    /* Copy .data from its load address (in flash) to its run address (in RAM). */
    uint32_t *src = &_data_load_start;
    uint32_t *dst = &_data_start;
    while (dst < &_data_end) *dst++ = *src++;

    /* Zero .bss. */
    for (uint32_t *p = &_bss_start; p < &_bss_end; p++) *p = 0;

    int rc = main(0, 0);
    bm_exit(rc);
}

void Default_Handler(void) {
    for (;;) { __asm__ volatile ("wfi"); }
}
