#pragma once

// Adaptors: devices that wrap another device, which they refer to and do not own, and change
// what it looks like. Each provides the capabilities of the wrapped device that survive the
// change.
//
//   multiplex_device  an own offset over a shared random access device
//   shift_device      hides the first `shift` bytes of a forward access device
//   sub_device        a window [begin, end) of a random access device, with an own offset

#include <jetio/iocore.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>

namespace jetio
{
    namespace detail
    {
        template <typename Device>
        using read_view_of = std::remove_cvref_t<decltype(jetio::view_rd(
            std::declval<Device &>(), std::declval<offset64_range>()))>;

        template <typename Device>
        using write_view_of = std::remove_cvref_t<decltype(jetio::view_wr(
            std::declval<Device &>(), std::declval<offset64_range>()))>;

        // The adaptors return an empty view when they report an error themselves.
        template <typename Device>
        concept default_read_map =
            read_map<Device &> && std::default_initializable<read_view_of<Device>>;
        template <typename Device>
        concept default_write_map =
            write_map<Device &> && std::default_initializable<write_view_of<Device>>;

        /// @return false if a + b does not fit in size64_t
        constexpr bool add_size(size64_t a, size64_t b, size64_t &sum) noexcept
        {
            if (b > std::numeric_limits<size64_t>::max() - a)
                return false;
            sum = a + b;
            return true;
        }

        /// @pre a >= 0 && b >= 0
        /// @return false if a + b is not an offset below unknown_offset
        constexpr bool add_offset(offset64_t a, offset64_t b, offset64_t &sum) noexcept
        {
            JIO_Expects(a >= 0 && b >= 0);
            if (b >= unknown_offset - a)
                return false;
            sum = a + b;
            return true;
        }
    } // namespace detail

    /// Gives each user of one random access device an own offset: before every operation the
    /// device is moved to the offset of the adaptor, afterwards the adaptor advances its own.
    /// Not safe for concurrent use. An operation that throws leaves the offset unchanged.
    /// Provides what the wrapped device provides: [ sequence, random_access ] and
    /// [ reader, writer, syncer, is_eofer, sizer, truncater, read_map, write_map ].
    template <typename Device>
        requires random_access<Device &>
    class multiplex_device
    {
    public:
        /// @pre pos >= 0
        explicit multiplex_device(Device &device, offset64_t pos = 0) noexcept
            : m_device(device), m_pos(pos)
        {
            JIO_Expects(pos >= 0);
        }

        Device &underlying() noexcept { return m_device; }
        const Device &underlying() const noexcept { return m_device; }

        offset64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return m_pos;
        }

        /// @return the offset afterwards, as the wrapped device reports it; the unchanged
        ///         offset on an error
        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr)
        {
            const offset64_t result = jetio::seek(m_device, off, ec);
            if (has_error(ec))
                return m_pos;
            return m_pos = result;
        }

        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
            requires reader<Device &>
        {
            if (!enter_(ec))
                return buf.first(0);
            const mutable_buffer result = jetio::read(m_device, buf, ec);
            m_pos += static_cast<offset64_t>(result.size());
            return result;
        }

        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
            requires writer<Device &>
        {
            if (!enter_(ec))
                return buf;
            const const_buffer rest = jetio::write(m_device, buf, ec);
            m_pos += static_cast<offset64_t>(buf.size() - rest.size());
            return rest;
        }

        /// Moves the wrapped device to the offset of the adaptor, as it must for any answer.
        bool is_eof(error_code_ptr ec = nullptr)
            requires is_eofer<Device &>
        {
            if (!enter_(ec))
                return false;
            return jetio::is_eof(m_device, ec);
        }

        size64_t size(error_code_ptr ec = nullptr) const
            requires sizer<Device &>
        {
            return jetio::size(m_device, ec);
        }

        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr)
            requires truncater<Device &>
        {
            return jetio::truncate(m_device, new_size, ec);
        }

        void sync(error_code_ptr ec = nullptr)
            requires syncer<Device &>
        {
            jetio::sync(m_device, ec);
        }

        decltype(auto) view_rd(offset64_range h, error_code_ptr ec = nullptr)
            requires read_map<Device &>
        {
            return jetio::view_rd(m_device, h, ec);
        }

        decltype(auto) view_wr(offset64_range h, error_code_ptr ec = nullptr)
            requires write_map<Device &>
        {
            return jetio::view_wr(m_device, h, ec);
        }

    private:
        bool enter_(error_code_ptr ec)
        {
            jetio::seek(m_device, m_pos, ec);
            return !has_error(ec);
        }

        Device &m_device;
        offset64_t m_pos = 0;
    };

    /// Hides the first `shift` bytes of a device, such as a header that something else has
    /// read: the offset 0 of the adaptor is the offset `shift` of the device. The offset of the
    /// adaptor is the one of the device less `shift`, or 0 while the device has not reached
    /// `shift`; before the first read or write the device is moved forward to `shift`.
    /// A device that is shorter than `shift` has size 0 for the adaptor; truncating to size 0
    /// makes it `shift` bytes long. Offsets and sizes that do not fit with `shift` added are
    /// reported as errc::value_too_large.
    /// Provides [ sequence, forward_access ] and, as the wrapped device provides it,
    /// [ random_access, reader, writer, syncer, is_eofer, sizer, truncater, read_map, write_map ].
    template <typename Device>
        requires forward_access<Device &>
    class shift_device
    {
    public:
        /// @pre shift >= 0
        explicit shift_device(Device &device, offset64_t shift) noexcept
            : m_device(device), m_shift(shift)
        {
            JIO_Expects(shift >= 0);
        }

        Device &underlying() noexcept { return m_device; }
        const Device &underlying() const noexcept { return m_device; }
        offset64_t shift() const noexcept { return m_shift; }

        offset64_t offset(error_code_ptr ec = nullptr) const
        {
            return to_logical_(jetio::offset(m_device, ec));
        }

        /// @return the offset afterwards, see seek
        offset64_t seek_forward(offset64_t off, error_code_ptr ec = nullptr)
        {
            offset64_t target = 0;
            if (!detail::add_offset(off, m_shift, target))
                return too_large_(ec);
            return to_logical_(jetio::seek_forward(m_device, target, ec));
        }

        /// @return the offset afterwards: `off`, or the unchanged offset on an error
        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr)
            requires random_access<Device &>
        {
            offset64_t target = 0;
            if (!detail::add_offset(off, m_shift, target))
                return too_large_(ec);
            return to_logical_(jetio::seek(m_device, target, ec));
        }

        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
            requires reader<Device &>
        {
            if (!enter_(ec))
                return buf.first(0);
            return jetio::read(m_device, buf, ec);
        }

        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
            requires writer<Device &>
        {
            if (!enter_(ec))
                return buf;
            return jetio::write(m_device, buf, ec);
        }

        bool is_eof(error_code_ptr ec = nullptr)
            requires is_eofer<Device &>
        {
            if (!enter_(ec))
                return false;
            return jetio::is_eof(m_device, ec);
        }

        /// @return the size of the device less `shift` (0 if it is shorter), or unknown_size
        size64_t size(error_code_ptr ec = nullptr) const
            requires sizer<Device &>
        {
            const size64_t device_size = jetio::size(m_device, ec);
            return device_size == unknown_size ? device_size : logical_size_(device_size);
        }

        /// @return the size afterwards, `new_size` unless the device reports otherwise
        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr)
            requires truncater<Device &>
        {
            size64_t target = 0;
            if (!detail::add_size(new_size, static_cast<size64_t>(m_shift), target))
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                error_code ignored;
                return logical_size_(jetio::size(m_device, &ignored));
            }
            return logical_size_(jetio::truncate(m_device, target, ec));
        }

        void sync(error_code_ptr ec = nullptr)
            requires syncer<Device &>
        {
            jetio::sync(m_device, ec);
        }

        /// Reports errc::value_too_large, and returns an empty view, if `h` with `shift` added
        /// does not fit.
        auto view_rd(offset64_range h, error_code_ptr ec = nullptr)
            requires detail::default_read_map<Device>
        {
            using view = detail::read_view_of<Device>;
            offset64_range shifted;
            if (!shift_(h, shifted))
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return view{};
            }
            return jetio::view_rd(m_device, shifted, ec);
        }

        /// See view_rd.
        auto view_wr(offset64_range h, error_code_ptr ec = nullptr)
            requires detail::default_write_map<Device>
        {
            using view = detail::write_view_of<Device>;
            offset64_range shifted;
            if (!shift_(h, shifted))
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return view{};
            }
            return jetio::view_wr(m_device, shifted, ec);
        }

    private:
        offset64_t to_logical_(offset64_t device_offset) const noexcept
        {
            return device_offset < m_shift ? 0 : device_offset - m_shift;
        }

        size64_t logical_size_(size64_t device_size) const noexcept
        {
            const auto shift = static_cast<size64_t>(m_shift);
            return device_size < shift ? 0 : device_size - shift;
        }

        /// Reports errc::value_too_large.
        /// @return the unchanged offset
        offset64_t too_large_(error_code_ptr ec)
        {
            set_error_or_throw(ec, std::errc::value_too_large);
            error_code ignored;
            return to_logical_(jetio::offset(m_device, &ignored));
        }

        bool shift_(offset64_range h, offset64_range &shifted) const noexcept
        {
            if (!detail::add_offset(h.begin, m_shift, shifted.begin))
                return false;
            shifted.end = unknown_offset;
            return h.end == unknown_offset || detail::add_offset(h.end, m_shift, shifted.end);
        }

        /// Moves the device forward to `shift` if it has not reached it.
        bool enter_(error_code_ptr ec)
        {
            const offset64_t current = jetio::offset(m_device, ec);
            if (has_error(ec))
                return false;
            if (current < m_shift)
            {
                jetio::seek_forward(m_device, m_shift, ec);
                return !has_error(ec);
            }
            return true;
        }

        Device &m_device;
        offset64_t m_shift = 0;
    };

    /// A window [begin, end) of a device, which the adaptor sees as a device of its own with
    /// offsets from 0. It has an own offset, which may lie beyond the end of the window
    /// (nothing is read there); writing past the end of the window stores what fits, then
    /// reports errc::no_space_on_device. Several windows, which may overlap, can share one
    /// device. The window cannot be resized: truncate accepts only its size. The device may be
    /// shorter than the window, then reads return less. Not safe for concurrent use.
    /// Provides [ sequence, random_access, is_eofer, sizer, truncater ] and, as the wrapped
    /// device provides it, [ reader, writer, syncer, read_map, write_map ].
    template <typename Device>
        requires random_access<Device &>
    class sub_device
    {
    public:
        /// @pre is_valid_range(section) && section.end != unknown_offset
        explicit sub_device(Device &device, offset64_range section) noexcept
            : m_device(device), m_section(section)
        {
            JIO_Expects(is_valid_range(section) && section.end != unknown_offset);
        }

        Device &underlying() noexcept { return m_device; }
        const Device &underlying() const noexcept { return m_device; }
        offset64_range section() const noexcept { return m_section; }

        offset64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return m_pos;
        }

        /// Moves the offset of the window; any offset is accepted.
        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            return m_pos = off;
        }

        bool is_eof(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return std::cmp_greater_equal(m_pos, size_());
        }

        size64_t size(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return size_();
        }

        /// @return the part of buf that was filled, which ends at the end of the window
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
            requires reader<Device &>
        {
            const std::size_t count = fitting_(buf.size());
            if (count == 0)
            {
                clear_error(ec);
                return buf.first(0);
            }
            if (!enter_(ec))
                return buf.first(0);
            const mutable_buffer result = jetio::read(m_device, buf.first(count), ec);
            m_pos += static_cast<offset64_t>(result.size());
            return result;
        }

        /// @return the part of buf that was not written. Reports errc::no_space_on_device if it
        ///         does not fit in the window, after writing the part that fits.
        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
            requires writer<Device &>
        {
            const std::size_t count = fitting_(buf.size());
            std::size_t written = 0;
            if (count != 0)
            {
                if (!enter_(ec))
                    return buf;
                const const_buffer rest = jetio::write(m_device, buf.first(count), ec);
                written = count - rest.size();
                m_pos += static_cast<offset64_t>(written);
                if (has_error(ec))
                    return buf.last(buf.size() - written);
            }
            else
            {
                clear_error(ec);
            }
            if (written < buf.size())
                set_error_or_throw(ec, std::errc::no_space_on_device);
            return buf.last(buf.size() - written);
        }

        /// @return `new_size`, which must be the size of the window; reports
        ///         errc::invalid_argument otherwise, and returns the size
        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr)
        {
            if (new_size != size_())
                set_error_or_throw(ec, std::errc::invalid_argument);
            else
                clear_error(ec);
            return size_();
        }

        void sync(error_code_ptr ec = nullptr)
            requires syncer<Device &>
        {
            jetio::sync(m_device, ec);
        }

        /// Reports errc::invalid_argument, and returns an empty view, if `h` lies beyond the
        /// window. An unknown end of `h` is the end of the window.
        auto view_rd(offset64_range h, error_code_ptr ec = nullptr)
            requires detail::default_read_map<Device>
        {
            using view = detail::read_view_of<Device>;
            offset64_range mapped;
            if (!map_(h, mapped))
            {
                set_error_or_throw(ec, std::errc::invalid_argument);
                return view{};
            }
            return jetio::view_rd(m_device, mapped, ec);
        }

        /// See view_rd.
        auto view_wr(offset64_range h, error_code_ptr ec = nullptr)
            requires detail::default_write_map<Device>
        {
            using view = detail::write_view_of<Device>;
            offset64_range mapped;
            if (!map_(h, mapped))
            {
                set_error_or_throw(ec, std::errc::invalid_argument);
                return view{};
            }
            return jetio::view_wr(m_device, mapped, ec);
        }

    private:
        size64_t size_() const noexcept { return range_size(m_section); }

        /// @return the number of the first `wanted` bytes at the offset that are in the window
        std::size_t fitting_(std::size_t wanted) const noexcept
        {
            const size64_t left =
                std::cmp_less(m_pos, size_()) ? size_() - static_cast<size64_t>(m_pos) : 0;
            return static_cast<std::size_t>(std::min<size64_t>(wanted, left));
        }

        /// @pre m_pos < size_()
        bool enter_(error_code_ptr ec)
        {
            jetio::seek(m_device, m_section.begin + m_pos, ec);
            return !has_error(ec);
        }

        /// @pre is_valid_range(h)
        /// @return false if h is not within the window
        bool map_(offset64_range h, offset64_range &mapped) const noexcept
        {
            const size64_t window_size = size_();
            if (std::cmp_greater(h.begin, window_size) ||
                (h.end != unknown_offset && std::cmp_greater(h.end, window_size)))
                return false;
            mapped.begin = m_section.begin + h.begin;
            mapped.end = h.end == unknown_offset ? m_section.end : m_section.begin + h.end;
            return true;
        }

        Device &m_device;
        offset64_range m_section{};
        offset64_t m_pos = 0;
    };
} // namespace jetio
