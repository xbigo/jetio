#pragma once

#include <jetio/contract.hpp>

#include <exception>
#include <string>

namespace jetio::test
{
    /// Thrown by the handler installed by throwing_contract_handler.
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

    /// Turns contract violations into contract_failure exceptions for the lifetime of the
    /// object. Only functions that are not noexcept can be tested this way.
    class throwing_contract_handler
    {
    public:
        throwing_contract_handler() noexcept
            : m_previous(set_contract_violation_handler(&throw_failure))
        {
        }
        ~throwing_contract_handler() { set_contract_violation_handler(m_previous); }
        throwing_contract_handler(const throwing_contract_handler &) = delete;
        throwing_contract_handler &operator=(const throwing_contract_handler &) = delete;

    private:
        static void throw_failure(const contract_violation &v) { throw contract_failure(v); }

        contract_violation_handler m_previous;
    };
} // namespace jetio::test
