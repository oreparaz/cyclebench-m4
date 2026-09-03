# cyclebench-m4

[![ci](https://github.com/oreparaz/cyclebench-m4/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/oreparaz/cyclebench-m4/actions/workflows/ci.yml)

A small harness for estimating how many clock cycles a piece of pure C will
take on a **Cortex-M4**. It cross-compiles an architecturally-pure M4 ELF and
runs it under `qemu-system-arm -M mps2-an386`. One TCG plugin counts executed
instructions; a second applies a pessimistic zero-wait-state Cortex-M4 timing
model. Comes with a SHA-256 reference workload as a worked example.

Three measurement paths:

| Path | Toolchain | Run target | What it gives you |
|---|---|---|---|
| `make count` | `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16` | `qemu-system-arm -M mps2-an386` | Executed-instruction baseline. |
| **`make count-cycles`** (recommended for timing) | same | same | Pessimistic zero-wait-state Cortex-M4 cycle estimate and breakdown. |
| `make count-linux` (cross-check) | `arm-linux-gnueabihf-gcc -mcpu=cortex-a7 -mthumb -mfpu=fpv4-sp-d16` | `qemu-arm` (user-mode) | Same Thumb-2 / FPv4-SP instruction set; faster turnaround; Linux only. |

`count` uses `plugin/libinsn_count.so`, which emits one inline addition per
translated block. `count-cycles` uses `plugin/libm4_cycles.so`, which decodes
each instruction and emits per-instruction additions for the relevant timing
categories.

## What this measures (and what it doesn't)

QEMU itself is not cycle accurate. `make count` reports instructions executed,
which is a useful baseline but ignores multi-cycle behavior:

| Source of extra cycles    | Typical M4 cost |
|---|---|
| Taken branch              | 1 + P = 2–4 cycles |
| `LDR` / `STR` (single)    | 2 cycles (1 if pipelined with next) |
| `LDM` / `STM` of N regs   | 1 + N cycles |
| `MUL` / `MLA`             | 1 cycle |
| `UMULL` / `SMULL`         | 1 cycle |
| `SDIV` / `UDIV`           | 2–12 cycles (data-dependent) |
| FP single `VADD/VMUL`     | 1 cycle |
| `VDIV.F32` / `VSQRT.F32`  | 14 cycles |
| Flash wait states         | depends on MCU + cache config |

`make count-cycles` follows the timing tables in the
[Cortex-M4 Technical Reference Manual](https://documentation-service.arm.com/static/5fce431be167456a35b36ade):

- `SDIV`/`UDIV` always take their worst documented latency of 12 cycles.
- Taken branches use the worst pipeline refill, `P=3`; untaken conditional
  branches remain one cycle. Branch direction comes from the executed path.
- Single loads/stores are charged two cycles. This deliberately does not claim
  the one-cycle reduction possible for favorable adjacent memory operations.
- Multi-register and floating-point instructions use their documented maximum
  core costs. Immediate FP consumers are assumed where that increases latency.
- Other decoded integer and DSP instructions are charged their documented
  single cycle; unlike real pairing, these per-instruction maxima are additive.
- Instructions such as `WFI`, `WFE`, barriers, and semihosting `BKPT` that have
  no finite instruction-only bound are reported separately.

This is a deterministic, pessimistic estimate for the path QEMU executes. It
is not a formal WCET bound over every possible input, and it excludes flash,
SRAM, peripheral, and bus wait states. The MPS2 code region named `FLASH` in
the linker script is actually modeled as ZBT SRAM. For a specific MCU, use
its DWT cycle counter to calibrate or replace this estimate.

## Setup

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install -y \
    gcc-arm-none-eabi binutils-arm-none-eabi \
    gcc-arm-linux-gnueabihf \
    libcapstone-dev libglib2.0-dev ninja-build pkg-config python3-venv
make qemu        # builds QEMU with plugin and Capstone disassembly support
```

### macOS

```bash
brew install --cask gcc-arm-embedded   # arm-none-eabi-*
brew install qemu capstone              # QEMU plugins + instruction decoding
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
make count-cycles # → cycle estimate plus category breakdown
make count ITERS=10000 MSG_LEN=1024
```

Get the *per-iteration* cost cleanly by running twice and subtracting — that
nulls out program startup, `_exit`, and the SHA-256 finalise:

```bash
make count ITERS=1000   # → N1
make count ITERS=11000  # → N2
# per-iter ≈ (N2 - N1) / 10000

make count-cycles ITERS=1000   # → C1
make count-cycles ITERS=11000  # → C2
# per-iter cycle estimate ≈ (C2 - C1) / 10000
```

`ITERS=0` runs only program startup (FPU enable, .bss zero, exit) — that's
the baseline:

```bash
$ make count ITERS=0
insns retired: 863
```

### Worked example: SHA-256 cost on the supplied implementation

Measured here on the bare-metal M4 path (`arm-none-eabi-gcc 13.3.1`,
`-O2 -mthumb -mfpu=fpv4-sp-d16`, qemu-system-arm 11.0.0, mps2-an386):

| Method | ITERS=1,000 | ITERS=11,000 | Per iteration |
|---|---:|---:|---:|
| instruction count | 7,145,346 | 78,525,346 | 7,138 |
| pessimistic M4 cycles | 9,654,431 | 106,034,431 | 9,638 |

The 2,500-cycle difference per iteration consists of 1,606 memory-operation
cycles and 894 taken-branch/pipeline-refill cycles. This SHA-256 binary contains
no divide or floating-point instructions, so those categories are zero.

Per-iteration: **7,138 instructions** or **9,638 pessimistic zero-wait-state
core cycles** per `sha256(64-byte buffer)`. Each call processes one message
block plus one padding block.

The userspace cross-check path gives ~6,754 insns per call — about 5%
lower than bare-metal due to different scheduling tuning (`cortex-a7` vs
`cortex-m4`) and a slightly leaner libc-side syscall surface. Use the
bare-metal path when the exact M4 instruction stream matters; use the
userspace number for fast comparative iteration. Neither instruction count
alone is an authoritative real-hardware cycle measurement.

## Files

| File                          | What it is |
|---|---|
| `sha256.h`/`sha256.c`         | Pure-C SHA-256 (FIPS 180-4). The "code under measurement". |
| `bench.c`                     | Workload driver. Same source compiles for userspace Linux *and* bare-metal M4 via `-DBAREMETAL`. |
| `m4/startup.c`                | Cortex-M4 vector table + Reset_Handler. Enables CP10/CP11 (FPU) before main. |
| `m4/semihost.c`/`.h`          | ARM Semihosting `bm_write` / `bm_exit` (BKPT 0xab). |
| `m4/link.ld`                  | Linker script for `mps2-an386`: code at 0x0, RAM at 0x20000000. |
| `plugin/insn_count.c`         | Original QEMU TCG executed-instruction counter. |
| `plugin/m4_cycles.c`          | Per-instruction QEMU instrumentation and timing breakdown. |
| `plugin/m4_timing.c`/`.h`     | Testable Cortex-M4 mnemonic timing model. |
| `plugin/test_m4_timing.c`     | Unit coverage for ALU, divide, branch, memory, register-list, and FP timing. |
| `plugin/qemu-plugin.h`        | Vendored from QEMU 11.0.0 upstream (header-only, GPL-2). |
| `plugin/Makefile`             | Builds both plugins and the timing-model unit test against the vendored QEMU header. |
| `Makefile`                    | All build/run targets. |
| `qemu/`                       | (Optional) QEMU 11.0.0 source clone, used only by `make qemu` on Linux. |

## Inspecting the generated code

```bash
make disasm    # dumps Thumb-2 disassembly of sha256_compress
make sizes     # per-section sizes — flash/RAM footprint
```

The disassembly is the most useful artifact when comparing two C variants:
if you see the inner loop spilling to the stack, or the compiler choosing
`UMULL` over `MUL`, that's where your cycles go.

## How the plugins work

```c
sb         = qemu_plugin_scoreboard_new(sizeof(uint64_t));
insn_count = qemu_plugin_scoreboard_u64(sb);

qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans);
  └─ on_tb_trans:
       qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
           tb, QEMU_PLUGIN_INLINE_ADD_U64, insn_count, n_insns);

at exit:
       fprintf(stderr, "insns retired: %lu\n",
               qemu_plugin_u64_sum(insn_count));
```

A translation-time callback fires when QEMU compiles a TB. The plugin asks
QEMU to emit an inline `*counter += n` op into the JITted host code,
backed by a per-vcpu scoreboard slot. So per-TB host-side overhead is one
memory add — that's why the count target runs at ~2 billion guest
instructions per second. At process exit, the plugin sums across vcpus
and prints to stderr.

The cycle plugin instead enumerates the instructions in each TB, obtains their
Capstone disassembly through QEMU, and applies `m4_timing_for_disas()`. It uses
per-instruction inline counters so an exception in the middle of a TB does not
pre-count later instructions. An execution callback on each conditional branch
records its fall-through PC; a TB-entry callback compares that with the next
PC, charging the three-cycle refill only when the branch was taken. A nonzero
`undecoded instructions` result makes
`make count-cycles` fail rather than silently reverting to one cycle each.

We vendor `qemu-plugin.h` from QEMU 11.0.0 because Ubuntu's `qemu-user`
package doesn't ship plugin headers. The pre-9.0 inline-add API
(`qemu_plugin_register_vcpu_tb_exec_inline`) was removed in QEMU 9.0;
we use its successor (`_per_vcpu` + scoreboards). On Linux, `make qemu`
clones and builds QEMU v11.0.0 to match the API; on macOS, Homebrew
ships v11+.

## Why bare-metal vs userspace, and why the userspace path uses `-mcpu=cortex-a7`

The **bare-metal path** (`make count` or `make count-cycles`) is the one to
use for an M4-specific instruction stream: gcc
emits Thumb-2 + FPv4-SP instructions targeting Cortex-M4 specifically, the
linker places the vector table at address 0 with our SP and Reset_Handler,
and `qemu-system-arm -M mps2-an386` models Cortex-M4 architectural execution
with an FPU. QEMU does not model the core's cycle-by-cycle pipeline or the
target MCU's memory system; `count-cycles` supplies the separate timing-table
estimate described above.

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
