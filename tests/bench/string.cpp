// Building a string from three pieces: what the concator of `a + b + c` costs, against what one
// writes without it. The pieces are strings of n characters, so the result has 3n.
//   concator           string s = a + b + c;             one allocation, the pieces copied once
//   concat             string s = concat(a, b, c);       the same, as a function
//   std_string_append  std::string t = a; t += b; t += c; string s = t;   (and its reserved twin)
//   std_string_plus    std::string s = a + b + c;        for reference, std::string all through
//
// Small strings decide most of what is seen, so the lengths are chosen around their limits: a
// jio::string keeps up to 15 characters in the object (jio::string::tiny_capacity), a std::string
// up to 15 (libstdc++, MSVC) or 22 (libc++). A result of 12 characters needs no allocation
// anywhere; 18 is a block for the jio::string and in libstdc++, but still inside the object for
// a std::string of libc++; 48 and 192 are the common sizes of names and paths, where every
// string allocates. The intermediate std::string of the append cases has the same limits.
// The counter `allocs` is the number of allocations of one iteration.
//
// The cases `string_baseline_*` measure the parts, for the same total lengths: the loop alone, a
// copy of a jio::string (a block is shared: an atomic increment and decrement) and of a
// std::string, a jio::string made from a view (one allocation and a copy), and the allocator.
//
//   jetio_benchmarks --benchmark_filter='^string_' --benchmark_min_time=0.1s
#include <jio/jio.hpp>

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

// Every allocation of the benchmark thread is counted, to see how many each way takes: the memory
// of jio::string comes from the default resource of std::pmr, which is operator new. (The
// replacement is for the whole of jetio_benchmarks; it only adds a count to malloc.)
namespace
{
    thread_local std::size_t allocations = 0;
}

void *operator new(std::size_t size)
{
    ++allocations;
    if (void *p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
// libstdc++ makes the memory of std::pmr::new_delete_resource with the aligned form
void *operator new(std::size_t size, std::align_val_t alignment)
{
    ++allocations;
    const auto align = static_cast<std::size_t>(alignment);
    if (void *p = std::aligned_alloc(align, (size + align - 1) / align * align))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t, std::align_val_t) noexcept
{
    std::free(p);
}

namespace
{
    struct pieces
    {
        explicit pieces(std::size_t n)
            : sa(n, 'a'), sb(n, 'b'), sc(n, 'c'), a{std::string_view(sa)}, b{std::string_view(sb)},
              c{std::string_view(sc)}
        {
        }

        std::string sa, sb, sc;
        jio::string a, b, c;
    };

    // n characters in a piece: 12, 18, 48 and 192 in the result
    void lengths(benchmark::Benchmark *b)
    {
        for (const int n : {4, 6, 16, 64})
            b->Arg(n);
    }

    // One string of 3n characters, as a std::string and as a jio::string.
    struct whole
    {
        explicit whole(std::size_t n) : s(3 * n, 'x'), j{std::string_view(s)} {}

        std::string s;
        jio::string j;
    };

    // The allocations are counted from `before`, the count at the start of the loop.
    void count(benchmark::State &state, std::size_t before)
    {
        state.SetBytesProcessed(state.iterations() * 3 * state.range(0));
        state.SetLabel(std::to_string(3 * state.range(0)) + " chars");
        state.counters["allocs"] = benchmark::Counter(static_cast<double>(allocations - before),
                                                      benchmark::Counter::kAvgIterations);
    }
} // namespace

static void string_concator(benchmark::State &state)
{
    const pieces p(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        jio::string s = p.a + p.b + p.c;
        benchmark::DoNotOptimize(s);
    }
    count(state, before);
}

static void string_concat(benchmark::State &state)
{
    const pieces p(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        jio::string s = jio::concat(p.a, p.b, p.c);
        benchmark::DoNotOptimize(s);
    }
    count(state, before);
}

static void string_std_string_append(benchmark::State &state)
{
    const pieces p(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        std::string t(std::string_view(p.a));
        t += std::string_view(p.b);
        t += std::string_view(p.c);
        jio::string s(t);
        benchmark::DoNotOptimize(s);
    }
    count(state, before);
}

static void string_std_string_append_reserved(benchmark::State &state)
{
    const pieces p(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        std::string t;
        t.reserve(p.a.size() + p.b.size() + p.c.size());
        t += std::string_view(p.a);
        t += std::string_view(p.b);
        t += std::string_view(p.c);
        jio::string s(t);
        benchmark::DoNotOptimize(s);
    }
    count(state, before);
}

static void string_std_string_plus(benchmark::State &state)
{
    const pieces p(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        std::string s = p.sa + p.sb + p.sc;
        benchmark::DoNotOptimize(s);
    }
    count(state, before);
}

BENCHMARK(string_concator)->Apply(lengths);
BENCHMARK(string_concat)->Apply(lengths);
BENCHMARK(string_std_string_append)->Apply(lengths);
BENCHMARK(string_std_string_append_reserved)->Apply(lengths);
BENCHMARK(string_std_string_plus)->Apply(lengths);

static void string_baseline_loop(benchmark::State &state)
{
    whole w(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
        benchmark::DoNotOptimize(w.j);
    count(state, before);
}

static void string_baseline_copy(benchmark::State &state)
{
    const whole w(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        jio::string t = w.j;
        benchmark::DoNotOptimize(t);
    }
    count(state, before);
}

static void string_baseline_copy_std_string(benchmark::State &state)
{
    const whole w(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        std::string t = w.s;
        benchmark::DoNotOptimize(t);
    }
    count(state, before);
}

static void string_baseline_from_view(benchmark::State &state)
{
    const whole w(static_cast<std::size_t>(state.range(0)));
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        jio::string t{std::string_view(w.s)};
        benchmark::DoNotOptimize(t);
    }
    count(state, before);
}

// What a block of the string costs from the allocator alone: the size of the characters and the
// header of the block.
static void string_baseline_allocator(benchmark::State &state)
{
    const std::size_t size = 3 * static_cast<std::size_t>(state.range(0)) + 1 + 16;
    const std::size_t before = allocations;
    for (auto _ : state)
    {
        void *p = ::operator new(size);
        benchmark::DoNotOptimize(p);
        ::operator delete(p);
    }
    count(state, before);
}

BENCHMARK(string_baseline_loop)->Apply(lengths);
BENCHMARK(string_baseline_copy)->Apply(lengths);
BENCHMARK(string_baseline_copy_std_string)->Apply(lengths);
BENCHMARK(string_baseline_from_view)->Apply(lengths);
BENCHMARK(string_baseline_allocator)->Apply(lengths);
