#include <jetio/jetio.hpp>

#include <cstdlib>
#include <exception>
#include <print>

int main()
{
    try
    {
        std::println("jetio {}.{}.{}", jetio::version_major, jetio::version_minor,
                     jetio::version_patch);
    }
    catch (const std::exception &)
    {
        return EXIT_FAILURE;
    }
}
