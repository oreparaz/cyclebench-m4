# cortex-cycle

A small harness for estimating how many clock cycles a piece of pure C will
take on a **Cortex-M4**, by cross-compiling for ARM userspace, running under
`qemu-arm`, and counting retired instructions. Comes with a SHA-256 reference
workload as a worked example.

The harness gives you two ways to count, both verified to produce identical
numbers (within ~16 insns of qemu version drift):

| Path | Speed | Setup |
|---|---|---|
| `make count`      | trace via stock `qemu-arm -d exec`, ~700k insns/sec | works out of the box |
| `make count-fast` | TCG plugin against locally-built qemu-arm, ~2 Ginsns/sec | needs `make qemu` once (~13s build) |

Use `count` for quick checks, `count-fast` for parameter sweeps and large
workloads. Numbers are bit-for-bit comparable.

## What this measures (and what it doesn't)

QEMU reports **instructions retired**, not real M4 cycles. The two are close
but not equal — on a Cortex-M4 most ALU / Thumb-2 ops retire in 1 cycle, but
you also pay for:

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

So treat the count as a **tight lower bound** on real cycles. It's excellent
for *comparing* two C implementations — if A retires 30% fewer instructions
than B, A is almost certainly faster on silicon. For absolute cycle numbers,
use the DWT cycle counter on real hardware, a cycle-accurate sim (Renode,
Keil µVision, ARM Fast Models), or weight the instruction mix using the
table above.

## One-time setup

```bash
sudo apt-get install -y \
    gcc-arm-linux-gnueabihf gcc-arm-none-eabi binutils-arm-none-eabi \
    qemu-user                                       # for `make count`
# Plus, if you want `make count-fast`:
sudo apt-get install -y libglib2.0-dev ninja-build pkg-config python3-venv
```

Ubuntu's `qemu-user 8.2.2` is built **without** `--enable-plugins`, which is
why the fast path needs `make qemu` to compile a plugin-enabled `qemu-arm`
locally. It's a ~13-second build of just the `arm-linux-user` target.

## Usage

```bash
make             # builds host (sanity) + ARM userspace bench binary
make count       # → "insns retired: N"           (slow path, no extra setup)
make qemu        # → builds plugin-enabled qemu-arm  (one-time, ~13s)
make count-fast  # → same, ~30–1000× faster        (uses local qemu + plugin)
```

Tune the workload from the command line:

```bash
make count-fast ITERS=100000 MSG_LEN=1024
```

To get the *per-iteration* cost cleanly, run twice with different `ITERS` and
subtract — that nulls out program startup, the final `printf`, and `_exit`:

```bash
make count-fast ITERS=1000   # → N1
make count-fast ITERS=11000  # → N2
# per-iter ≈ (N2 - N1) / 10000
```

Or use `ITERS=0` directly as a baseline (the bench skips the loop entirely):

```bash
make count-fast ITERS=0          # → baseline
make count-fast ITERS=10000 MSG_LEN=64
# per-call ≈ (count_at_10000 - baseline) / 10000
```

### Worked example: SHA-256 cost on the supplied implementation

Measured on this harness (qemu-arm 8.2.2, gcc 13.3.0, `-O2 -mthumb -mfpu=fpv4-sp-d16`):

```
baseline (iters=0)         52,628
msg_len=  16   per-call =   3,533    per-byte = 220.8
msg_len=  64   per-call =   6,755    per-byte = 105.6
msg_len= 256   per-call =  16,535    per-byte =  64.6
msg_len=1024   per-call =  55,655    per-byte =  54.4
msg_len=4096   per-call = 212,137    per-byte =  51.8
```

The asymptote of ~52 insns/byte = **~3,300 insns per 64-byte SHA-256 block**.
On real M4 silicon at 1 IPC with no flash wait states, expect ~3,300–4,500
cycles/block depending on memory layout — consistent with published numbers
for naive C SHA-256 on M4 (~3,500–6,000 cycles/block).

## Files

| File                          | What it is |
|---|---|
| `sha256.h/.c`                 | Pure-C SHA-256 (FIPS 180-4). The "code under measurement". |
| `bench.c`                     | Driver: hashes a buffer N times, chaining digests so the loop can't be hoisted. |
| `Makefile`                    | Build + run targets, including bare-metal Cortex-M4 `.elf`. |
| `plugin/insn_count.c`         | Tiny QEMU TCG plugin: registers `INLINE_ADD_U64` per TB and prints the total at exit. |
| `plugin/qemu-plugin.h`        | Vendored from QEMU 8.2.2 upstream (header-only, GPL-2). |
| `plugin/Makefile`             | Builds `libinsn_count.so` against the vendored header — no glib needed. |
| `qemu/`                       | (Optional) QEMU 8.2.2 source clone, used only by `make qemu`. |

## Inspecting the generated code

```bash
make disasm    # dumps Thumb-2 disassembly of sha256_compress
make sizes    # per-section sizes — proxy for flash + .bss/.data footprint
```

The disassembly is the most useful artifact when comparing two C variants:
if you see the inner loop spilling to the stack, or the compiler choosing
`UMULL` over `MUL`, that's where your cycles go.

## Why the userspace path uses `-mcpu=cortex-a7 -mtune=cortex-m4`

A truly M-profile object can't be linked against glibc startup (which is
A-profile) — `ld` rejects it with `conflicting architecture profiles M/A`.
The Makefile first tries `-mcpu=cortex-m4 -mthumb` and falls back to
`-mcpu=cortex-a7 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`, which produces
the same Thumb-2 + FPv4-SP-d16 instruction selection an M4 implements (M4 is
armv7e-m + DSP + FPv4-SP, all of which Cortex-A7 also supports). The numbers
you get back are valid for M4 cycle estimation — what differs is only the
ELF architecture-attribute byte, not the bytes that retire in the hot loop.

For a truly M-profile binary, `make bench-m4` cross-compiles with
`arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb` to produce `bench-m4.elf`. That
runs only under `qemu-system-arm -M mps2-an386` with a startup file +
linker script + semihosting setup (out of scope here), but its disassembly
via `arm-none-eabi-objdump -d bench-m4.elf` is the most faithful view of
what an M4 will execute.

## How the plugin works (50 lines)

`plugin/insn_count.c` registers a translation-time callback. When QEMU
translates a Translation Block (TB), the plugin counts instructions in it
and asks QEMU to emit an inline `ADD_U64 &counter, n` op into the JITted
host code. So per-TB cost is one host memory add — that's why the fast path
runs at ~2 billion guest instructions per second. At process exit, the
plugin prints the counter to stderr.

```
qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb_trans);
  └─ on_tb_trans: qemu_plugin_register_vcpu_tb_exec_inline(
         tb, QEMU_PLUGIN_INLINE_ADD_U64, &insn_count, n_insns);
```

That's the whole counting mechanism.
