// Infrastructure smoke fuzzer; replace with real parsers/readers once they exist.
#include <jio/jio.hpp>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    (void)data;
    (void)size;
    (void)jio::version_major;
    return 0;
}
