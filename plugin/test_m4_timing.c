#include "m4_timing.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(const char *disas, uint64_t cycles, uint64_t memory,
                   uint64_t branch, uint64_t divide, uint64_t fp) {
    struct m4_timing got = m4_timing_for_disas(disas);
    if (got.cycles != cycles || got.memory_extra != memory ||
        got.branch_extra != branch || got.divide_extra != divide ||
        got.fp_extra != fp) {
        fprintf(stderr,
                "%s: got cycles=%" PRIu64 " mem=%" PRIu64
                " branch=%" PRIu64 " div=%" PRIu64 " fp=%" PRIu64 "\n",
                disas, got.cycles, got.memory_extra, got.branch_extra,
                got.divide_extra, got.fp_extra);
        exit(1);
    }
}

int main(void) {
    if (m4_timing_for_disas("0x00000000").undecoded != 1) return 1;
    expect("adds r0, r1, r2", 1, 0, 0, 0, 0);
    expect("umull r0, r1, r2, r3", 1, 0, 0, 0, 0);
    expect("udiv r0, r1, r2", 12, 0, 0, 11, 0);
    expect("bne.w 0x100", 1, 0, 0, 0, 0);
    if (m4_timing_for_disas("bne.w 0x100").conditional_branch != 1) return 1;
    expect("tbb [r0, r1]", 5, 0, 4, 0, 0);
    expect("ldr r0, [r1, #4]", 2, 1, 0, 0, 0);
    expect("ldr r0, [pc, #4]", 2, 1, 0, 0, 0);
    expect("ldr pc, [r1]", 5, 1, 3, 0, 0);
    expect("push {r4-r7, lr}", 6, 5, 0, 0, 0);
    expect("pop {r4-r7, pc}", 9, 5, 3, 0, 0);
    expect("ldmia.w r0!, {r1-r3}", 4, 3, 0, 0, 0);
    expect("vdiv.f32 s0, s1, s2", 15, 0, 0, 0, 14);
    expect("vldr d0, [pc, #8]", 3, 2, 0, 0, 0);
    expect("vpush {d8-d9}", 5, 4, 0, 0, 0);
    return 0;
}
