// Infrastructure smoke benchmark; replace with real IO benchmarks.
#include <jetio/jetio.hpp>

#include <benchmark/benchmark.h>

static void version_sum(benchmark::State &state)
{
    for (auto _ : state)
    {
        int sum = jetio::version_major + jetio::version_minor + jetio::version_patch;
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(version_sum);
