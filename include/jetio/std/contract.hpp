#pragma once

// Design by contract, simulated for C++23 (see docs/design/contracts.md).
//
//   JIO_Expects(cond)  precondition, checked on entry
//   JIO_Ensures(cond)  postcondition, checked before returning
//   JIO_Assert(cond)   assertion
//
// Each has a _critical variant (also checked in release builds) and an _audit variant
// (expensive, debug builds only), except JIO_Ensures_critical. They correspond to C++26 pre(),
// post() and contract_assert().
//
// JIO_CONTRACT_LEVEL: 0 nothing is checked, 1 critical, 2 plus default, 3 plus audit. The
// default is 1 with NDEBUG and 3 without. Use the same level in the whole program.
//
// A violation calls the handler set with set_contract_violation_handler. The handler must not
// return: it terminates (the default does) or, in tests, throws.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <string_view>

#if !defined(JIO_CONTRACT_LEVEL)
#if defined(NDEBUG)
#define JIO_CONTRACT_LEVEL 1
#else
#define JIO_CONTRACT_LEVEL 3
#endif
#endif

namespace jetio
{
    inline constexpr int contract_level = JIO_CONTRACT_LEVEL;

    enum class contract_kind : std::uint8_t
    {
        precondition,
        postcondition,
        assertion
    };

    struct contract_violation
    {
        contract_kind kind;
        std::string_view expression;
        std::source_location location;
    };

    /// Must not return: terminate the program or, in tests, throw.
    using contract_violation_handler = void (*)(const contract_violation &);

    namespace detail
    {
        constexpr std::string_view to_string(contract_kind kind) noexcept
        {
            switch (kind)
            {
            case contract_kind::precondition:
                return "precondition";
            case contract_kind::postcondition:
                return "postcondition";
            case contract_kind::assertion:
                return "assertion";
            }
            return "contract";
        }

        // The pointers of std::source_location and of stderr are the boundary: they become
        // string_views at once, and are written with their length.
        [[noreturn]] inline void abort_on_violation(const contract_violation &violation)
        {
            const std::string_view file = violation.location.file_name();
            const std::string_view function = violation.location.function_name();
            const std::string_view kind = to_string(violation.kind);
            // NOLINTNEXTLINE(modernize-use-std-print): std::println can throw, this must not
            std::fprintf(
                stderr, "%.*s(%u): %.*s: %.*s violated: %.*s\n", static_cast<int>(file.size()),
                file.data(), static_cast<unsigned>(violation.location.line()),
                static_cast<int>(function.size()), function.data(), static_cast<int>(kind.size()),
                kind.data(), static_cast<int>(violation.expression.size()),
                violation.expression.data());
            std::abort();
        }

        inline std::atomic<contract_violation_handler> violation_handler{&abort_on_violation};
    } // namespace detail

    /// Replaces the contract violation handler. The handler must not return, see
    /// contract_violation_handler.
    /// @param handler the new handler; nullptr restores the default one
    /// @return the previous handler
    /// @post the stored handler != nullptr
    inline contract_violation_handler
    set_contract_violation_handler(contract_violation_handler handler) noexcept
    {
        return detail::violation_handler.exchange(handler ? handler : &detail::abort_on_violation);
    }

    /// Reports a violated contract. Called by the macros; not meant to be called directly.
    /// Never returns normally: the handler terminates or throws, and if it returns the program
    /// is aborted.
    [[noreturn]] inline void
    handle_contract_violation(contract_kind kind, std::string_view expression,
                              std::source_location location = std::source_location::current())
    {
        detail::violation_handler.load()(
            contract_violation{.kind = kind, .expression = expression, .location = location});
        std::fputs("jetio: the contract violation handler returned\n", stderr);
        std::abort();
    }
} // namespace jetio

// The condition is not evaluated if the level is too low, but it is always compiled.
#define JIO_CONTRACT_CHECK_(level, kind, ...)                                                      \
    static_cast<void>(                                                                             \
        (JIO_CONTRACT_LEVEL < (level)) || static_cast<bool>(__VA_ARGS__) ||                        \
        (::jetio::handle_contract_violation(::jetio::contract_kind::kind, #__VA_ARGS__), false))

#define JIO_Expects_critical(...) JIO_CONTRACT_CHECK_(1, precondition, __VA_ARGS__)
#define JIO_Assert_critical(...) JIO_CONTRACT_CHECK_(1, assertion, __VA_ARGS__)
#define JIO_Expects(...) JIO_CONTRACT_CHECK_(2, precondition, __VA_ARGS__)
#define JIO_Ensures(...) JIO_CONTRACT_CHECK_(2, postcondition, __VA_ARGS__)
#define JIO_Assert(...) JIO_CONTRACT_CHECK_(2, assertion, __VA_ARGS__)
#define JIO_Expects_audit(...) JIO_CONTRACT_CHECK_(3, precondition, __VA_ARGS__)
#define JIO_Ensures_audit(...) JIO_CONTRACT_CHECK_(3, postcondition, __VA_ARGS__)
#define JIO_Assert_audit(...) JIO_CONTRACT_CHECK_(3, assertion, __VA_ARGS__)
