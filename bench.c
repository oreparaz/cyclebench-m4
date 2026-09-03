/* bench.c — workload driver for the cyclebench-m4 harness.
 *
 * Compiles two ways from the same source:
 *   - userspace Linux ELF: linked against glibc, parses argv[1..2]
 *   - bare-metal Cortex-M4 ELF (-DBAREMETAL): writes via ARM semihosting,
 *     iters/msg_len baked in at compile time via -DBENCH_ITERS / -DBENCH_MSGLEN
 *
 * The whole program is the "region of interest": instructions retired
 * between _start and exit() are what QEMU's libinsn plugin will count.
 * Subtract a baseline run with iters=0 to get the per-iteration cost.
 */
#include "sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef BAREMETAL
#  include "m4/semihost.h"
#  ifndef BENCH_ITERS
#    define BENCH_ITERS  1000u
#  endif
#  ifndef BENCH_MSGLEN
#    define BENCH_MSGLEN 64u
#  endif
#else
#  include <stdio.h>
#  include <stdlib.h>
#endif

#define MAX_MSG_LEN 8192
static uint8_t g_msg[MAX_MSG_LEN];

static void emit_digest(const uint8_t *d, size_t n) {
    static const char hex[] = "0123456789abcdef";
    char buf[2 * SHA256_DIGEST_SIZE + 1];
    for (size_t i = 0; i < n; i++) {
        buf[2 * i]     = hex[d[i] >> 4];
        buf[2 * i + 1] = hex[d[i] & 0xf];
    }
    buf[2 * n] = '\n';
#ifdef BAREMETAL
    bm_write(buf, 2 * n + 1);
#else
    fwrite(buf, 1, 2 * n + 1, stdout);
#endif
}

int main(int argc, char **argv) {
    size_t iters, msg_len;
#ifdef BAREMETAL
    (void)argc; (void)argv;
    iters   = BENCH_ITERS;
    msg_len = BENCH_MSGLEN;
#else
    iters   = (argc > 1) ? (size_t)strtoul(argv[1], NULL, 10) : 1000;
    msg_len = (argc > 2) ? (size_t)strtoul(argv[2], NULL, 10) : 64;
#endif
    if (msg_len > MAX_MSG_LEN) msg_len = MAX_MSG_LEN;

    /* Deterministic input so successive runs are bit-identical. */
    for (size_t i = 0; i < msg_len; i++) g_msg[i] = (uint8_t)(i * 31u + 7u);

    /* Chain digests so the optimizer can't hoist the call out of the loop. */
    uint8_t digest[SHA256_DIGEST_SIZE] = {0};
    for (size_t i = 0; i < iters; i++) {
        sha256(g_msg, msg_len, digest);
        if (msg_len >= SHA256_DIGEST_SIZE) {
            memcpy(g_msg, digest, SHA256_DIGEST_SIZE);
        } else if (msg_len > 0) {
            memcpy(g_msg, digest, msg_len);
        }
    }

    emit_digest(digest, SHA256_DIGEST_SIZE);
    return 0;
}
