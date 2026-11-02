#pragma once

// Views over devices that cannot map their data, made by copying it. They turn a reader into
// a read_map and a writer into a write_map, at the price of the copy.
//
//   reader_to_view  view_rd reads the range into memory (cache_rd_view)
//   writer_to_view  view_wr gives memory that is written to the device when the view is destroyed
//                   (cache_wr_view)

#include <jetio/algorithm.hpp>
#include <jetio/iocore.hpp>

#include <cstddef>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace jetio
{
    /// Read-only view of a copy of a part of a device.
    class cache_rd_view
    {
    public:
        cache_rd_view() noexcept = default;
        explicit cache_rd_view(std::vector<std::byte> data) noexcept : m_data(std::move(data)) {}

        const_buffer address() const noexcept { return m_data; }

    private:
        std::vector<std::byte> m_data;
    };

    namespace detail
    {
        /// Allocates n bytes, zeroed. Reports errc::value_too_large if n does not fit in
        /// std::size_t, errc::not_enough_memory if the allocation fails.
        /// @return false after an error
        inline bool allocate_bytes(size64_t n, std::vector<std::byte> &bytes, error_code_ptr ec)
        {
            if (!in_size_t_range(n))
            {
                set_error_or_throw(ec, std::errc::value_too_large);
                return false;
            }
            try
            {
                bytes.assign(to_size_t(n), std::byte{});
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

        /// The end of `rng` for a device that knows its size, if the end is unknown.
        /// Reports errc::invalid_argument if the end cannot be known.
        /// @return false after an error
        template <typename Device>
        bool known_end(Device &device, offset64_range rng, offset64_t &end, error_code_ptr ec)
        {
            if (rng.end != unknown_offset)
            {
                end = rng.end;
                return true;
            }
            if constexpr (sizer<Device &>)
            {
                const size64_t size = jetio::size(device, ec);
                if (has_error(ec))
                    return false;
                if (size != unknown_size && size < static_cast<size64_t>(unknown_offset))
                {
                    end = static_cast<offset64_t>(size);
                    return true;
                }
            }
            set_error_or_throw(ec, std::errc::invalid_argument);
            return false;
        }
    } // namespace detail

    /// Makes a read_map of a reader: view_rd reads the bytes of the range into memory. The
    /// device moves to the begin of the range first, forward if it can only do that
    /// (precondition of seek_forward). An unknown end of the range is the size of the device if
    /// it knows it. Provides [ read_map ] ([ sizer ] as the device does).
    template <typename Device>
        requires reader<Device &> && forward_access<Device &>
    class reader_to_view
    {
    public:
        explicit reader_to_view(Device &device) noexcept : m_device(device) {}

        Device &underlying() noexcept { return m_device; }
        const Device &underlying() const noexcept { return m_device; }

        size64_t size(error_code_ptr ec = nullptr) const
            requires sizer<Device &>
        {
            return jetio::size(m_device, ec);
        }

        /// @pre is_valid_range(rng)
        /// @return the view. Reports errc::invalid_argument, and returns an empty view, if
        ///         the range lies beyond the end of the data or its end cannot be known,
        ///         errc::value_too_large and errc::not_enough_memory if it cannot be held in
        ///         memory, and the errors of the device.
        cache_rd_view view_rd(offset64_range rng, error_code_ptr ec = nullptr)
        {
            offset64_t end = 0;
            if (!detail::known_end(m_device, rng, end, ec))
                return {};
            if (rng.begin > end)
            {
                set_error_or_throw(ec, std::errc::invalid_argument);
                return {};
            }
            std::vector<std::byte> data;
            if (!detail::allocate_bytes(static_cast<size64_t>(end - rng.begin), data, ec))
                return {};

            if constexpr (random_access<Device &>)
                jetio::seek(m_device, static_cast<size64_t>(rng.begin), ec);
            else
                jetio::seek_forward(m_device, static_cast<size64_t>(rng.begin), ec);
            if (has_error(ec))
                return {};

            const mutable_buffer filled = jetio::read_full(m_device, data, ec);
            if (has_error(ec))
                return {};
            if (filled.size() != data.size())
            {
                set_error_or_throw(ec, std::errc::invalid_argument);
                return {};
            }
            return cache_rd_view(std::move(data));
        }

    private:
        Device &m_device;
    };

    /// Writable view of a copy of a part of a device: the bytes of the view, which start as
    /// zeros, are written to the device at the begin of the range when the view is destroyed.
    ///
    /// The destructor writes as well as it can, like any other operation on `ec`: the view keeps
    /// the `ec` that view_wr was given, the result is stored there (cleared on success), and
    /// without an `ec` a failure is dropped; the destructor does not throw. A view that
    /// view_wr could not make writes nothing. To see a failure, pass an `ec` and look at it
    /// after the view is destroyed. `ec` must outlive the view.
    template <typename Device>
        requires writer<Device &> && random_access<Device &>
    class cache_wr_view
    {
    public:
        /// A view of nothing: it has no bytes and writes nothing.
        cache_wr_view() noexcept : m_pending(false) {}

        cache_wr_view(Device &device, std::vector<std::byte> data, offset64_t pos,
                      error_code_ptr ec) noexcept
            : m_device(&device), m_data(std::move(data)), m_pos(pos), m_ec(ec)
        {
        }

        cache_wr_view(cache_wr_view &&other) noexcept
            : m_device(other.m_device), m_data(std::move(other.m_data)), m_pos(other.m_pos),
              m_ec(other.m_ec), m_pending(other.m_pending)
        {
            other.m_pending = false;
        }
        cache_wr_view(const cache_wr_view &) = delete;
        cache_wr_view &operator=(const cache_wr_view &) = delete;
        cache_wr_view &operator=(cache_wr_view &&) = delete;

        ~cache_wr_view()
        {
            if (!m_pending)
                return;
            try
            {
                error_code dropped;
                write_back_(m_ec ? m_ec : &dropped);
            }
            catch (...) // NOLINT(bugprone-empty-catch) a destructor must not throw
            {
            }
        }

        mutable_buffer address() noexcept { return m_data; }

    private:
        void write_back_(error_code_ptr ec)
        {
            jetio::seek(*m_device, static_cast<size64_t>(m_pos), ec);
            if (has_error(ec))
                return;
            // write may take less than it is given: write the rest, stop at the first error
            for (const_buffer rest = m_data; !rest.empty();)
            {
                rest = jetio::write(*m_device, rest, ec);
                if (has_error(ec))
                    return;
            }
        }

        Device *m_device = nullptr;
        std::vector<std::byte> m_data;
        offset64_t m_pos = 0;
        error_code_ptr m_ec = nullptr;
        bool m_pending = true;
    };

    /// Makes a write_map of a writer: view_wr gives memory that is written to the device when the
    /// view is destroyed.
    /// An unknown end of the range is the size of the device if it knows it. Provides
    /// [ write_map ] ([ sizer ] as the device does).
    template <typename Device>
        requires writer<Device &> && random_access<Device &>
    class writer_to_view
    {
    public:
        explicit writer_to_view(Device &device) noexcept : m_device(device) {}

        Device &underlying() noexcept { return m_device; }
        const Device &underlying() const noexcept { return m_device; }

        size64_t size(error_code_ptr ec = nullptr) const
            requires sizer<Device &>
        {
            return jetio::size(m_device, ec);
        }

        /// @pre is_valid_range(rng)
        /// @return the view, which keeps `ec` (see cache_wr_view). Reports
        ///         errc::invalid_argument, and returns a view that writes nothing, if the end
        ///         of the range cannot be known, errc::value_too_large and
        ///         errc::not_enough_memory if it cannot be held in memory.
        cache_wr_view<Device> view_wr(offset64_range rng, error_code_ptr ec = nullptr)
        {
            offset64_t end = 0;
            if (!detail::known_end(m_device, rng, end, ec))
                return {};
            if (rng.begin > end)
            {
                set_error_or_throw(ec, std::errc::invalid_argument);
                return {};
            }
            std::vector<std::byte> data;
            if (!detail::allocate_bytes(static_cast<size64_t>(end - rng.begin), data, ec))
                return {};
            clear_error(ec);
            return cache_wr_view<Device>(m_device, std::move(data), rng.begin, ec);
        }

    private:
        Device &m_device;
    };
} // namespace jetio
