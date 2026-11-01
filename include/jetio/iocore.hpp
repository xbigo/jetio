#pragma once

// The device protocol.
//
// A device is any type that provides some of the operations below, each as a member function or
// as a free function found by argument dependent lookup (in the namespace of the device), so a
// custom device need not be written in namespace jetio. A free function takes precedence, so a
// type can be extended or overridden from outside; do not write an unconstrained function
// template with the name of an operation, it would also take over types that wrap yours. The
// operations of namespace jetio (read, write, ...) are customization point objects: they call
// the member or the free function, add the default `ec` argument and check the protocol-level
// contract. A concept (reader, writer, ...) tells whether a type provides the operation.
//
// Call them qualified or after a using declaration: being objects, they are not overloaded with
// functions of the same name, so `using namespace jetio` conflicts with, for example, ::read of
// <unistd.h> for unqualified calls.
//
//   [ sequence, forward_access, random_access ]   offset, seek_forward, seek
//   [ reader, is_eofer, sizer, read_map ]
//   [ writer, syncer, truncater, write_map ]
//   [ options ]
//
// Conventions
//   - `ec` is the error_code_ptr of error.hpp. Failures a caller cannot check beforehand are
//     reported through it (or io_error if it is null). Violating a documented precondition is
//     a contract violation, see std/contract.hpp.
//   - Offsets and sizes are in bytes and 64 bit (size64_t). Seeking beyond the end is
//     allowed; reading there yields no data, writing there extends the device if it can grow.
//   - Exception safety: a device is always left valid, destructible and usable. Individual
//     operations document anything stronger. An operation that reports an error has made
//     progress only where it says so.

#include <jetio/error.hpp>
#include <jetio/std/contract.hpp>
#include <jetio/types.hpp>

#include <any>
#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>
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
    // How an operation reaches the device: a free function by ADL first, then the member. Each
    // operation has a namespace of its own in detail. The deleted function with no parameters in
    // it stops the ordinary lookup there: it is never viable, but being a function it leaves
    // ADL on, which the object of the same name in namespace jetio would turn off.
    // ---------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------
    // read, reader
    // ---------------------------------------------------------------------------------------

    namespace detail::read_op
    {
        void read() = delete;
        template <typename D>
        concept by_member = requires(D &device, mutable_buffer buf, error_code_ptr ec) {
            { device.read(buf, ec) } -> std::convertible_to<mutable_buffer>;
        };
        template <typename D>
        concept by_adl = requires(D &device, mutable_buffer buf, error_code_ptr ec) {
            { read(device, buf, ec) } -> std::convertible_to<mutable_buffer>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            mutable_buffer operator()(Device &&device, mutable_buffer buf,
                                      error_code_ptr ec = nullptr) const
            {
                const mutable_buffer result = [&]() -> mutable_buffer
                {
                    if constexpr (by_adl<std::remove_reference_t<Device>>)
                        return read(device, buf, ec);
                    else
                        return device.read(buf, ec);
                }();
                JIO_Ensures(detail::is_prefix(result, buf));
                return result;
            }
        };
    } // namespace detail::read_op

    /// Reads up to buf.size() bytes at the current offset and advances the offset by the
    /// number of bytes read.
    /// @return the part of buf that was filled, always a prefix of buf. It may be shorter than
    ///         buf (a device need not fill it), but it is empty only if buf is empty, at the end
    ///         of the data, or after an error.
    /// @post result.data() == buf.data() && result.size() <= buf.size()
    inline constexpr detail::read_op::fn read{};

    template <typename Device>
    concept reader = requires(Device &&device, mutable_buffer buf, error_code_ptr ec) {
        { jetio::read(device, buf, ec) } -> std::convertible_to<mutable_buffer>;
    };

    // ---------------------------------------------------------------------------------------
    // write, writer
    // ---------------------------------------------------------------------------------------

    namespace detail::write_op
    {
        void write() = delete;
        template <typename D>
        concept by_member = requires(D &device, const_buffer buf, error_code_ptr ec) {
            { device.write(buf, ec) } -> std::convertible_to<const_buffer>;
        };
        template <typename D>
        concept by_adl = requires(D &device, const_buffer buf, error_code_ptr ec) {
            { write(device, buf, ec) } -> std::convertible_to<const_buffer>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            const_buffer operator()(Device &&device, const_buffer buf,
                                    error_code_ptr ec = nullptr) const
            {
                const const_buffer result = [&]() -> const_buffer
                {
                    if constexpr (by_adl<std::remove_reference_t<Device>>)
                        return write(device, buf, ec);
                    else
                        return device.write(buf, ec);
                }();
                JIO_Ensures(detail::is_suffix(result, buf));
                return result;
            }
        };
    } // namespace detail::write_op

    /// Writes buf at the current offset and advances the offset by the number of bytes
    /// written.
    /// @return the part of buf that was not written, always a suffix of buf. It is not empty
    ///         only after an error.
    /// @post result.data() + result.size() == buf.data() + buf.size() && result.size() <=
    /// buf.size()
    inline constexpr detail::write_op::fn write{};

    template <typename Device>
    concept writer = requires(Device &&device, const_buffer buf, error_code_ptr ec) {
        { jetio::write(device, buf, ec) } -> std::convertible_to<const_buffer>;
    };

    // ---------------------------------------------------------------------------------------
    // sync, syncer
    // ---------------------------------------------------------------------------------------

    namespace detail::sync_op
    {
        void sync() = delete;
        template <typename D>
        concept by_member = requires(D &device, error_code_ptr ec) { device.sync(ec); };
        template <typename D>
        concept by_adl = requires(D &device, error_code_ptr ec) { sync(device, ec); };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            void operator()(Device &&device, error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    sync(device, ec);
                else
                    device.sync(ec);
            }
        };
    } // namespace detail::sync_op

    /// Makes written data durable, as far as the device can.
    inline constexpr detail::sync_op::fn sync{};

    template <typename Device>
    concept syncer = requires(Device &&device, error_code_ptr ec) { jetio::sync(device, ec); };

    // ---------------------------------------------------------------------------------------
    // is_eof, is_eofer
    // ---------------------------------------------------------------------------------------

    namespace detail::is_eof_op
    {
        void is_eof() = delete;
        template <typename D>
        concept by_member = requires(D &device, error_code_ptr ec) {
            { device.is_eof(ec) } -> std::convertible_to<bool>;
        };
        template <typename D>
        concept by_adl = requires(D &device, error_code_ptr ec) {
            { is_eof(device, ec) } -> std::convertible_to<bool>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            bool operator()(Device &&device, error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return is_eof(device, ec);
                else
                    return device.is_eof(ec);
            }
        };
    } // namespace detail::is_eof_op

    /// @return true if no data is left to read at the current offset
    inline constexpr detail::is_eof_op::fn is_eof{};

    template <typename Device>
    concept is_eofer = requires(Device &&device, error_code_ptr ec) {
        { jetio::is_eof(device, ec) } -> std::convertible_to<bool>;
    };

    // ---------------------------------------------------------------------------------------
    // truncate, truncater
    // ---------------------------------------------------------------------------------------

    namespace detail::truncate_op
    {
        void truncate() = delete;
        template <typename D>
        concept by_member = requires(D &device, size64_t size, error_code_ptr ec) {
            { device.truncate(size, ec) } -> std::convertible_to<size64_t>;
        };
        template <typename D>
        concept by_adl = requires(D &device, size64_t size, error_code_ptr ec) {
            { truncate(device, size, ec) } -> std::convertible_to<size64_t>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            size64_t operator()(Device &&device, size64_t size, error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return truncate(device, size, ec);
                else
                    return device.truncate(size, ec);
            }
        };
    } // namespace detail::truncate_op

    /// Sets the size of the device. Data beyond the new size is discarded; new space reads as
    /// zero. The offset is not changed.
    /// @return the size of the device afterwards
    inline constexpr detail::truncate_op::fn truncate{};

    template <typename Device>
    concept truncater = requires(Device &&device, size64_t size, error_code_ptr ec) {
        { jetio::truncate(device, size, ec) } -> std::convertible_to<size64_t>;
    };

    // ---------------------------------------------------------------------------------------
    // size, sizer
    // ---------------------------------------------------------------------------------------

    namespace detail::size_op
    {
        void size() = delete;
        template <typename D>
        concept by_member = requires(D &device, error_code_ptr ec) {
            { device.size(ec) } -> std::convertible_to<size64_t>;
        };
        template <typename D>
        concept by_adl = requires(D &device, error_code_ptr ec) {
            { size(device, ec) } -> std::convertible_to<size64_t>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            size64_t operator()(Device &&device, error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return size(device, ec);
                else
                    return device.size(ec);
            }
        };
    } // namespace detail::size_op

    /// @return the size of the device, or unknown_size if it is not known
    inline constexpr detail::size_op::fn size{};

    template <typename Device>
    concept sizer = requires(Device &&device, error_code_ptr ec) {
        { jetio::size(device, ec) } -> std::convertible_to<size64_t>;
    };

    // ---------------------------------------------------------------------------------------
    // offset, sequence
    // ---------------------------------------------------------------------------------------

    namespace detail::offset_op
    {
        void offset() = delete;
        template <typename D>
        concept by_member = requires(D &device, error_code_ptr ec) {
            { device.offset(ec) } -> std::convertible_to<size64_t>;
        };
        template <typename D>
        concept by_adl = requires(D &device, error_code_ptr ec) {
            { offset(device, ec) } -> std::convertible_to<size64_t>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            size64_t operator()(Device &&device, error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return offset(device, ec);
                else
                    return device.offset(ec);
            }
        };
    } // namespace detail::offset_op

    /// @return the current offset
    inline constexpr detail::offset_op::fn offset{};

    template <typename Device>
    concept sequence = requires(Device &&device, error_code_ptr ec) {
        { jetio::offset(device, ec) } -> std::convertible_to<size64_t>;
    };

    // ---------------------------------------------------------------------------------------
    // seek, random_access
    // ---------------------------------------------------------------------------------------

    namespace detail::seek_op
    {
        void seek() = delete;
        template <typename D>
        concept by_member = requires(D &device, size64_t off, error_code_ptr ec) {
            { device.seek(off, ec) } -> std::convertible_to<size64_t>;
        };
        template <typename D>
        concept by_adl = requires(D &device, size64_t off, error_code_ptr ec) {
            { seek(device, off, ec) } -> std::convertible_to<size64_t>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            size64_t operator()(Device &&device, size64_t off, error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return seek(device, off, ec);
                else
                    return device.seek(off, ec);
            }
        };
    } // namespace detail::seek_op

    /// Moves the current offset to `off`.
    /// @return the offset afterwards: `off`, unless the device documents otherwise (for
    ///         example empty_device, which has no offset but 0). On an error it is the unchanged
    ///         offset.
    inline constexpr detail::seek_op::fn seek{};

    template <typename Device>
    concept random_access = requires(Device &&device, size64_t off, error_code_ptr ec) {
        { jetio::seek(device, off, ec) } -> std::convertible_to<size64_t>;
    } && sequence<Device>;

    // ---------------------------------------------------------------------------------------
    // seek_forward, forward_access
    // ---------------------------------------------------------------------------------------

    namespace detail
    {
        /// Used in contracts only: true if `off` is not before the current offset (or the
        /// current offset cannot be obtained).
        template <typename Device> bool is_not_before_offset(Device &device, size64_t off)
        {
            if constexpr (requires(Device &d, error_code_ptr ec) { jetio::offset(d, ec); })
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

    namespace detail::seek_forward_op
    {
        void seek_forward() = delete;
        template <typename D>
        concept by_member = requires(D &device, size64_t off, error_code_ptr ec) {
            { device.seek_forward(off, ec) } -> std::convertible_to<size64_t>;
        };
        template <typename D>
        concept by_adl = requires(D &device, size64_t off, error_code_ptr ec) {
            { seek_forward(device, off, ec) } -> std::convertible_to<size64_t>;
        };
        template <typename D>
        concept by_seek = requires(D &device, size64_t off, error_code_ptr ec) {
            { jetio::seek(device, off, ec) } -> std::convertible_to<size64_t>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>> ||
                         by_seek<std::remove_reference_t<Device>>
            size64_t operator()(Device &&device, size64_t off, error_code_ptr ec = nullptr) const
            {
                JIO_Expects_audit(detail::is_not_before_offset(device, off));
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return seek_forward(device, off, ec);
                else if constexpr (by_member<std::remove_reference_t<Device>>)
                    return device.seek_forward(off, ec);
                else
                    return jetio::seek(device, off, ec);
            }
        };
    } // namespace detail::seek_forward_op

    /// Moves the current offset forward to `off`. Devices that cannot go back (streams) provide
    /// only this; every random access device gets it through seek.
    /// @pre off >= offset(device) (audit)
    /// @return the offset afterwards, see seek
    inline constexpr detail::seek_forward_op::fn seek_forward{};

    template <typename Device>
    concept forward_access = requires(Device &&device, size64_t off, error_code_ptr ec) {
        { jetio::seek_forward(device, off, ec) } -> std::convertible_to<size64_t>;
    } && sequence<Device>;

    // ---------------------------------------------------------------------------------------
    // address, read_view, write_view
    // ---------------------------------------------------------------------------------------

    namespace detail::address_op
    {
        void address() = delete;
        template <typename V>
        concept by_adl = requires(V &view) { address(view); };
        template <typename V>
        concept by_member = !by_adl<V> && requires(V &view) { view.address(); };
        template <typename V>
        concept by_pointer = !by_adl<V> && !by_member<V> && requires(V &view) { view->address(); };
        struct fn
        {
            template <typename View>
                requires by_member<std::remove_reference_t<View>> ||
                         by_pointer<std::remove_reference_t<View>> ||
                         by_adl<std::remove_reference_t<View>>
            decltype(auto) operator()(View &&view) const
            {
                if constexpr (by_adl<std::remove_reference_t<View>>)
                    return address(view);
                else if constexpr (by_member<std::remove_reference_t<View>>)
                    return view.address();
                else
                    return view->address();
            }
        };
    } // namespace detail::address_op

    /// A view gives direct access to bytes of a device. It is valid until the device is
    /// modified or destroyed, unless the device documents otherwise.
    /// @return the bytes of the view. Works for views that have address(), for pointer-like
    ///         handles to such views, and for views with a free function address.
    inline constexpr detail::address_op::fn address{};

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

    namespace detail::view_rd_op
    {
        void view_rd() = delete;
        template <typename D>
        concept by_member = requires(D &device, offset64_range rng, error_code_ptr ec) {
            { device.view_rd(rng, ec) } -> read_view;
        };
        template <typename D>
        concept by_adl = requires(D &device, offset64_range rng, error_code_ptr ec) {
            { view_rd(device, rng, ec) } -> read_view;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            decltype(auto) operator()(Device &&device, offset64_range rng,
                                      error_code_ptr ec = nullptr) const
            {
                JIO_Expects(is_valid_range(rng));
                decltype(auto) view = [&]() -> decltype(auto)
                {
                    if constexpr (by_adl<std::remove_reference_t<Device>>)
                        return view_rd(device, rng, ec);
                    else
                        return device.view_rd(rng, ec);
                }();
                JIO_Ensures(has_error(ec) || rng.end == unknown_offset ||
                            jetio::address(view).size() == range_size(rng));
                return view;
            }
        };
    } // namespace detail::view_rd_op

    /// Gives read access to the bytes in `rng` without copying them, if the device can.
    /// @pre is_valid_range(rng)
    /// @return the view; when the end of `rng` is unknown_offset it extends to the end of the
    ///         device. Reports errc::invalid_argument if the range lies beyond the device.
    /// @post has_error(ec) || rng.end == unknown_offset || address(view).size() == range_size(rng)
    inline constexpr detail::view_rd_op::fn view_rd{};

    template <typename Device>
    concept read_map = requires(Device &&device, offset64_range rng, error_code_ptr ec) {
        { jetio::view_rd(device, rng, ec) } -> read_view;
    } && sizer<Device>;

    // ---------------------------------------------------------------------------------------
    // view_wr, write_map
    // ---------------------------------------------------------------------------------------

    namespace detail::view_wr_op
    {
        void view_wr() = delete;
        template <typename D>
        concept by_member = requires(D &device, offset64_range rng, error_code_ptr ec) {
            { device.view_wr(rng, ec) } -> write_view;
        };
        template <typename D>
        concept by_adl = requires(D &device, offset64_range rng, error_code_ptr ec) {
            { view_wr(device, rng, ec) } -> write_view;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            decltype(auto) operator()(Device &&device, offset64_range rng,
                                      error_code_ptr ec = nullptr) const
            {
                JIO_Expects(is_valid_range(rng));
                decltype(auto) view = [&]() -> decltype(auto)
                {
                    if constexpr (by_adl<std::remove_reference_t<Device>>)
                        return view_wr(device, rng, ec);
                    else
                        return device.view_wr(rng, ec);
                }();
                JIO_Ensures(has_error(ec) || rng.end == unknown_offset ||
                            jetio::address(view).size() == range_size(rng));
                return view;
            }
        };
    } // namespace detail::view_wr_op

    /// Gives write access to the bytes in `rng` without copying them, if the device can. The
    /// device grows to fit the end of `rng` if it can.
    /// @pre is_valid_range(rng)
    /// @return the view, see view_rd
    /// @post has_error(ec) || rng.end == unknown_offset || address(view).size() == range_size(rng)
    inline constexpr detail::view_wr_op::fn view_wr{};

    template <typename Device>
    concept write_map = requires(Device &&device, offset64_range rng, error_code_ptr ec) {
        { jetio::view_wr(device, rng, ec) } -> write_view;
    } && sizer<Device>;

    // ---------------------------------------------------------------------------------------
    // getopt, setopt, options
    // ---------------------------------------------------------------------------------------

    namespace detail::getopt_op
    {
        void getopt() = delete;
        template <typename D>
        concept by_member =
            requires(D &device, int id, const std::any &optdata, error_code_ptr ec) {
                { device.getopt(id, optdata, ec) } -> std::convertible_to<std::any>;
            };
        template <typename D>
        concept by_adl = requires(D &device, int id, const std::any &optdata, error_code_ptr ec) {
            { getopt(device, id, optdata, ec) } -> std::convertible_to<std::any>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            std::any operator()(Device &&device, int id, const std::any &optdata,
                                error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return getopt(device, id, optdata, ec);
                else
                    return device.getopt(id, optdata, ec);
            }
        };
    } // namespace detail::getopt_op

    /// Device specific options, identified by `id`.
    inline constexpr detail::getopt_op::fn getopt{};

    namespace detail::setopt_op
    {
        void setopt() = delete;
        template <typename D>
        concept by_member =
            requires(D &device, int id, const std::any &optdata, error_code_ptr ec) {
                { device.setopt(id, optdata, optdata, ec) } -> std::convertible_to<std::any>;
            };
        template <typename D>
        concept by_adl = requires(D &device, int id, const std::any &optdata, error_code_ptr ec) {
            { setopt(device, id, optdata, optdata, ec) } -> std::convertible_to<std::any>;
        };
        struct fn
        {
            template <typename Device>
                requires by_member<std::remove_reference_t<Device>> ||
                         by_adl<std::remove_reference_t<Device>>
            std::any operator()(Device &&device, int id, const std::any &optdata,
                                const std::any &indata, error_code_ptr ec = nullptr) const
            {
                if constexpr (by_adl<std::remove_reference_t<Device>>)
                    return setopt(device, id, optdata, indata, ec);
                else
                    return device.setopt(id, optdata, indata, ec);
            }
        };
    } // namespace detail::setopt_op

    /// Device specific options, identified by `id`.
    inline constexpr detail::setopt_op::fn setopt{};

    template <typename Device>
    concept options =
        requires(Device &&device, int id, const std::any &optdata, error_code_ptr ec) {
            { jetio::getopt(device, id, optdata, ec) } -> std::convertible_to<std::any>;
            { jetio::setopt(device, id, optdata, optdata, ec) } -> std::convertible_to<std::any>;
        };
} // namespace jetio
