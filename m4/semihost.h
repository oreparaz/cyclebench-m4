/* Tiny ARM Semihosting helpers. Bare-metal builds only. */
#ifndef CYCLEBENCH_SEMIHOST_H
#define CYCLEBENCH_SEMIHOST_H

#include <stddef.h>

/* Write n bytes to the host's stdout via SYS_WRITEC. */
void bm_write(const char *buf, size_t n);

/* Terminate the simulation; QEMU returns rc to the host shell. */
void __attribute__((noreturn)) bm_exit(int rc);

#endif
