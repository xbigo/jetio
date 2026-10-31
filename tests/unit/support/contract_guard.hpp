#pragma once

#include <jetio/std/contract.hpp>

#include <exception>
#include <string>

namespace jetio::test
{
    /// Thrown by the handler installed by scoped_throwing_contract_handler.
    struct contract_failure : std::exception
    {
        explicit contract_failure(const contract_violation &v)
            : kind(v.kind), expression(v.expression)
        {
        }
        const char *what() const noexcept override { return expression.c_str(); }

        contract_kind kind;
        std::string expression;
    };

    /// Turns contract violations into contract_failure exceptions while it exists. Functions
    /// that are noexcept cannot be tested this way.
    class scoped_throwing_contract_handler
    {
    public:
        scoped_throwing_contract_handler() noexcept
            : m_previous(set_contract_violation_handler(&throw_failure))
        {
        }
        ~scoped_throwing_contract_handler() { set_contract_violation_handler(m_previous); }
        scoped_throwing_contract_handler(const scoped_throwing_contract_handler &) = delete;
        scoped_throwing_contract_handler &
        operator=(const scoped_throwing_contract_handler &) = delete;

    private:
        static void throw_failure(const contract_violation &v) { throw contract_failure(v); }

        contract_violation_handler m_previous;
    };
} // namespace jetio::test
