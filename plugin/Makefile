# Build the QEMU TCG instruction-count and M4 timing-estimate plugins.
#
# We don't need any of QEMU's build system — the plugin is a standalone
# shared object loaded via dlopen. The header (qemu-plugin.h) is
# self-contained and vendored in this directory.
#
# We rely on visibility attributes in qemu-plugin.h (QEMU_PLUGIN_EXPORT
# expands to __attribute__((visibility("default")))) plus -fvisibility=hidden
# to keep everything else local. This is portable across GNU ld and Apple ld
# without needing a version script.

UNAME_S := $(shell uname -s)

# QEMU 9+ pulled glib into qemu-plugin.h. We don't use the GArray-returning
# function ourselves, but the include forces us to find glib's headers.
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0 2>/dev/null)

PLUGIN  := libinsn_count.so
CYCLE_PLUGIN := libm4_cycles.so
TIMING_TEST := test_m4_timing
CFLAGS  := -O2 -fPIC -fvisibility=hidden -Wall -Wextra -I. $(GLIB_CFLAGS)

# QEMU resolves plugin API symbols at dlopen time, so the .so links with
# undefined references. Apple ld64 rejects that by default; pass
# -undefined dynamic_lookup. GNU ld is permissive.
ifeq ($(UNAME_S),Darwin)
LDFLAGS := -shared -undefined dynamic_lookup
else
LDFLAGS := -shared
endif

all: $(PLUGIN) $(CYCLE_PLUGIN)

$(PLUGIN): insn_count.c qemu-plugin.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ insn_count.c

$(CYCLE_PLUGIN): m4_cycles.c m4_timing.c m4_timing.h qemu-plugin.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ m4_cycles.c m4_timing.c

$(TIMING_TEST): test_m4_timing.c m4_timing.c m4_timing.h
	$(CC) -O2 -Wall -Wextra -Werror -o $@ test_m4_timing.c m4_timing.c

.PHONY: test
test: $(TIMING_TEST)
	./$(TIMING_TEST)

clean:
	rm -f $(PLUGIN) $(CYCLE_PLUGIN) $(TIMING_TEST)

.PHONY: all clean
