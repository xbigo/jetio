#pragma once

// Devices over memory.
//
// A memory_device works on a "represent": the bytes plus the current position. A represent is
// a type that has either the members `data` and `pos`, or the accessors `data()` and `pos()`
// with `pos(size_t)` as setter. `data` is a contiguous range of std::byte.
//   vector_buffer_represent   owns a growable std::vector<std::byte>
//   mutable_buffer_represent  refers to fixed-size writable memory
//   const_buffer_represent    refers to fixed-size read-only memory

#include <jetio/iocore.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace jetio
{
    /// Read-only view of a part of a memory device. Valid until the device is modified,
    /// moved or destroyed.
    class buffer_rd_view
    {
    public:
        constexpr const_buffer address() const noexcept { return m_data; }
        constexpr buffer_rd_view() noexcept = default;
        constexpr explicit buffer_rd_view(const_buffer data) noexcept : m_data(data) {}

    private:
        const_buffer m_data;
    };

    /// Writable view of a part of a memory device. Valid until the device is resized, moved
    /// or destroyed.
    class buffer_wr_view
    {
    public:
        constexpr mutable_buffer address() const noexcept { return m_data; }
        constexpr buffer_wr_view() noexcept = default;
        constexpr explicit buffer_wr_view(mutable_buffer data) noexcept : m_data(data) {}

    private:
        mutable_buffer m_data;
    };

    namespace detail
    {
        template <typename Rep>
        concept member_represent = requires(Rep &rep, std::size_t p) {
            rep.data;
            { rep.pos } -> std::convertible_to<std::size_t>;
            rep.pos = p;
        };

        template <typename Rep>
        concept accessor_represent = requires(Rep &rep, std::size_t p) {
            rep.data();
            { rep.pos() } -> std::convertible_to<std::size_t>;
            rep.pos(p);
        };

        template <typename Rep> constexpr decltype(auto) represent_data(Rep &rep) noexcept
        {
            if constexpr (accessor_represent<std::remove_const_t<Rep>>)
                return (rep.data());
            else
                return (rep.data);
        }

        template <typename Rep> constexpr std::size_t represent_pos(const Rep &rep) noexcept
        {
            if constexpr (accessor_represent<std::remove_const_t<Rep>>)
                return static_cast<std::size_t>(rep.pos());
            else
                return static_cast<std::size_t>(rep.pos);
        }

        template <typename Rep> constexpr void set_represent_pos(Rep &rep, std::size_t p) noexcept
        {
            if constexpr (accessor_represent<std::remove_const_t<Rep>>)
                rep.pos(p);
            else
                rep.pos = p;
        }

        template <typename Range>
        concept byte_range =
            std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range> &&
            std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>;
    } // namespace detail

    /// The represent can be read: it has data and pos, data is a contiguous range of bytes.
    template <typename Rep>
    concept read_represent = (detail::member_represent<std::remove_cvref_t<Rep>> ||
                              detail::accessor_represent<std::remove_cvref_t<Rep>>) &&
                             detail::byte_range<decltype(detail::represent_data(
                                 std::declval<const std::remove_cvref_t<Rep> &>()))>;

    /// The represent can be written: the bytes are not const.
    template <typename Rep>
    concept write_represent =
        read_represent<Rep> &&
        std::ranges::output_range<decltype(detail::represent_data(
                                      std::declval<std::remove_cvref_t<Rep> &>())),
                                  std::byte>;

    /// The represent can change its size.
    template <typename Rep>
    concept truncatable_represent =
        write_represent<Rep> && requires(std::remove_cvref_t<Rep> &rep, std::size_t size) {
            detail::represent_data(rep).resize(size);
        };

    struct const_buffer_represent
    {
        const_buffer &data;
        std::size_t &pos;
    };

    struct mutable_buffer_represent
    {
        mutable_buffer &data;
        std::size_t &pos;
    };

    struct vector_buffer_represent
    {
        std::vector<std::byte> data;
        std::size_t pos{0};
    };

    /// A device over memory: [ sequence, random_access, forward_access ] [ reader, is_eofer, sizer,
    /// read_map ] and, depending on the represent, [ writer, syncer, write_map ] and [ truncater ].
    ///
    /// Invariant: none beyond the validity of the represent. The offset may be beyond the end.
    ///
    /// Errors (besides contract violations) are all reported as described in error.hpp:
    ///   errc::value_too_large    a size or offset does not fit in std::size_t
    ///   errc::invalid_argument   a view lies beyond the end of the data
    ///   errc::no_space_on_device the data cannot grow and the write does not fit
    ///   errc::not_enough_memory  growing the data failed
    ///
    /// Exception safety: every operation gives the strong guarantee, except write, which
    /// stores what fits in a fixed-size represent before it reports errc::no_space_on_device.
    template <read_represent Represent = vector_buffer_represent> class memory_device
    {
    public:
        explicit memory_device(Represent rep = {}) noexcept(
            std::is_nothrow_move_constructible_v<Represent>)
            : m_rep(std::move(rep))
        {
        }

        /// @return the current offset
        long_size_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return pos();
        }

        /// @return true if the offset is at or beyond the end of the data
        bool is_eof(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return pos() >= data_size();
        }

        /// @return the size of the data
        long_size_t size(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return data_size();
        }

        /// Moves the offset; it may be moved beyond the end.
        /// @return `off`. Reports errc::value_too_large, and returns the unchanged offset, if
        ///         `off` does not fit in std::size_t.
        /// @post on success offset() == off
        long_size_t seek(long_size_t off, error_code_ptr ec = nullptr)
        {
            if (!in_size_t_range(off))
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return pos();
            }
            clear_error(ec);
            detail::set_represent_pos(m_rep, to_size_t(off));
            JIO_Ensures(pos() == off);
            return off;
        }

        /// Moves the offset forward, see seek.
        /// @pre off >= offset()
        long_size_t seek_forward(long_size_t off, error_code_ptr ec = nullptr)
        {
            JIO_Expects(off >= pos());
            return seek(off, ec);
        }

        /// Copies the data at the offset into buf; never fails.
        /// @return the part of buf that was filled
        /// @post the result has min(buf.size(), data left) bytes, so it is shorter than buf only
        ///       at the end of the data
        /// @post the offset advanced by the size of the result
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
        {
            clear_error(ec);
            const std::size_t old_pos = pos();
            const std::size_t old_size = data_size();
            const std::size_t count =
                std::min(old_pos < old_size ? old_size - old_pos : std::size_t{0}, buf.size());
            if (count != 0)
                std::copy_n(read_data() + old_pos, count, buf.begin());
            detail::set_represent_pos(m_rep, old_pos + count);

            const mutable_buffer result = buf.first(count);
            JIO_Ensures(pos() == old_pos + result.size());
            JIO_Ensures(result.size() == buf.size() || pos() >= data_size());
            return result;
        }

        /// Gives the bytes in `h` without copying.
        /// @pre is_valid_range(h)
        /// @return the view. The end of `h` is the end of the data if it is unknown_offset.
        ///         Reports errc::invalid_argument, and returns an empty view, if `h` lies
        ///         beyond the end of the data.
        /// @post on success the view has the size of the range
        buffer_rd_view view_rd(long_offset_range h, error_code_ptr ec = nullptr) const
        {
            JIO_Expects_critical(is_valid_range(h));
            const std::size_t size = data_size();
            const long_offset_t end =
                h.end == unknown_offset ? static_cast<long_offset_t>(size) : h.end;
            if (std::cmp_greater(end, size) || h.begin > end)
            {
                set_error_or_throw(ec, std::errc::invalid_argument);
                return buffer_rd_view();
            }
            clear_error(ec);
            const buffer_rd_view result(const_buffer(read_data() + offset_to_size_t(h.begin),
                                                     offset_to_size_t(end - h.begin)));
            JIO_Ensures(result.address().size() == static_cast<long_size_t>(end - h.begin));
            return result;
        }

        /// Writes buf at the offset. The data grows if the represent can, the gap between the
        /// old end and the offset reads as zero. Writing nothing changes nothing.
        /// @return the part of buf that was not written: empty on success. Reports
        ///         errc::no_space_on_device if the data cannot grow, after writing the part
        ///         that fits. Reports errc::value_too_large if the end of the write does not
        ///         fit in std::size_t, and errc::not_enough_memory if growing failed, both
        ///         without having written anything.
        /// @post the offset advanced by the number of bytes written
        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
            requires write_represent<Represent>
        {
            if (buf.empty())
            {
                clear_error(ec);
                return buf;
            }

            const std::size_t old_pos = pos();
            const std::size_t old_size = data_size();
            if (buf.size() > std::numeric_limits<std::size_t>::max() - old_pos)
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return buf;
            }

            const std::size_t new_pos = old_pos + buf.size();
            std::size_t writable = buf.size();
            if (new_pos > old_size)
            {
                if constexpr (truncatable_represent<Represent>)
                {
                    if (!resize_data(new_pos, ec))
                        return buf;
                }
                else
                {
                    writable = old_pos < old_size ? old_size - old_pos : std::size_t{0};
                }
            }

            if (writable != 0)
                std::copy_n(buf.begin(), writable, write_data() + old_pos);
            detail::set_represent_pos(m_rep, old_pos + writable);

            const const_buffer result = buf.last(buf.size() - writable);
            if (result.empty())
                clear_error(ec);
            else
                set_error_or_throw(ec, std::errc::no_space_on_device);
            JIO_Ensures(pos() == old_pos + (buf.size() - result.size()));
            return result;
        }

        /// Gives writable access to the bytes in `h`. The data grows to fit the end of `h` if
        /// the represent can.
        /// @pre is_valid_range(h)
        /// @return the view. The end of `h` is the end of the data if it is unknown_offset.
        ///         Reports errc::invalid_argument if the begin of `h` is beyond the end of the
        ///         data, errc::no_space_on_device if the data cannot grow, errc::value_too_large
        ///         and errc::not_enough_memory like write. An empty view is returned on error.
        /// @post on success the view has the size of the range
        buffer_wr_view view_wr(long_offset_range h, error_code_ptr ec = nullptr)
            requires write_represent<Represent>
        {
            JIO_Expects_critical(is_valid_range(h));
            const std::size_t size = data_size();
            const long_offset_t end =
                h.end == unknown_offset ? static_cast<long_offset_t>(size) : h.end;
            if (h.begin > end)
            {
                set_error_or_throw(ec, std::errc::invalid_argument);
                return buffer_wr_view();
            }
            if (!in_size_t_range(end))
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return buffer_wr_view();
            }
            if (std::cmp_greater(end, size))
            {
                if constexpr (truncatable_represent<Represent>)
                {
                    if (!resize_data(offset_to_size_t(end), ec))
                        return buffer_wr_view();
                }
                else
                {
                    set_error_or_throw(ec, std::errc::no_space_on_device);
                    return buffer_wr_view();
                }
            }
            clear_error(ec);
            const buffer_wr_view result(mutable_buffer(write_data() + offset_to_size_t(h.begin),
                                                       offset_to_size_t(end - h.begin)));
            JIO_Ensures(result.address().size() == static_cast<long_size_t>(end - h.begin));
            return result;
        }

        /// Nothing to flush.
        void sync(error_code_ptr ec = nullptr)
            requires write_represent<Represent>
        {
            clear_error(ec);
        }

        /// Sets the size of the data. New space is zero; the offset is not changed.
        /// @return `new_size`. Reports errc::value_too_large if it does not fit in
        ///         std::size_t, errc::not_enough_memory if growing failed; the size is
        ///         unchanged then, and the current size is returned.
        /// @post on success size() == new_size
        long_size_t truncate(long_size_t new_size, error_code_ptr ec = nullptr)
            requires truncatable_represent<Represent>
        {
            if (!in_size_t_range(new_size))
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return data_size();
            }
            if (!resize_data(to_size_t(new_size), ec))
                return data_size();
            clear_error(ec);
            JIO_Ensures(data_size() == new_size);
            return new_size;
        }

        /// Access to the represent. Changing it changes the device.
        Represent &underlying() noexcept { return m_rep; }
        const Represent &underlying() const noexcept { return m_rep; }

    private:
        std::size_t pos() const noexcept { return detail::represent_pos(m_rep); }

        std::size_t data_size() const noexcept
        {
            return static_cast<std::size_t>(std::ranges::size(detail::represent_data(m_rep)));
        }

        const std::byte *read_data() const noexcept
        {
            return std::ranges::data(detail::represent_data(m_rep));
        }

        std::byte *write_data() noexcept
            requires write_represent<Represent>
        {
            return std::ranges::data(detail::represent_data(m_rep));
        }

        // The one place that turns exceptions into errors: containers report failure to grow by
        // throwing, the device reports it as described in error.hpp. A failed resize leaves the
        // data unchanged.
        bool resize_data(std::size_t new_size, error_code_ptr ec)
            requires truncatable_represent<Represent>
        {
            try
            {
                detail::represent_data(m_rep).resize(new_size);
            }
            catch (const std::bad_alloc &)
            {
                set_error_or_throw(ec, std::errc::not_enough_memory);
                return false;
            }
            catch (const std::length_error &)
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return false;
            }
            return true;
        }

        Represent m_rep;
    };
} // namespace jetio
