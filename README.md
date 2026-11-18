# performance

Experiments in how CPU architecture affects C++ performance, measured with [Google Benchmark](https://github.com/google/benchmark).

## 1. Branch prediction: `if` vs branchless

`bench.cpp` sums every element ≥ 128 in an array of 2²⁰ random ints (0–255), two ways:

- `sum_if`: a plain `if`, so one conditional jump per element
- `sum_mask`: branchless, `s += x & -(x >= 128)`

Each runs on unsorted data (the branch is a coin flip) and sorted data (the branch is predictable).

### Results (4-core 3.8 GHz, 32 KiB L1d, 256 KiB L2, 6 MiB L3, GCC -O2)

| | unsorted | sorted |
|---|---|---|
| `sum_if` | 3.61 ms | 0.52 ms |
| `sum_mask` | 0.62 ms | 0.61 ms |

- Mispredicts make `sum_if` ~7x slower on random data.
- `sum_mask` has no branch, so the order of the data doesn't matter.
- On predictable data the branch is nearly free, and slightly beats the mask.
- Estimated mispredict penalty: (3.61 − 0.52) ms ÷ 2²⁰ elements ≈ 2.9 ns ≈ 11 cycles per element. About half the elements mispredict, so ≈ 22 cycles per miss.

`-fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize` stop GCC from removing the branch itself (with `cmov` or SIMD). Remove them to see what the optimiser does on its own.

## Build

Requires CMake ≥ 3.20, GCC or Clang, and git (Google Benchmark is fetched automatically).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bench
```

Always benchmark a **Release** build. For less noise, set the CPU governor to performance first:
`sudo cpupower frequency-set --governor performance`

## Next

- Memory hierarchy: pointer chasing across array sizes to measure L1 / L2 / L3 / RAM latency
- Write bandwidth
