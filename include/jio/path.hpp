#pragma once

#include <filesystem>

namespace jio
{
    /// The type of file names. For now the standard one, until the project decides whether it
    /// needs a path type of its own.
    using path = std::filesystem::path;
} // namespace jio
