// Infrastructure smoke fuzzer; replace with real parsers/readers once they exist.
#include <jetio/jetio.hpp>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    (void)data;
    (void)size;
    (void)jetio::version_major;
    return 0;
}
