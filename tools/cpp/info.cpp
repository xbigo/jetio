#include <jio/jio.hpp>

#include <cstdlib>
#include <exception>
#include <print>

int main()
{
    try
    {
        std::println("jetio {}.{}.{}", jio::version_major, jio::version_minor, jio::version_patch);
    }
    catch (const std::exception &)
    {
        return EXIT_FAILURE;
    }
}
