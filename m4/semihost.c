/* m4/semihost.c — minimal ARM Semihosting stubs for the bare-metal bench.
 *
 * Calling convention: r0 = op, r1 = arg pointer, BKPT 0xab, r0 = result.
 * We only need writec and exit; bench output is small so per-char is fine.
 */
#include "semihost.h"
#include <stdint.h>

#define SYS_WRITEC  0x03
#define SYS_EXIT    0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026u

static int semihost_call(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile ("bkpt 0xab"
                      : "+r"(r0)
                      : "r"(r1)
                      : "memory");
    return r0;
}

void bm_write(const char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];
        semihost_call(SYS_WRITEC, &c);
    }
}

void __attribute__((noreturn)) bm_exit(int rc) {
    /* ARMv7-M extended SYS_EXIT takes a {reason, code} pair. */
    uint32_t args[2] = { ADP_STOPPED_APPLICATION_EXIT, (uint32_t)rc };
    semihost_call(SYS_EXIT, args);
    for (;;) { /* qemu has detached by now */ }
}
