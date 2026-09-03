#ifndef CYCLEBENCH_M4_TIMING_H
#define CYCLEBENCH_M4_TIMING_H

#include <stdint.h>

/*
 * Zero-wait-state Cortex-M4 timing estimate for one disassembled instruction.
 * The estimate deliberately chooses the maximum documented instruction latency
 * where the core timing is variable (for example UDIV/SDIV and pipeline
 * refills). Memory-system wait states are outside this model.
 */
struct m4_timing {
    uint64_t cycles;
    uint64_t memory_extra;
    uint64_t branch_extra;
    uint64_t divide_extra;
    uint64_t fp_extra;
    uint64_t system_extra;
    uint64_t unbounded;
    uint64_t undecoded;
    uint64_t conditional_branch;
};

struct m4_timing m4_timing_for_disas(const char *disas);

#endif
