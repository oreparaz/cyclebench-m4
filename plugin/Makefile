# Build a tiny QEMU TCG plugin (libinsn_count.so) using QEMU 8.2's plugin API.
#
# We don't need any of QEMU's build system — the plugin is a standalone
# shared object linked against libdl. The header (qemu-plugin.h) is
# self-contained and ships in this directory.

PLUGIN  := libinsn_count.so
CFLAGS  := -O2 -fPIC -Wall -Wextra -I.
LDFLAGS := -shared -Wl,--version-script=plugin.ld

all: $(PLUGIN)

# Symbols the plugin must export so QEMU can find them at dlopen time.
plugin.ld:
	@printf '{\n  global: qemu_plugin_install; qemu_plugin_version;\n  local: *;\n};\n' > $@

$(PLUGIN): insn_count.c qemu-plugin.h plugin.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ insn_count.c

clean:
	rm -f $(PLUGIN) plugin.ld

.PHONY: all clean
