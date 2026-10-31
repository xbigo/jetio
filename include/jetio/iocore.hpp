#pragma once

// The device protocol.
//
// A device is any type that provides some of the member functions below. Each free function
// forwards to the member of the same name, adds the default `ec` argument and checks the
// protocol-level contract. A concept (reader, writer, ...) tells whether a type provides the
// operation.
//
//   [ sequence, forward_access, random_access ]   offset, seek_forward, seek
//   [ reader, is_eofer, sizer, read_map ]
//   [ writer, syncer, truncater, write_map ]
//   [ options ]
//
// Conventions
//   - `ec` is the error_code_ptr of error.hpp. Failures a caller cannot check beforehand are
//     reported through it (or io_error if it is null). Violating a documented precondition is
//     a contract violation, see contract.hpp.
//   - Offsets and sizes are in bytes and 64 bit (size64_t). Seeking beyond the end is
//     allowed; reading there yields no data, writing there extends the device if it can grow.
//   - Exception safety: a device is always left valid, destructible and usable. Individual
//     operations document anything stronger. An operation that reports an error has made
//     progress only where it says so.
//   - Operations with a precondition are not noexcept.

#include <jetio/contract.hpp>
#include <jetio/error.hpp>
#include <jetio/types.hpp>

#include <any>
#include <concepts>
#include <cstddef>
#include <span>
#include <utility>

namespace jetio
{
    using mutable_buffer = std::span<std::byte>;
    using const_buffer = std::span<const std::byte>;

    namespace detail
    {
        /// @return true if `part` is the first part.size() bytes of `whole`
        constexpr bool is_prefix(const_buffer part, const_buffer whole) noexcept
        {
            return part.size() <= whole.size() && part.data() == whole.data();
        }

        /// @return true if `part` is the last part.size() bytes of `whole`
        constexpr bool is_suffix(const_buffer part, const_buffer whole) noexcept
        {
            return part.size() <= whole.size() &&
                   part.data() + part.size() == whole.data() + whole.size();
        }
    } // namespace detail

    // ---------------------------------------------------------------------------------------
    // read, reader
    // ---------------------------------------------------------------------------------------

    /// Reads up to buf.size() bytes at the current offset and advances the offset by the
    /// number of bytes read.
    /// @return the part of buf that was filled, always a prefix of buf. It is shorter than buf
    ///         only at the end of the data or after an error.
    /// @post the result is a prefix of buf
    template <typename Device>
        requires requires(Device &&device, mutable_buffer buf, error_code_ptr ec) {
            { device.read(buf, ec) } -> std::convertible_to<mutable_buffer>;
        }
    mutable_buffer read(Device &&device, mutable_buffer buf, error_code_ptr ec = nullptr)
    {
        const mutable_buffer result = device.read(buf, ec);
        JIO_Ensures(detail::is_prefix(result, buf));
        return result;
    }
    template <typename Device>
    concept reader = requires(Device &&device, mutable_buffer buf, error_code_ptr ec) {
        { jetio::read(device, buf, ec) } -> std::convertible_to<mutable_buffer>;
    };

    // ---------------------------------------------------------------------------------------
    // write, writer
    // ---------------------------------------------------------------------------------------

    /// Writes buf at the current offset and advances the offset by the number of bytes
    /// written.
    /// @return the part of buf that was not written, always a suffix of buf. It is not empty
    ///         only after an error.
    /// @post the result is a suffix of buf
    template <typename Device>
        requires requires(Device &&device, const_buffer buf, error_code_ptr ec) {
            { device.write(buf, ec) } -> std::convertible_to<const_buffer>;
        }
    const_buffer write(Device &&device, const_buffer buf, error_code_ptr ec = nullptr)
    {
        const const_buffer result = device.write(buf, ec);
        JIO_Ensures(detail::is_suffix(result, buf));
        return result;
    }
    template <typename Device>
    concept writer = requires(Device &&device, const_buffer buf, error_code_ptr ec) {
        { jetio::write(device, buf, ec) } -> std::convertible_to<const_buffer>;
    };

    // ---------------------------------------------------------------------------------------
    // sync, syncer
    // ---------------------------------------------------------------------------------------

    /// Makes written data durable, as far as the device can.
    template <typename Device>
        requires requires(Device &&device, error_code_ptr ec) { device.sync(ec); }
    void sync(Device &&device, error_code_ptr ec = nullptr)
    {
        device.sync(ec);
    }
    template <typename Device>
    concept syncer = requires(Device &&device, error_code_ptr ec) { jetio::sync(device, ec); };

    // ---------------------------------------------------------------------------------------
    // is_eof, is_eofer
    // ---------------------------------------------------------------------------------------

    /// @return true if no data is left to read at the current offset
    template <typename Device>
        requires requires(Device &&device, error_code_ptr ec) {
            { device.is_eof(ec) } -> std::convertible_to<bool>;
        }
    bool is_eof(Device &&device, error_code_ptr ec = nullptr)
    {
        return device.is_eof(ec);
    }
    template <typename Device>
    concept is_eofer = requires(Device &&device, error_code_ptr ec) {
        { jetio::is_eof(device, ec) } -> std::convertible_to<bool>;
    };

    // ---------------------------------------------------------------------------------------
    // truncate, truncater
    // ---------------------------------------------------------------------------------------

    /// Sets the size of the device. Data beyond the new size is discarded; new space reads as
    /// zero. The offset is not changed.
    /// @return the size of the device afterwards
    template <typename Device>
        requires requires(Device &&device, size64_t size, error_code_ptr ec) {
            { device.truncate(size, ec) } -> std::convertible_to<size64_t>;
        }
    size64_t truncate(Device &&device, size64_t size, error_code_ptr ec = nullptr)
    {
        return device.truncate(size, ec);
    }
    template <typename Device>
    concept truncater = requires(Device &&device, size64_t size, error_code_ptr ec) {
        { jetio::truncate(device, size, ec) } -> std::convertible_to<size64_t>;
    };

    // ---------------------------------------------------------------------------------------
    // size, sizer
    // ---------------------------------------------------------------------------------------

    /// @return the size of the device, or unknown_size if it is not known
    template <typename Device>
        requires requires(Device &&device, error_code_ptr ec) {
            { device.size(ec) } -> std::convertible_to<size64_t>;
        }
    size64_t size(Device &&device, error_code_ptr ec = nullptr)
    {
        return device.size(ec);
    }
    template <typename Device>
    concept sizer = requires(Device &&device, error_code_ptr ec) {
        { jetio::size(device, ec) } -> std::convertible_to<size64_t>;
    };

    // ---------------------------------------------------------------------------------------
    // offset, sequence
    // ---------------------------------------------------------------------------------------

    /// @return the current offset
    template <typename Device>
        requires requires(Device &&device, error_code_ptr ec) {
            { device.offset(ec) } -> std::convertible_to<size64_t>;
        }
    size64_t offset(Device &&device, error_code_ptr ec = nullptr)
    {
        return device.offset(ec);
    }
    template <typename Device>
    concept sequence = requires(Device &&device, error_code_ptr ec) {
        { jetio::offset(device, ec) } -> std::convertible_to<size64_t>;
    };

    // ---------------------------------------------------------------------------------------
    // seek, random_access
    // ---------------------------------------------------------------------------------------

    /// Moves the current offset to `off`.
    /// @return the offset afterwards: `off`, unless the device documents otherwise (for
    ///         example empty_device, which has no offset but 0). On an error it is the unchanged
    ///         offset.
    template <typename Device>
        requires requires(Device &&device, size64_t off, error_code_ptr ec) {
            { device.seek(off, ec) } -> std::convertible_to<size64_t>;
        }
    size64_t seek(Device &&device, size64_t off, error_code_ptr ec = nullptr)
    {
        return device.seek(off, ec);
    }
    template <typename Device>
    concept random_access = requires(Device &&device, size64_t off, error_code_ptr ec) {
        { jetio::seek(device, off, ec) } -> std::convertible_to<size64_t>;
    } && sequence<Device>;

    // ---------------------------------------------------------------------------------------
    // seek_forward, forward_access
    // ---------------------------------------------------------------------------------------

    namespace detail
    {
        template <typename Device>
        concept has_seek_forward = requires(Device &&device, size64_t off, error_code_ptr ec) {
            { device.seek_forward(off, ec) } -> std::convertible_to<size64_t>;
        };

        /// Used in contracts only: true if `off` is not before the current offset (or the
        /// current offset cannot be obtained).
        template <typename Device> bool is_not_before_offset(Device &device, size64_t off)
        {
            if constexpr (sequence<Device &>)
            {
                error_code ec;
                const size64_t current = jetio::offset(device, &ec);
                return ec || off >= current;
            }
            else
            {
                return true;
            }
        }
    } // namespace detail

    /// Moves the current offset forward to `off`. Devices that cannot go back (streams) provide
    /// only this; every random access device gets it through seek.
    /// @pre `off` is not before the current offset (audit)
    /// @return the offset afterwards, see seek
    template <typename Device>
        requires detail::has_seek_forward<Device> ||
                 requires(Device &&device, size64_t off, error_code_ptr ec) {
                     { device.seek(off, ec) } -> std::convertible_to<size64_t>;
                 }
    size64_t seek_forward(Device &&device, size64_t off, error_code_ptr ec = nullptr)
    {
        JIO_Expects_audit(detail::is_not_before_offset(device, off));
        if constexpr (detail::has_seek_forward<Device>)
            return device.seek_forward(off, ec);
        else
            return device.seek(off, ec);
    }
    template <typename Device>
    concept forward_access = requires(Device &&device, size64_t off, error_code_ptr ec) {
        { jetio::seek_forward(device, off, ec) } -> std::convertible_to<size64_t>;
    } && sequence<Device>;

    // ---------------------------------------------------------------------------------------
    // address, read_view, write_view
    // ---------------------------------------------------------------------------------------

    /// A view gives direct access to bytes of a device. It is valid until the device is
    /// modified or destroyed, unless the device documents otherwise.
    /// @return the bytes of the view. Works for views that have address() and for
    ///         pointer-like handles to such views.
    template <typename View>
        requires requires(View &&view) { view.address(); } ||
                 requires(View &&view) { view->address(); }
    decltype(auto) address(View &&view)
    {
        if constexpr (requires { view.address(); })
            return view.address();
        else
            return view->address();
    }
    template <typename View>
    concept read_view = requires(View &&view) {
        { jetio::address(view) } -> std::convertible_to<const_buffer>;
    };
    template <typename View>
    concept write_view = requires(View &&view) {
        { jetio::address(view) } -> std::convertible_to<mutable_buffer>;
    };

    // ---------------------------------------------------------------------------------------
    // view_rd, read_map
    // ---------------------------------------------------------------------------------------

    /// Gives read access to the bytes in `rng` without copying them, if the device can.
    /// @pre is_valid_range(rng)
    /// @return the view; when the end of `rng` is unknown_offset it extends to the end of the
    ///         device. Reports errc::invalid_argument if the range lies beyond the device.
    /// @post unless an error occurred, the view has range_size(rng) bytes (end known)
    template <typename Device>
        requires requires(Device &&device, offset64_range rng, error_code_ptr ec) {
            { device.view_rd(rng, ec) } -> read_view;
        }
    decltype(auto) view_rd(Device &&device, offset64_range rng, error_code_ptr ec = nullptr)
    {
        JIO_Expects(is_valid_range(rng));
        decltype(auto) view = device.view_rd(rng, ec);
        JIO_Ensures(has_error(ec) || rng.end == unknown_offset ||
                    jetio::address(view).size() == range_size(rng));
        return view;
    }
    template <typename Device>
    concept read_map = requires(Device &&device, offset64_range rng, error_code_ptr ec) {
        { jetio::view_rd(device, rng, ec) } -> read_view;
    } && sizer<Device>;

    // ---------------------------------------------------------------------------------------
    // view_wr, write_map
    // ---------------------------------------------------------------------------------------

    /// Gives write access to the bytes in `rng` without copying them, if the device can. The
    /// device grows to fit the end of `rng` if it can.
    /// @pre is_valid_range(rng)
    /// @return the view, see view_rd
    /// @post unless an error occurred, the view has range_size(rng) bytes (end known)
    template <typename Device>
        requires requires(Device &&device, offset64_range rng, error_code_ptr ec) {
            { device.view_wr(rng, ec) } -> write_view;
        }
    decltype(auto) view_wr(Device &&device, offset64_range rng, error_code_ptr ec = nullptr)
    {
        JIO_Expects(is_valid_range(rng));
        decltype(auto) view = device.view_wr(rng, ec);
        JIO_Ensures(has_error(ec) || rng.end == unknown_offset ||
                    jetio::address(view).size() == range_size(rng));
        return view;
    }
    template <typename Device>
    concept write_map = requires(Device &&device, offset64_range rng, error_code_ptr ec) {
        { jetio::view_wr(device, rng, ec) } -> write_view;
    } && sizer<Device>;

    // ---------------------------------------------------------------------------------------
    // getopt, setopt, options
    // ---------------------------------------------------------------------------------------

    /// Device specific options, identified by `id`.
    template <typename Device>
        requires requires(Device &&device, int id, const std::any &optdata, error_code_ptr ec) {
            { device.getopt(id, optdata, ec) } -> std::convertible_to<std::any>;
        }
    std::any getopt(Device &&device, int id, const std::any &optdata, error_code_ptr ec = nullptr)
    {
        return device.getopt(id, optdata, ec);
    }
    template <typename Device>
        requires requires(Device &&device, int id, const std::any &optdata, error_code_ptr ec) {
            { device.setopt(id, optdata, optdata, ec) } -> std::convertible_to<std::any>;
        }
    std::any setopt(Device &&device, int id, const std::any &optdata, const std::any &indata,
                    error_code_ptr ec = nullptr)
    {
        return device.setopt(id, optdata, indata, ec);
    }
    template <typename Device>
    concept options =
        requires(Device &&device, int id, const std::any &optdata, error_code_ptr ec) {
            { jetio::getopt(device, id, optdata, ec) } -> std::convertible_to<std::any>;
            { jetio::setopt(device, id, optdata, optdata, ec) } -> std::convertible_to<std::any>;
        };
} // namespace jetio
