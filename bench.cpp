// Branch prediction benchmark: if vs branchless, on unsorted vs sorted data.
// Build: see CMakeLists.txt (fetches Google Benchmark automatically).
#include <benchmark/benchmark.h>

#include <algorithm>
#include <random>
#include <vector>

namespace {

constexpr std::size_t kN = 1 << 20;   // 1M ints (4 MB)

std::vector<int> make_data(bool sorted) {
    std::vector<int> v(kN);
    std::mt19937 rng(42);
    for (auto& x : v) x = static_cast<int>(rng() % 256);
    if (sorted) std::sort(v.begin(), v.end());
    return v;
}

// Plain if: one conditional jump per element.
// noinline keeps the function a separate unit so we can inspect its assembly.
[[gnu::noinline]] long long sum_if(const std::vector<int>& v) {
    long long s = 0;
    for (int x : v)
        if (x >= 128) s += x;
    return s;
}

// Branchless: the comparison becomes a 0 / -1 mask, so the add always runs.
[[gnu::noinline]] long long sum_mask(const std::vector<int>& v) {
    long long s = 0;
    for (int x : v)
        s += x & -static_cast<int>(x >= 128);
    return s;
}

template <long long (*Fn)(const std::vector<int>&)>
void BM_Sum(benchmark::State& state) {
    const auto v = make_data(state.range(0) != 0);    // setup: not timed
    for (auto _ : state) {
        long long s = Fn(v);
        benchmark::DoNotOptimize(s);                   // stop the compiler deleting the work
    }
    state.SetItemsProcessed(state.iterations() * static_cast<long long>(kN));
    state.SetLabel(state.range(0) ? "sorted" : "unsorted");
}

}  // namespace

// Arg(0) = unsorted, Arg(1) = sorted
BENCHMARK_TEMPLATE(BM_Sum, sum_if)->Arg(0)->Arg(1);
BENCHMARK_TEMPLATE(BM_Sum, sum_mask)->Arg(0)->Arg(1);

BENCHMARK_MAIN();
