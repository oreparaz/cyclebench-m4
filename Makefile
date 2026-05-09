# Cortex-M4 cycle-estimation harness
#
# Targets:
#   make              — build everything (host + ARM userspace)
#   make host         — build a native x86 reference binary (for sanity)
#   make bench        — build ARM userspace binary (qemu-arm runnable)
#   make bench-m4     — build bare-metal Cortex-M4 .elf (no QEMU run target here)
#   make count        — run under qemu-arm with libinsn TCG plugin (insns retired)
#   make count-trace  — fallback: count via -d in_asm (slow, no plugin needed)
#   make disasm       — dump Thumb-2 disassembly of the ARM build
#   make sizes        — print per-section sizes (rough flash/RAM footprint)
#   make clean

# ---- toolchains ----
CC_HOST    ?= cc
CC_ARM     ?= arm-linux-gnueabihf-gcc
OBJDUMP    ?= arm-linux-gnueabihf-objdump
SIZE       ?= arm-linux-gnueabihf-size
CC_M4      ?= arm-none-eabi-gcc
OBJDUMP_M4 ?= arm-none-eabi-objdump
SIZE_M4    ?= arm-none-eabi-size

QEMU       ?= qemu-arm

# ---- flags ----
# We approximate Cortex-M4 ISA in userspace with armv7e-m via -mcpu=cortex-m4
# + -mthumb. arm-linux-gnueabihf's glibc startup is ARM-mode; if your toolchain
# rejects -mcpu=cortex-m4 for a fully-linked binary, fall back to ARMFLAGS_FALLBACK.
# The instruction mix is what matters for cycle estimation, and Thumb-2 + FPv4-SP
# matches what an M4 actually executes.
ARMFLAGS          = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
ARMFLAGS_FALLBACK = -mcpu=cortex-a7  -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
CFLAGS_COMMON     = -O2 -g -Wall -Wextra -fno-builtin -fno-stack-protector

# Bare-metal M4 uses -specs=nosys.specs so we link without a real OS.
M4FLAGS = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb \
          -O2 -g -Wall -Wextra -ffreestanding -specs=nosys.specs

SRCS = bench.c sha256.c

# ---- default ----
.PHONY: all
all: host bench

# ---- host reference build (correctness sanity) ----
.PHONY: host
host: bench-host
bench-host: $(SRCS) sha256.h
	$(CC_HOST) $(CFLAGS_COMMON) -o $@ $(SRCS)

# ---- ARM userspace build (qemu-arm runnable) ----
bench: $(SRCS) sha256.h
	@echo "  CC[arm-userspace]  $@"
	@$(CC_ARM) $(ARMFLAGS) $(CFLAGS_COMMON) -static -o $@ $(SRCS) 2>/tmp/cortex-cc.log \
	    || ( echo "  -> retrying with $(ARMFLAGS_FALLBACK)"; \
	         $(CC_ARM) $(ARMFLAGS_FALLBACK) $(CFLAGS_COMMON) -static -o $@ $(SRCS) )
	@$(SIZE) $@ || true

# ---- bare-metal Cortex-M4 build (proper M4 ISA, no Linux) ----
bench-m4.elf: $(SRCS) sha256.h
	$(CC_M4) $(M4FLAGS) -o $@ $(SRCS)
	$(SIZE_M4) $@

.PHONY: bench-m4
bench-m4: bench-m4.elf

# ---- instruction counting ----
# Two paths:
#   make count        — stock qemu-arm + -d exec trace, streamed through grep.
#                       Works with any qemu but is slow (~1µs/insn host time).
#                       Good for ITERS up to a few thousand.
#   make count-fast   — uses ./qemu/build/qemu-arm built with --enable-plugins
#                       and our libinsn_count.so. ~30× faster, scales to 1e9 insns.
#                       Run `make qemu` once to produce that binary.

ITERS   ?= 1000
MSG_LEN ?= 64
INSN_PLUGIN := plugin/libinsn_count.so

$(INSN_PLUGIN): plugin/insn_count.c plugin/qemu-plugin.h plugin/Makefile
	$(MAKE) -C plugin

# -- stock-qemu trace counter --
.PHONY: count
count: bench bench-host
	@echo "  RUN(trace)  $(QEMU) -one-insn-per-tb ./bench $(ITERS) $(MSG_LEN)"
	@$(QEMU) ./bench $(ITERS) $(MSG_LEN) > /tmp/cortex-digest.txt 2>/dev/null \
	 ; ./bench-host $(ITERS) $(MSG_LEN) > /tmp/cortex-digest-host.txt \
	 ; cmp -s /tmp/cortex-digest.txt /tmp/cortex-digest-host.txt && echo "  digest OK (host == arm)" || echo "  WARN: digest mismatch"
	@$(QEMU) -one-insn-per-tb -d nochain,exec ./bench $(ITERS) $(MSG_LEN) 2>&1 >/dev/null \
	  | awk '/^Trace /{n++} END{printf "insns retired: %d\n", n}'

# -- plugin-enabled qemu (built locally) --
QEMU_LOCAL := qemu/build/qemu-arm

.PHONY: count-fast
count-fast: bench $(INSN_PLUGIN) $(QEMU_LOCAL)
	@echo "  PLUGIN  $(INSN_PLUGIN)"
	@echo "  RUN     $(QEMU_LOCAL) ./bench $(ITERS) $(MSG_LEN)"
	@$(QEMU_LOCAL) -plugin ./$(INSN_PLUGIN) -d plugin ./bench $(ITERS) $(MSG_LEN)

# One-shot build of a minimal plugin-enabled qemu-arm. ~3-5 minutes.
.PHONY: qemu
qemu: $(QEMU_LOCAL)

$(QEMU_LOCAL):
	@if [ ! -d qemu ]; then \
	  git clone --depth 1 --branch v8.2.2 https://gitlab.com/qemu-project/qemu.git ; \
	fi
	cd qemu && ./configure \
	    --target-list=arm-linux-user \
	    --enable-plugins \
	    --disable-system --disable-tools --disable-docs \
	    --disable-werror --static \
	    && $(MAKE) -j$$(nproc) qemu-arm

# ---- plugin-free fallback ----
# -d in_asm logs every translated block; with -singlestep that's one block per
# instruction, so wc -l on lines starting with 0x gives a count. Slow but works.
.PHONY: count-trace
count-trace: bench
	@echo "  RUN(trace)  $(QEMU) -singlestep -d in_asm ./bench $(ITERS) $(MSG_LEN)"
	@$(QEMU) -singlestep -d in_asm,nochain ./bench $(ITERS) $(MSG_LEN) 2> /tmp/cortex-trace.log >/dev/null
	@printf "instructions retired (approx): "
	@grep -c '^0x' /tmp/cortex-trace.log

# ---- inspection ----
.PHONY: disasm
disasm: bench
	$(OBJDUMP) -d -S --no-show-raw-insn bench | sed -n '/<sha256_compress>:/,/^$$/p'

.PHONY: sizes
sizes: bench
	$(SIZE) -A bench

# ---- housekeeping ----
.PHONY: clean
clean:
	rm -f bench bench-host bench-m4.elf /tmp/cortex-trace.log /tmp/cortex-cc.log
