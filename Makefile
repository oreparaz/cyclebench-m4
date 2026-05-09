# cyclebench-m4 — instruction counting for Cortex-M4 C code via QEMU.
#
# Two measurement paths, both using a small in-tree TCG plugin
# (plugin/libinsn_count.so) that asks QEMU to emit an inline ADD_U64
# per translated block:
#
#   make count          architecturally-pure: bare-metal Cortex-M4 .elf
#                       on qemu-system-arm -M mps2-an386 (works on Linux
#                       and macOS, both via brew/apt or a local QEMU build).
#
#   make count-linux    cross-check: ARM userspace ELF on qemu-arm.
#                       Faster to iterate (no startup), Linux-only.
#                       Uses -mcpu=cortex-a7 -mthumb because glibc startup
#                       is A-profile and won't link against M-profile objects.
#
# Once-only setup:
#   make qemu           builds local qemu-arm + qemu-system-arm with
#                       --enable-plugins (Ubuntu's packages don't ship them).

# ---- toolchains ----
CC_HOST    ?= cc
CC_ARM     ?= arm-linux-gnueabihf-gcc
SIZE       ?= arm-linux-gnueabihf-size
OBJDUMP    ?= arm-linux-gnueabihf-objdump
CC_M4      ?= arm-none-eabi-gcc
SIZE_M4    ?= arm-none-eabi-size
OBJDUMP_M4 ?= arm-none-eabi-objdump

# Default to the locally-built plugin-enabled QEMU (produced by `make qemu`).
# Override with environment variables on systems where qemu has plugins
# baked into the distro package, e.g. macOS Homebrew:
#   QEMU_SYSTEM=qemu-system-arm make count
QEMU_USER_LOCAL   := qemu/build/qemu-arm
QEMU_SYSTEM_LOCAL := qemu/build/qemu-system-arm
QEMU_USER         ?= $(QEMU_USER_LOCAL)
QEMU_SYSTEM       ?= $(QEMU_SYSTEM_LOCAL)

# ---- flags ----
CFLAGS_COMMON  = -O2 -g -Wall -Wextra -fno-builtin -fno-stack-protector

# Userspace ARM build: tune for M4 if the linker accepts it (it usually
# won't because glibc startup is A-profile), otherwise fall back to A7
# which has the same Thumb-2 + FPv4-SP-d16 instruction set.
ARMFLAGS          = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
ARMFLAGS_FALLBACK = -mcpu=cortex-a7 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb

# Bare-metal Cortex-M4: the architecturally-pure path.
M4FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -ffreestanding -nostartfiles -O2 -g -Wall -Wextra \
          -DBAREMETAL -I.
M4LDFLAGS = -T m4/link.ld -Wl,--gc-sections -Wl,--build-id=none

ITERS   ?= 1000
MSG_LEN ?= 64

SRCS    = bench.c sha256.c
M4_SRCS = $(SRCS) m4/startup.c m4/semihost.c

INSN_PLUGIN := plugin/libinsn_count.so

# ---- default ----
.PHONY: all
all: bench-host bench bench-m4.elf $(INSN_PLUGIN)

# ---- builds ----
bench-host: $(SRCS) sha256.h
	$(CC_HOST) $(CFLAGS_COMMON) -o $@ $(SRCS)

bench: $(SRCS) sha256.h
	@echo "  CC[arm-userspace]  $@"
	@$(CC_ARM) $(ARMFLAGS) $(CFLAGS_COMMON) -static -o $@ $(SRCS) 2>/tmp/cortex-cc.log \
	    || ( echo "  -> retrying with $(ARMFLAGS_FALLBACK)"; \
	         $(CC_ARM) $(ARMFLAGS_FALLBACK) $(CFLAGS_COMMON) -static -o $@ $(SRCS) )
	@$(SIZE) $@ || true

# bench-m4.elf bakes ITERS/MSG_LEN in at compile time (no argv on bare-metal),
# so we force-rebuild on every invocation — the build is sub-second.
.PHONY: bench-m4.elf
bench-m4.elf: $(M4_SRCS) sha256.h m4/semihost.h m4/link.ld
	@echo "  CC[m4-bare]  $@  (ITERS=$(ITERS) MSG_LEN=$(MSG_LEN))"
	@$(CC_M4) $(M4FLAGS) -DBENCH_ITERS=$(ITERS) -DBENCH_MSGLEN=$(MSG_LEN) \
	    $(M4LDFLAGS) -o $@ $(M4_SRCS)
	@$(SIZE_M4) $@

$(INSN_PLUGIN): plugin/insn_count.c plugin/qemu-plugin.h plugin/Makefile
	$(MAKE) -C plugin

# ---- bare-metal cycle count (the recommended path) ----
.PHONY: count
count: bench-m4.elf $(INSN_PLUGIN)
	@echo "  RUN  $(QEMU_SYSTEM) -M mps2-an386 -kernel bench-m4.elf"
	@$(QEMU_SYSTEM) -M mps2-an386 -nographic -no-reboot \
	    -semihosting-config enable=on,target=native \
	    -plugin ./$(INSN_PLUGIN) -d plugin \
	    -kernel bench-m4.elf 2>&1 \
	    | grep -E '^[0-9a-f]{64}|^insns retired'

# ---- userspace Linux cross-check (Linux-only) ----
.PHONY: count-linux
count-linux: bench bench-host $(INSN_PLUGIN)
	@echo "  RUN  $(QEMU_USER) ./bench $(ITERS) $(MSG_LEN)"
	@$(QEMU_USER) ./bench $(ITERS) $(MSG_LEN)         > /tmp/d-arm.txt
	@./bench-host $(ITERS) $(MSG_LEN)                 > /tmp/d-host.txt
	@cmp -s /tmp/d-arm.txt /tmp/d-host.txt && echo "  digest OK (host == arm)" \
	    || ( echo "  WARN: digest mismatch"; diff /tmp/d-arm.txt /tmp/d-host.txt )
	@$(QEMU_USER) -plugin ./$(INSN_PLUGIN) -d plugin ./bench $(ITERS) $(MSG_LEN) \
	    | grep -E '^insns retired' || true

# ---- one-shot build of plugin-enabled qemu-arm + qemu-system-arm ----
# Only needed on Linux (Ubuntu's qemu packages aren't built --enable-plugins).
# macOS Homebrew's qemu already has plugins, so this target is unused there.
.PHONY: qemu
qemu: $(QEMU_USER_LOCAL) $(QEMU_SYSTEM_LOCAL)

$(QEMU_USER_LOCAL) $(QEMU_SYSTEM_LOCAL): | qemu/build/build.ninja
	@cd qemu/build && $(MAKE) -j$$(nproc) qemu-arm qemu-system-arm

qemu/build/build.ninja:
	@if [ ! -d qemu/.git ]; then \
	  git clone --depth 1 --branch v11.0.0 https://gitlab.com/qemu-project/qemu.git ; \
	fi
	@mkdir -p qemu/build
	@cd qemu/build && ../configure \
	    --target-list=arm-linux-user,arm-softmmu \
	    --enable-plugins \
	    --disable-tools --disable-docs --disable-werror

# ---- inspection ----
.PHONY: disasm
disasm: bench-m4.elf
	@$(OBJDUMP_M4) -d --no-show-raw-insn bench-m4.elf \
	    | sed -n '/<sha256_compress>:/,/^$$/p'

.PHONY: sizes
sizes: bench-m4.elf
	@$(SIZE_M4) -A bench-m4.elf

# ---- a small built-in self-test for CI ----
.PHONY: test
test: count
	@$(CC_HOST) $(CFLAGS_COMMON) -o bench-host $(SRCS)
	@./bench-host $(ITERS) $(MSG_LEN) > /tmp/d-host.txt
	@$(QEMU_SYSTEM) -M mps2-an386 -nographic -no-reboot \
	    -semihosting-config enable=on,target=native \
	    -kernel bench-m4.elf 2>&1 | sed -n '1p' > /tmp/d-m4.txt
	@cmp -s /tmp/d-host.txt /tmp/d-m4.txt && echo "  OK: m4 digest == host digest" \
	    || ( echo "  FAIL: digest mismatch"; diff /tmp/d-host.txt /tmp/d-m4.txt; exit 1 )

# ---- housekeeping ----
.PHONY: clean
clean:
	rm -f bench bench-host bench-m4.elf bench-m4.map
	rm -f /tmp/cortex-cc.log /tmp/d-arm.txt /tmp/d-host.txt /tmp/d-m4.txt
	$(MAKE) -C plugin clean

.PHONY: distclean
distclean: clean
	rm -rf qemu
