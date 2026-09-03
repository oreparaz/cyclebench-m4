/* Cortex-M4 pessimistic, zero-wait-state instruction timing estimator. */
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include <glib.h>
#include <qemu-plugin.h>

#include "m4_timing.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

struct counters {
    uint64_t instructions;
    uint64_t cycles;
    uint64_t memory_extra;
    uint64_t branch_extra;
    uint64_t divide_extra;
    uint64_t fp_extra;
    uint64_t system_extra;
    uint64_t unbounded;
    uint64_t undecoded;
    uint64_t pending_conditional;
    uint64_t conditional_fallthrough;
};

struct tb_info {
    uint64_t start;
    uint64_t conditional_fallthrough;
};

static struct qemu_plugin_scoreboard *scoreboard;
static qemu_plugin_u64 instructions;
static qemu_plugin_u64 cycles;
static qemu_plugin_u64 memory_extra;
static qemu_plugin_u64 branch_extra;
static qemu_plugin_u64 divide_extra;
static qemu_plugin_u64 fp_extra;
static qemu_plugin_u64 system_extra;
static qemu_plugin_u64 unbounded;
static qemu_plugin_u64 undecoded;
static GPtrArray *translated_blocks;

static void on_tb_exec(unsigned int vcpu_index, void *userdata) {
    const struct tb_info *tb = userdata;
    struct counters *state = qemu_plugin_scoreboard_find(scoreboard, vcpu_index);

    if (state->pending_conditional && tb->start != state->conditional_fallthrough) {
        /* The branch was taken: use the worst documented pipeline refill P=3. */
        state->cycles += 3;
        state->branch_extra += 3;
    }
    state->pending_conditional = 0;
}

static void on_conditional_exec(unsigned int vcpu_index, void *userdata) {
    const struct tb_info *tb = userdata;
    struct counters *state = qemu_plugin_scoreboard_find(scoreboard, vcpu_index);

    state->pending_conditional = 1;
    state->conditional_fallthrough = tb->conditional_fallthrough;
}

static void add_if_nonzero(struct qemu_plugin_insn *insn, qemu_plugin_u64 counter,
                           uint64_t value) {
    if (value != 0) {
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_ADD_U64, counter, value);
    }
}

static void on_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    struct tb_info *tb_info = g_new0(struct tb_info, 1);
    size_t n = qemu_plugin_tb_n_insns(tb);
    (void)id;

    tb_info->start = qemu_plugin_tb_vaddr(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        char *disas = qemu_plugin_insn_disas(insn);
        struct m4_timing timing = m4_timing_for_disas(disas);

        add_if_nonzero(insn, instructions, 1);
        add_if_nonzero(insn, cycles, timing.cycles);
        add_if_nonzero(insn, memory_extra, timing.memory_extra);
        add_if_nonzero(insn, branch_extra, timing.branch_extra);
        add_if_nonzero(insn, divide_extra, timing.divide_extra);
        add_if_nonzero(insn, fp_extra, timing.fp_extra);
        add_if_nonzero(insn, system_extra, timing.system_extra);
        add_if_nonzero(insn, unbounded, timing.unbounded);
        add_if_nonzero(insn, undecoded, timing.undecoded);
        if (timing.conditional_branch) {
            tb_info->conditional_fallthrough =
                qemu_plugin_insn_vaddr(insn) + qemu_plugin_insn_size(insn);
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, on_conditional_exec, QEMU_PLUGIN_CB_NO_REGS, tb_info);
        }
        g_free(disas);
    }
    g_ptr_array_add(translated_blocks, tb_info);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, on_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, tb_info);
}

static void on_exit_cb(qemu_plugin_id_t id, void *userdata) {
    (void)id;
    (void)userdata;
    fprintf(stderr, "insns executed: %" PRIu64 "\n",
            qemu_plugin_u64_sum(instructions));
    fprintf(stderr, "m4 pessimistic core cycles: %" PRIu64 "\n",
            qemu_plugin_u64_sum(cycles));
    fprintf(stderr, "  memory extra: %" PRIu64 "\n",
            qemu_plugin_u64_sum(memory_extra));
    fprintf(stderr, "  branch extra: %" PRIu64 "\n",
            qemu_plugin_u64_sum(branch_extra));
    fprintf(stderr, "  divide extra: %" PRIu64 "\n",
            qemu_plugin_u64_sum(divide_extra));
    fprintf(stderr, "  fp extra: %" PRIu64 "\n",
            qemu_plugin_u64_sum(fp_extra));
    fprintf(stderr, "  system extra: %" PRIu64 "\n",
            qemu_plugin_u64_sum(system_extra));
    fprintf(stderr, "  unbounded instructions: %" PRIu64 "\n",
            qemu_plugin_u64_sum(unbounded));
    fprintf(stderr, "  undecoded instructions: %" PRIu64 "\n",
            qemu_plugin_u64_sum(undecoded));
    g_ptr_array_free(translated_blocks, TRUE);
    qemu_plugin_scoreboard_free(scoreboard);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv) {
    (void)info;
    (void)argc;
    (void)argv;
    scoreboard = qemu_plugin_scoreboard_new(sizeof(struct counters));
    translated_blocks = g_ptr_array_new_with_free_func(g_free);
    instructions = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, instructions);
    cycles = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, cycles);
    memory_extra = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, memory_extra);
    branch_extra = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, branch_extra);
    divide_extra = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, divide_extra);
    fp_extra = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, fp_extra);
    system_extra = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, system_extra);
    unbounded = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, unbounded);
    undecoded = qemu_plugin_scoreboard_u64_in_struct(scoreboard, struct counters, undecoded);
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans);
    qemu_plugin_register_atexit_cb(id, on_exit_cb, NULL);
    return 0;
}
