# cyclebench-m4

A small harness for estimating how many clock cycles a piece of pure C will
take on a **Cortex-M4**. Cross-compiles for an architecturally-pure M4 ELF,
runs it under `qemu-system-arm -M mps2-an386` (a real Cortex-M4 board model),
and counts retired instructions via a tiny TCG plugin. Comes with a SHA-256
reference workload as a worked example.

Two measurement paths, both producing comparable numbers:

| Path | Toolchain | Run target | What it gives you |
|---|---|---|---|
| **`make count`** (recommended) | `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16` | `qemu-system-arm -M mps2-an386` | Architecturally-pure M4 ISA. Works on Linux & macOS. |
| `make count-linux` (cross-check) | `arm-linux-gnueabihf-gcc -mcpu=cortex-a7 -mthumb -mfpu=fpv4-sp-d16` | `qemu-arm` (user-mode) | Same Thumb-2 / FPv4-SP instruction set; faster turnaround; Linux only. |

Both paths use the same in-tree TCG plugin (`plugin/libinsn_count.so`, ~50
lines) which asks QEMU to emit an inline `ADD_U64` per translated block —
counting is essentially free in the JITted host code. ~2 billion guest
instructions per second.

## What this measures (and what it doesn't)

QEMU reports **instructions retired**, not real M4 cycles. Most Thumb-2 ALU
ops retire in 1 cycle on M4, so retired-insn count is a very tight lower
bound on cycles, but it ignores:

| Source of extra cycles    | Typical M4 cost |
|---|---|
| Taken branch              | 1–3 cycles (pipeline refill) |
| `LDR` / `STR` (single)    | 2 cycles (1 if pipelined with next) |
| `LDM` / `STM` of N regs   | 1 + N cycles |
| `MUL` / `MLA`             | 1 cycle |
| `UMULL` / `SMULL`         | 3–5 cycles |
| `SDIV` / `UDIV`           | 2–12 cycles (data-dependent) |
| FP single `VADD/VMUL`     | 1 cycle |
| `VDIV.F32` / `VSQRT.F32`  | 14 cycles |
| Flash wait states         | depends on MCU + cache config |

Treat the count as a tight lower bound on real cycles. It's excellent for
**comparing** two C implementations — if A retires 30% fewer instructions
than B, A is almost certainly faster on silicon. For absolute cycle numbers,
use the DWT cycle counter on real hardware, a cycle-accurate sim (Renode,
Keil µVision, ARM Fast Models), or weight the instruction mix using the
table above.

## Setup

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install -y \
    gcc-arm-none-eabi binutils-arm-none-eabi \
    gcc-arm-linux-gnueabihf \
    libglib2.0-dev ninja-build pkg-config python3-venv
make qemu        # ~30s — builds qemu-arm + qemu-system-arm with --enable-plugins
```

### macOS

```bash
brew install --cask gcc-arm-embedded   # arm-none-eabi-*
brew install qemu                       # qemu-system-arm with plugins enabled
```

(The macOS path uses Homebrew's QEMU, which ships with `--enable-plugins`,
so `make qemu` is unnecessary on Mac. The `make count-linux` path needs
`arm-linux-gnueabihf-gcc` and `qemu-arm` user-mode, neither of which has a
clean Homebrew bottle — that's why it's "Linux-only".)

Ubuntu's `qemu-user` and `qemu-system-arm` are both built **without**
`--enable-plugins`, which is why Linux needs `make qemu` to compile a
plugin-enabled pair locally. Builds in ~30s, only the
`arm-linux-user`/`arm-softmmu` targets.

## Usage

```bash
make             # build host (sanity) + ARM userspace + bare-metal M4 + plugin
make count       # → "insns retired: N" using the bare-metal M4 path
make count ITERS=10000 MSG_LEN=1024
```

Get the *per-iteration* cost cleanly by running twice and subtracting — that
nulls out program startup, `_exit`, and the SHA-256 finalise:

```bash
make count ITERS=1000   # → N1
make count ITERS=11000  # → N2
# per-iter ≈ (N2 - N1) / 10000
```

`ITERS=0` runs only program startup (FPU enable, .bss zero, exit) — that's
the baseline:

```bash
$ make count ITERS=0
insns retired: 863
```

### Worked example: SHA-256 cost on the supplied implementation

Measured here on the bare-metal M4 path (`arm-none-eabi-gcc 13.3.1`,
`-O2 -mthumb -mfpu=fpv4-sp-d16`, qemu-system-arm 8.2.2, mps2-an386):

```
ITERS=0     baseline                                  863
ITERS=1000  msg_len=64    total = 7,145,346    per-iter ≈ 7,144
ITERS=11000 msg_len=64    total = 78,525,346   per-iter ≈ 7,138
```

Per-iteration: **7,138 instructions per `sha256(64-byte buffer)`** on real
Cortex-M4 ISA, which decomposes as ~3,569 instructions per 64-byte SHA-256
compress block (each call processes 1 message block + 1 padding block).
Matches published numbers for naive C SHA-256 on M4 (~3,500–6,000
cycles/block on real silicon).

The userspace cross-check path gives ~6,754 insns per call — about 5%
lower than bare-metal due to different scheduling tuning (`cortex-a7` vs
`cortex-m4`) and a slightly leaner libc-side syscall surface. Use the
bare-metal number as the authoritative one for "how many cycles on a real
M4"; use the userspace number for fast iteration when you don't need
absolute accuracy.

## Files

| File                          | What it is |
|---|---|
| `sha256.h`/`sha256.c`         | Pure-C SHA-256 (FIPS 180-4). The "code under measurement". |
| `bench.c`                     | Workload driver. Same source compiles for userspace Linux *and* bare-metal M4 via `-DBAREMETAL`. |
| `m4/startup.c`                | Cortex-M4 vector table + Reset_Handler. Enables CP10/CP11 (FPU) before main. |
| `m4/semihost.c`/`.h`          | ARM Semihosting `bm_write` / `bm_exit` (BKPT 0xab). |
| `m4/link.ld`                  | Linker script for `mps2-an386`: code at 0x0, RAM at 0x20000000. |
| `plugin/insn_count.c`         | QEMU TCG plugin: registers `INLINE_ADD_U64` per TB, prints total at exit. |
| `plugin/qemu-plugin.h`        | Vendored from QEMU 8.2.2 upstream (header-only, GPL-2). |
| `plugin/Makefile`             | Builds `libinsn_count.so` against the vendored header — no glib needed. |
| `Makefile`                    | All build/run targets. |
| `qemu/`                       | (Optional) QEMU 8.2.2 source clone, used only by `make qemu` on Linux. |

## Inspecting the generated code

```bash
make disasm    # dumps Thumb-2 disassembly of sha256_compress
make sizes     # per-section sizes — flash/RAM footprint
```

The disassembly is the most useful artifact when comparing two C variants:
if you see the inner loop spilling to the stack, or the compiler choosing
`UMULL` over `MUL`, that's where your cycles go.

## How the plugin works (in 5 lines)

```c
qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans);
  └─ on_tb_trans:
       qemu_plugin_register_vcpu_tb_exec_inline(
           tb, QEMU_PLUGIN_INLINE_ADD_U64, &insn_count, n_insns);
```

A translation-time callback fires when QEMU compiles a TB. We ask QEMU to
emit an inline `*counter += n` op into the JITted host code. So per-TB
overhead is one host memory add — that's why the count target runs at ~2
billion guest instructions per second. At process exit, the plugin prints
the counter to stderr.

We vendor `qemu-plugin.h` from QEMU 8.2.2 because Ubuntu's `qemu-user`
package doesn't ship plugin headers, and it's a self-contained header
file that compiles cleanly with `-fPIC -shared`.

## Why bare-metal vs userspace, and why the userspace path uses `-mcpu=cortex-a7`

The **bare-metal path** (`make count`) is the one you should trust: gcc
emits Thumb-2 + FPv4-SP instructions targeting Cortex-M4 specifically, the
linker places the vector table at address 0 with our SP and Reset_Handler,
and `qemu-system-arm -M mps2-an386` runs a real Cortex-M4 model with FPU.
This is what an M4 actually executes.

The **userspace path** (`make count-linux`) is a convenience for fast
turnaround. A truly M-profile object can't be linked against glibc startup
(which is A-profile) — `ld` rejects it with `conflicting architecture
profiles M/A`. The Makefile first tries `-mcpu=cortex-m4` and falls back to
`-mcpu=cortex-a7`, which has the same Thumb-2 + FPv4-SP-d16 + DSP
instruction set as M4. The instruction *selection* is essentially identical
in the hot loop; only the scheduler tuning and the ELF arch-attribute byte
differ. Numbers are within ~5% of bare-metal, which is more than tight
enough for comparing two implementations.

## CI

`.github/workflows/ci.yml` runs the harness on both Ubuntu and macOS as a
sanity check that the bare-metal path stays portable.

## License

GPL-2.0-or-later. See `LICENSE`. The plugin links against QEMU's
GPL-2.0-or-later plugin API; the rest of the code follows suit for clarity.
