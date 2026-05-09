/* insn_count.c — minimal QEMU TCG plugin: prints total guest instructions.
 *
 * Hooks every translated TB and registers an inline ADD_U64 against a single
 * counter, so per-TB cost is one memory add. Prints the total in atexit.
 *
 * Build:  see Makefile in this directory (libinsn_count.so).
 * Use:    qemu-arm -plugin ./libinsn_count.so -d plugin ./bench …
 *
 * Single-threaded only — qemu-arm user-mode is single-threaded, so the
 * non-atomic inline add is fine.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t insn_count;

static void on_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    (void)id;
    size_t n = qemu_plugin_tb_n_insns(tb);
    qemu_plugin_register_vcpu_tb_exec_inline(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, &insn_count, (uint64_t)n);
}

static void on_exit_cb(qemu_plugin_id_t id, void *userdata) {
    (void)id; (void)userdata;
    fprintf(stderr, "insns retired: %" PRIu64 "\n", insn_count);
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id,
                        const qemu_info_t *info,
                        int argc, char **argv) {
    (void)info; (void)argc; (void)argv;
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans);
    qemu_plugin_register_atexit_cb(id, on_exit_cb, NULL);
    return 0;
}
