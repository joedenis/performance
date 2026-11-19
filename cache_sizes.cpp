// summing arrays that fit in L1, L2, L3 or only RAM
// are the vectorised loops time constrained by limited instructions being fed or by
// how quickly data arrives from memory
// build with -03 -march=native (AVX2)

#include <benchmark/benchmark.h>

#include <cstddef>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

    // unsorted random ints 0-255.
    const std::vector<int>& data_of_size(std::size_t n) {
        static std::map<std::size_t, std::vector<int>> cache;
        auto& v = cache[n];
        if (v.empty()) {
            v.resize(n); // add n elements
            std::mt19937 rng(42);
            for (auto& x: v) x = static_cast<int>(rng() % 256);
        }
        return v;
    }

    [[gnu::noinline]] long long sum_if(const std::vector<int>& v) {
        long long s = 0;
        for (int x : v) {
            if (x >= 128) s += x;
        }
        return s;
    }

    [[gnu::noinline]]long long sum_mask(const std::vector<int>& v) {
        long long s = 0;
        for (int x : v) {
            s += x & -static_cast<int>(x >= 128);
        }
        return s;
    }

    std::string size_label(std::size_t bytes) {
        if (bytes >= (1u << 20)) return std::to_string(bytes >> 20) + " MiB";
        return std::to_string(bytes >> 10) + " KiB";
    }

    template <long long (*Fn)(const std::vector<int>&)>
    void BM_SumBySize(benchmark::State& state) {
        const std::size_t n = std::size_t{1} << state.range(0);   // 2^arg ints
        const auto& v = data_of_size(n);                          // setup: not timed
        for (auto _ : state) {
            long long s = Fn(v);
            benchmark::DoNotOptimize(s);
        }
        const auto elements = state.iterations() * static_cast<long long>(n);
        state.SetItemsProcessed(elements);
        state.SetBytesProcessed(elements * static_cast<long long>(sizeof(int)));
        state.SetLabel(size_label(n * sizeof(int)));
    }

} // namespace


// Arg = log2(number of ints): 2^12 = 16 KiB (L1), 2^15 = 128 KiB (L2),
// 2^20 = 4 MiB (L3), 2^24 = 64 MiB (RAM only)
BENCHMARK_TEMPLATE(BM_SumBySize, sum_if)->Arg(12)->Arg(15)->Arg(20)->Arg(24);
BENCHMARK_TEMPLATE(BM_SumBySize, sum_mask)->Arg(12)->Arg(15)->Arg(20)->Arg(24);

BENCHMARK_MAIN();
