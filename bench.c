/* bench.c — workload driver for the Cortex-M4 cycle-estimation harness.
 *
 * The whole program is the "region of interest": instructions retired
 * between _start and exit() are what QEMU's libinsn plugin will count.
 * We keep host-side I/O minimal so it doesn't dominate the count for
 * small workloads.
 *
 *   ./bench [iters] [msg_len]
 *
 *     iters    — how many sha256() calls to run         (default 1000)
 *     msg_len  — size of the input buffer to hash       (default 64)
 *
 * The final digest is printed to stdout as a sanity check; that I/O
 * happens after the loop, so it's a fixed tail in the instruction count
 * (subtract a baseline run with iters=0 to get the per-iteration cost).
 */
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    size_t iters   = (argc > 1) ? (size_t)strtoul(argv[1], NULL, 10) : 1000;
    size_t msg_len = (argc > 2) ? (size_t)strtoul(argv[2], NULL, 10) : 64;

    /* Deterministic input so successive runs are bit-identical. */
    uint8_t *msg = (uint8_t *)malloc(msg_len ? msg_len : 1);
    for (size_t i = 0; i < msg_len; i++) msg[i] = (uint8_t)(i * 31u + 7u);

    /* Chain digests so the optimizer can't hoist the call out of the loop. */
    uint8_t digest[SHA256_DIGEST_SIZE] = {0};
    for (size_t i = 0; i < iters; i++) {
        sha256(msg, msg_len, digest);
        if (msg_len >= SHA256_DIGEST_SIZE) {
            memcpy(msg, digest, SHA256_DIGEST_SIZE);
        } else if (msg_len > 0) {
            memcpy(msg, digest, msg_len);
        }
    }

    /* Print the final digest; stops dead-code elimination and gives a
     * cheap correctness check across builds. */
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) printf("%02x", digest[i]);
    putchar('\n');

    free(msg);
    return 0;
}
