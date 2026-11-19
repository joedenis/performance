# performance

Experiments in how CPU architecture affects C++ performance, measured with [Google Benchmark](https://github.com/google/benchmark).

## 1. Branch prediction: `if` vs branchless

`bench.cpp` sums every element ≥ 128 in an array of 2²⁰ random ints (0–255), two ways:

- `sum_if`: a plain `if`, so one conditional jump per element
- `sum_mask`: branchless, `s += x & -(x >= 128)`

Each runs on unsorted data (the branch is a coin flip) and sorted data (the branch is predictable).

### Results (4-core 3.8 GHz, 32 KiB L1d, 256 KiB L2, 6 MiB L3, GCC 13.3 -O2; medians of 10 repetitions with the governor set to `performance`)

| | unsorted | sorted |
|---|---|---|
| `sum_if` | 3.51 ms · 3.3 ns | 0.51 ms · 0.49 ns |
| `sum_mask` | 0.60 ms · 0.57 ns | 0.60 ms · 0.57 ns |

ms = one call over 2²⁰ elements; ns = per element (time ÷ 2²⁰).

- Mispredicts make `sum_if` ~7x slower on random data.
- `sum_mask` has no branch, so the order of the data doesn't matter.
- On predictable data the branch is nearly free, and slightly beats the mask.
- Estimated mispredict penalty: (3.51 − 0.51) ms ÷ 2²⁰ elements ≈ 2.9 ns ≈ 11 cycles per element. About half the elements mispredict, so ≈ 22 cycles (≈ 6 ns) per miss.

Results were built with `-fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize`, which stop GCC from removing the branch itself (with `cmov` or SIMD). Section 2 removes them.

## 2. Letting GCC remove the branch

`CMakeLists.txt` now compiles with plain `-O2` (the `-fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize` flags removed).

GCC 13.3 **if-converted** `sum_if`: the assembly has a `cmov` (conditional move) instead of a conditional jump. It did **not** vectorise the loop (no `xmm`/`ymm` compare instructions). `-O2` vectorises with the "very cheap" cost model (since GCC 12), which skips loops like this one: the trip count isn't known at compile time, so it would need a leftover loop, and each `int` has to be widened to `long long`. `-O3` uses the full cost model, which accepts both (section 3).

### Results (same machine; medians of 10 repetitions with the governor set to `performance`)

| | unsorted | sorted |
|---|---|---|
| `sum_if`, branch kept (section 1) | 3.51 ms · 3.3 ns | 0.51 ms · 0.49 ns |
| `sum_if`, GCC `cmov` | 0.86 ms · 0.82 ns | 0.86 ms · 0.82 ns |
| `sum_mask` | 0.60 ms · 0.57 ns | 0.60 ms · 0.57 ns |

- **Unsorted got 4.1x faster (3.51 → 0.86 ms):** with no jump, there is nothing to mispredict, so sorted and unsorted are now the same.
- **Sorted got ~70% slower (0.51 → 0.86 ms):** a `cmov` always waits for both of its inputs, and here it sits in the loop-carried dependency on `s`. A correctly predicted branch lets the CPU run ahead speculatively instead.
- **The hand-written mask beats cmov**, as described below.
- **Lesson:** always measure.

### The assembly GCC generated

`sum_if`'s loop at `-O2` (Intel syntax):

```
c0e0: movsxd rdx, DWORD PTR [rax]   ; load x, sign-extend int → 64 bits
c0e3: mov    rsi, rdx               ; keep a copy of x
c0e6: add    rdx, rcx               ; candidate = s + x   (always computed)
c0e9: cmp    esi, 0x7f              ; x > 127 ?
c0ec: cmovg  rcx, rdx               ; if so, s = candidate   (no jump)
c0f0: add    rax, 0x4               ; next int
c0f4: cmp    rdi, rax
c0f7: jne    c0e0                   ; the loop's own branch: almost always predicted
```

- The `if` became `cmovg`. The only jump left is the loop's own `jne`, which is taken a million times and not taken once.
- The dependency chain runs through `rcx` (`s`): `add` reads it and `cmovg` writes it, so every element waits for two steps. That's why it's slower than `sum_mask`.
- Before the loop, two long `nop`s (11 + 7 bytes) pad it so it starts at `c0e0`, a 32-byte boundary, for faster instruction fetch.

To reproduce it:

```
objdump -d -C -M intel --no-show-raw-insn build/bench | awk '/::sum_if\(.*>:$/,/ret/'
```

### Why the mask beats cmov: out-of-order execution and the chain through `s`

Both are branchless. What differs is how many steps sit on the **loop-carried dependency**: each element's add into `s` has to wait for the previous one.

```
cmov:  s → add → cmov → s → add → cmov → s …   2 steps per element
mask:  s → add → s → add → s …                 1 step per element
            ↑         ↑
         x or 0    x or 0     worked out off the chain
```

- In GCC's `cmov` version, `s` feeds `add rdx, rcx` (s + x), which feeds `cmovg rcx, rdx` (the new `s`).
- In the mask version, `x & -(x >= 128)` doesn't involve `s`, so out-of-order execution works it out ahead for upcoming elements. Only the final `s += …` is on the chain.
- The ratio is ~1.4:1 (0.86 vs 0.60 ms), not 2:1, because the loads and the loop's own instructions also take time.

GCC also compiles the mask with a `cmov`, but a different one. `sum_mask`'s loop at `-O2`:

```
      xor    edi, edi               ; rdi = 0, set once before the loop
c120: movsxd rax, DWORD PTR [rdx]   ; load x
c123: cmp    eax, 0x7f              ; x > 127 ?
c126: cmovle rax, rdi               ; if not, x = 0      → rax is x or 0
c12a: add    rdx, 0x4               ; next int
c12e: add    rcx, rax               ; s += (x or 0)      ← the only step on the chain
c131: cmp    rsi, rdx
c134: jne    c120
```

| `-O2` loop | the `cmov` chooses between | instructions per element | steps on the chain through `s` (`rcx`) |
|---|---|---|---|
| `sum_if` | `s + x` and `s` | 8 | 2: `add`, then `cmovg` |
| `sum_mask` | `x` and `0` | 7 | 1: `add` |

What matters is whether `s` is involved, not which instruction is used. In `sum_mask` the `cmov` works on `x` alone, so it runs ahead for upcoming elements while the chain is busy.

## 3. Vectorising: SSE2 and AVX2

Same code again, now with `-O3` (full vectoriser cost model) and then `-O3 -march=native`. The CPU has AVX2 but not AVX-512 (`grep -o -w -E 'sse2|avx2|avx512f' /proc/cpuinfo | sort -u`).

| Flags | Instruction set | Registers | ints per instruction |
|---|---|---|---|
| `-O3` | SSE2 (every x86-64 CPU) | `xmm`, 128-bit | 4 |
| `-O3 -march=native` | AVX2 | `ymm`, 256-bit | 8 |

### Results (same machine; medians of 10 repetitions with the governor set to `performance`)

Sorted and unsorted are the same for every build here, so one figure each:

| Build | `sum_if` | `sum_mask` |
|---|---|---|
| `-O2` (scalar, `cmov`) | 0.86 ms · 0.82 ns | 0.60 ms · 0.57 ns |
| `-O3` (SSE2) | 0.37 ms · 0.35 ns | 0.25 ms · 0.24 ns |
| `-O3 -march=native` (AVX2) | 0.23 ms · 0.22 ns | 0.17 ms · 0.16 ns |

- Both loops are fully branchless: the only jump in each vector loop is its own `jne`.
- AVX2 `sum_mask` is about **20x faster than the original branchy `sum_if` on random data**: 3.3 ns → 0.16 ns per element, well under one clock cycle, because each instruction handles 8 ints.

### What the vectoriser generated

AVX2 `sum_mask`, 9 instructions per 8 ints:

```
vmovdqu   ymm2, [rax]          ; load 8 ints
vpcmpgtd  ymm0, ymm2, ymm3     ; compare all 8 with 127 → mask
vpand     ymm0, ymm0, ymm2     ; x or 0, still 32-bit
vpmovsxdq ymm2, xmm0           ; widen low 4 to long long
vextracti128 xmm0, ymm0, 1     ; upper 4
vpmovsxdq ymm0, xmm0           ; widen upper 4
vpaddq ×2                      ; add both halves into ymm1: 4 running 64-bit sums
cmp / jne                      ; loop
```

The parallel work: 8 ints are loaded, compared and masked together, one instruction per step. They're then widened to 8 long longs, which need 2 registers of 4 lanes each. Each `vpaddq` adds one register's 4 values into `ymm1` at once. `ymm1` holds 4 running 64-bit sums, one per lane, and they're added together into one total after the loop.

| | SSE2: instructions per 4 ints | AVX2: instructions per 8 ints |
|---|---|---|
| `sum_if` | 19 | 14 |
| `sum_mask` | 12 | 9 |

- `sum_if` says "if `x` passes, add `x` as a `long long`", so GCC widens **both the values and the masks** to 64-bit, then ANDs them.
- `sum_mask` ANDs **first**, while the values are 32-bit, then widens once.

### `-O2` with only the vectoriser changed

`-O2 -fvect-cost-model=dynamic` gives `-O2` the full vectoriser cost model, without the rest of `-O3` (extra inlining, unrolling and unswitching). Medians of 5 repetitions, both SSE2:

| | `-O2 -fvect-cost-model=dynamic` | `-O3` |
|---|---|---|
| `sum_if` | 0.37 ms · 0.35 ns | 0.37 ms · 0.35 ns |
| `sum_mask` | 0.25 ms · 0.24 ns | 0.25 ms · 0.24 ns |

For this code they're identical: the vectoriser is the only part of `-O3` that matters here. One flag at `-O2` gets the whole speed-up, without the code-size cost of `-O3` everywhere else.

### Why AVX2 isn't 2x faster than SSE2

Twice the width and fewer instructions, but `sum_mask` only went from 0.25 to 0.17 ms. At ~0.2 ns per element the loop is probably limited by how fast data arrives from the caches: the 4 MiB array fits in L3 but not in L1 or L2. That's the next experiment.

Note: a `-march=native` build only runs on CPUs with the same instruction sets. On a CPU without AVX2 it crashes with "illegal instruction".

## Build

Requires CMake ≥ 3.20, GCC or Clang, and git (Google Benchmark is fetched automatically). Results above: Ubuntu 24.04, GCC 13.3.

Each section uses different flags in `target_compile_options` in `CMakeLists.txt`:

| Section | Flags |
|---|---|
| 1 | `-O2 -fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize` |
| 2 | `-O2` |
| 3 | `-O3`, `-O3 -march=native`, `-O2 -fvect-cost-model=dynamic` |

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bench
```

Always benchmark a **Release** build.

Google Benchmark warns if CPU frequency scaling is on: Linux changes the clock speed with load, which adds noise to timings. The gaps here are large enough not to matter. For steadier numbers, fix the clock at its maximum while benchmarking, then put back whatever governor you had before (`cpupower` is in Ubuntu's `linux-tools-common` package):

```
old=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)   # remember the current one, e.g. schedutil or powersave
sudo cpupower frequency-set --governor performance
./build/bench
sudo cpupower frequency-set --governor "$old"                       # restore it
```

## Next

- Multiple accumulators (`s0`…`s3`) in the scalar version, to measure the dependency chain directly
- `perf stat -e branches,branch-misses` to count mispredicts directly
- Memory hierarchy: pointer chasing across array sizes to measure L1 / L2 / L3 / RAM latency
- Write bandwidth
