#pragma once

// Error reporting for operations that can fail for reasons the caller cannot check beforehand
// (IO failures, out of memory, platform limits). Programming errors are contract violations,
// see std/contract.hpp.
//
// Every such operation takes a trailing `error_code_ptr ec`:
//   ec != nullptr  the operation never throws its exception; *ec is cleared on success and set
//                  on failure
//   ec == nullptr  the operation throws on failure: io_error for the devices, a type of its
//                  own for the other parts (utf8::invalid_utf8_code, ...), all derived from
//                  jio::exception

#include <jio/std/contract.hpp>

#include <concepts>
#include <source_location>
#include <string>
#include <system_error>
#include <type_traits>

namespace jio
{
    using error_code = std::error_code;

    /// Nullable out-parameter, see above. Not owned.
    using error_code_ptr = error_code *;

    /// The base of the exceptions of the library: a std::system_error, with the error code, that
    /// also tells where it was thrown. Define a type by deriving from it, with the constructors:
    ///   struct bad_hex_input : exception { using exception::exception; };
    class exception : public std::system_error
    {
    public:
        /// @param where the line that reports the failure; by default the one of the caller,
        ///        which for the helpers below is the caller of the helper
        explicit exception(error_code code,
                           std::source_location where = std::source_location::current())
            : std::system_error(code), m_where(where)
        {
        }
        exception(error_code code, const std::string &message,
                  std::source_location where = std::source_location::current())
            : std::system_error(code, message), m_where(where)
        {
        }

        const std::source_location &where() const noexcept { return m_where; }

    private:
        std::source_location m_where;
    };

    /// Thrown by the devices when they were not given an error_code_ptr.
    class io_error : public exception
    {
    public:
        using exception::exception;
    };

    template <typename E>
    concept error_enum = std::is_error_code_enum_v<E> || std::is_error_condition_enum_v<E>;

    /// Marks success.
    inline void clear_error(error_code_ptr ec) noexcept
    {
        if (ec)
            ec->clear();
    }

    /// @return true if ec is not null and holds an error
    inline bool has_error(const error_code *ec) noexcept
    {
        return ec && *ec;
    }

    /// Reports a failure.
    /// @pre e != error_code()
    /// @throws Exception carrying e (and `message` and the place of the caller) if ec is null
    template <std::derived_from<exception> Exception>
    void set_error_or_throw(error_code_ptr ec, error_code e, const std::string &message = {},
                            std::source_location where = std::source_location::current())
    {
        JIO_Expects(static_cast<bool>(e));
        if (ec)
            *ec = e;
        else if (message.empty())
            throw Exception(e, where);
        else
            throw Exception(e, message, where);
    }

    /// Reports a failure of a device: io_error.
    /// @pre e != error_code()
    /// @throws io_error carrying e if ec is null
    inline void set_error_or_throw(error_code_ptr ec, error_code e,
                                   std::source_location where = std::source_location::current())
    {
        set_error_or_throw<io_error>(ec, e, {}, where);
    }

    template <error_enum E>
    void set_error_or_throw(error_code_ptr ec, E e,
                            std::source_location where = std::source_location::current())
    {
        set_error_or_throw<io_error>(ec, make_error_code(e), {}, where);
    }

    template <std::derived_from<exception> Exception, error_enum E>
    void set_error_or_throw(error_code_ptr ec, E e, const std::string &message = {},
                            std::source_location where = std::source_location::current())
    {
        set_error_or_throw<Exception>(ec, make_error_code(e), message, where);
    }
} // namespace jio
