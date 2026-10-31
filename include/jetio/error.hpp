#pragma once

// Error reporting for operations that can fail for reasons the caller cannot check beforehand
// (IO failures, out of memory, platform limits). Programming errors are contract violations,
// see contract.hpp.
//
// Every such operation takes a trailing `error_code_ptr ec`:
//   ec != nullptr  the operation never throws io_error; *ec is cleared on success and set on
//                  failure
//   ec == nullptr  the operation throws io_error on failure

#include <jetio/contract.hpp>

#include <system_error>
#include <type_traits>

namespace jetio
{
    using error_code = std::error_code;

    /// Nullable out-parameter, see above. Not owned.
    using error_code_ptr = error_code *;

    /// Thrown by operations that were not given an error_code_ptr.
    class io_error : public std::system_error
    {
    public:
        using std::system_error::system_error;
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
    /// @pre e is an error (not the success value)
    /// @throws io_error carrying e if ec is null
    inline void set_error_or_throw(error_code_ptr ec, error_code e)
    {
        JIO_Expects(static_cast<bool>(e));
        if (ec)
            *ec = e;
        else
            throw io_error(e);
    }

    template <error_enum E> inline void set_error_or_throw(error_code_ptr ec, E e)
    {
        set_error_or_throw(ec, make_error_code(e));
    }
} // namespace jetio
