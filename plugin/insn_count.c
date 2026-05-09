/* insn_count.c — minimal QEMU TCG plugin: prints total guest instructions.
 *
 * For each translated TB we register an inline ADD_U64 against a per-vcpu
 * scoreboard slot. At exit we sum across all vcpus. Per-TB host-side cost
 * is one memory add, so the plugin runs at ~2 Ginsns/sec.
 *
 * Build:  see Makefile in this directory (libinsn_count.so).
 * Use:    qemu-system-arm -plugin ./libinsn_count.so -d plugin -kernel ...
 *         qemu-arm         -plugin ./libinsn_count.so -d plugin ./bench …
 *
 * Targets the QEMU 11.0 plugin API (`per_vcpu` inline ops + scoreboards).
 * The pre-9.0 inline-add API was removed; this is the maintained successor.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static struct qemu_plugin_scoreboard *sb;
static qemu_plugin_u64               insn_count;

static void on_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    (void)id;
    size_t n = qemu_plugin_tb_n_insns(tb);
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, insn_count, (uint64_t)n);
}

static void on_exit_cb(qemu_plugin_id_t id, void *userdata) {
    (void)id; (void)userdata;
    uint64_t total = qemu_plugin_u64_sum(insn_count);
    fprintf(stderr, "insns retired: %" PRIu64 "\n", total);
    qemu_plugin_scoreboard_free(sb);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id,
                        const qemu_info_t *info,
                        int argc, char **argv) {
    (void)info; (void)argc; (void)argv;
    sb = qemu_plugin_scoreboard_new(sizeof(uint64_t));
    insn_count = qemu_plugin_scoreboard_u64(sb);
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans);
    qemu_plugin_register_atexit_cb(id, on_exit_cb, NULL);
    return 0;
}
