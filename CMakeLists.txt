cmake_minimum_required(VERSION 3.20)
project(branch_bench CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Download and build Google Benchmark as part of this project.
include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.1
)
FetchContent_MakeAvailable(benchmark)

add_executable(bench bench.cpp)
target_link_libraries(bench PRIVATE benchmark::benchmark)

# Keep the branch in sum_if: stop GCC turning it into cmov or SIMD.
# Remove these two flags to see what the optimiser does on its own.
#target_compile_options(bench PRIVATE -O2 -fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize)

target_compile_options(bench PRIVATE -O2 )

# Section 4: the same sums over arrays that fit in L1, L2, L3, or only RAM.
# Its own program, with its own flags: always the AVX2 build.
add_executable(cache_sizes cache_sizes.cpp)
target_link_libraries(cache_sizes PRIVATE benchmark::benchmark)
target_compile_options(cache_sizes PRIVATE -O3 -march=native)



