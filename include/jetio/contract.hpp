#pragma once

// Design by contract, simulated for C++23.
//
//   JIO_Expects(cond)           precondition: caller's obligation, checked on entry
//   JIO_Ensures(cond)           postcondition: callee's obligation, checked before returning
//   JIO_Assert(cond)            internal assertion
//   JIO_Expects_critical(cond)  same, but kept in release builds, see below
//   JIO_Assert_critical(cond)
//   JIO_Expects_audit(cond)     same, for checks too expensive for every build
//   JIO_Ensures_audit(cond)
//   JIO_Assert_audit(cond)
//
// They map one-to-one to C++26 pre(), post() and contract_assert(), so they can be replaced
// mechanically once the library moves to C++26.
//
// JIO_CONTRACT_LEVEL selects what is checked (define it for the whole program):
//   0  nothing is checked or evaluated
//   1  critical checks only
//   2  critical and default checks
//   3  critical, default and audit checks
// If not defined it is 1 with NDEBUG and 3 without NDEBUG.
//
// The plain macros cost nothing in release builds, so write them freely. Use a _critical macro
// only if a violation would otherwise lead to memory unsafety, undefined behavior or
// unrecoverable data corruption, and the check is O(1) and does not call into a device.
// Use an _audit macro if the check is expensive or calls into a device.
//
// A violated contract calls the violation handler. The default handler reports the violation to
// stderr and aborts. A handler that returns lets execution continue ("observe" semantics).
// Use a different level in different translation units only if you accept an ODR violation in
// the inline functions.

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

    using contract_violation_handler = void (*)(const contract_violation &);

    namespace detail
    {
        constexpr const char *to_string(contract_kind kind) noexcept
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

        [[noreturn]] inline void abort_on_violation(const contract_violation &violation)
        {
            // NOLINTNEXTLINE(modernize-use-std-print): std::println can throw, this must not
            std::fprintf(stderr, "%s(%u): %s: %s violated: %.*s\n", violation.location.file_name(),
                         static_cast<unsigned>(violation.location.line()),
                         violation.location.function_name(), to_string(violation.kind),
                         static_cast<int>(violation.expression.size()),
                         violation.expression.data());
            std::abort();
        }

        inline std::atomic<contract_violation_handler> violation_handler{&abort_on_violation};
    } // namespace detail

    /// Replaces the contract violation handler.
    /// @param handler the new handler; nullptr restores the default one
    /// @return the previous handler
    /// @post the handler is never null
    inline contract_violation_handler
    set_contract_violation_handler(contract_violation_handler handler) noexcept
    {
        return detail::violation_handler.exchange(handler ? handler : &detail::abort_on_violation);
    }

    /// Reports a violated contract. Called by the macros; not meant to be called directly.
    inline void
    handle_contract_violation(contract_kind kind, std::string_view expression,
                              std::source_location location = std::source_location::current())
    {
        detail::violation_handler.load()(
            contract_violation{.kind = kind, .expression = expression, .location = location});
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
