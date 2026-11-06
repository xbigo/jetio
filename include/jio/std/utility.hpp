#pragma once

// Small helpers that the standard library lacks.

namespace jio
{
    /// Marks the arguments as used, to silence unused warnings:
    /// `might_unused(x, y, "why they are unused");`. The arguments are evaluated and ignored.
    template <typename... Args> constexpr void might_unused(Args &&...) noexcept {}
} // namespace jio
