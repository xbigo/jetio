// Operations on a memory_device through the protocol functions. Run in a release preset and
// in its -audit twin to see what contract checking costs.
#include <jetio/jetio.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <vector>

namespace
{
    constexpr std::size_t device_size = 1 << 16;

    jetio::memory_device<> make_device()
    {
        return jetio::memory_device<>(
            jetio::vector_buffer_represent{std::vector<std::byte>(device_size), 0});
    }
} // namespace

static void memory_read(benchmark::State &state)
{
    auto device = make_device();
    std::vector<std::byte> out(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state)
    {
        jetio::seek(device, 0);
        benchmark::DoNotOptimize(jetio::read(device, out));
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(memory_read)->Arg(16)->Arg(256)->Arg(4096);

static void memory_write(benchmark::State &state)
{
    auto device = make_device();
    const std::vector<std::byte> data(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state)
    {
        jetio::seek(device, 0);
        benchmark::DoNotOptimize(jetio::write(device, data));
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(memory_write)->Arg(16)->Arg(256)->Arg(4096);

static void memory_view_rd(benchmark::State &state)
{
    auto device = make_device();
    for (auto _ : state)
        benchmark::DoNotOptimize(jetio::view_rd(device, {128, 128 + state.range(0)}));
}
BENCHMARK(memory_view_rd)->Arg(16)->Arg(4096);

// Many small reads: the case where per-call checks matter most.
static void memory_read_small_sequence(benchmark::State &state)
{
    auto device = make_device();
    std::array<std::byte, 4> out;
    for (auto _ : state)
    {
        jetio::seek(device, 0);
        for (std::size_t i = 0; i < device_size / out.size(); ++i)
            benchmark::DoNotOptimize(jetio::read(device, out));
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(device_size / out.size()));
}
BENCHMARK(memory_read_small_sequence);
