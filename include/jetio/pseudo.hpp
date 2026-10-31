#pragma once

// Pseudo devices that have no storage: [ sequence, random_access, forward_access ] and as listed
// per device. None of them can fail, so none reports an error.

#include <jetio/iocore.hpp>

#include <algorithm>
#include <cstddef>
#include <system_error>

namespace jetio
{
    namespace detail
    {
        /// Offset handling of the devices that accept any offset and have unknown size.
        class pseudo_common
        {
        public:
            size64_t offset(error_code_ptr ec = nullptr) const noexcept
            {
                clear_error(ec);
                return m_pos;
            }

            bool is_eof(error_code_ptr ec = nullptr) const noexcept
            {
                clear_error(ec);
                return false;
            }

            size64_t seek(size64_t off, error_code_ptr ec = nullptr) noexcept
            {
                clear_error(ec);
                return m_pos = off;
            }

            size64_t size(error_code_ptr ec = nullptr) const noexcept
            {
                clear_error(ec);
                return unknown_size;
            }

        protected:
            size64_t m_pos = 0;
        };
    } // namespace detail

    /// Reads zeros forever. [ reader, is_eofer, sizer ]
    class zero_device : public detail::pseudo_common
    {
    public:
        /// @return buf, filled with zeros
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            std::ranges::fill(buf, std::byte{});
            m_pos += buf.size();
            return buf;
        }
    };

    /// Reads a pattern, repeated forever: the byte at offset n is pattern[n % pattern.size()].
    /// [ reader, is_eofer, sizer ]
    class fill_device : public detail::pseudo_common
    {
    public:
        /// @pre !pattern.empty()
        /// @pre the memory of pattern outlives the device
        explicit fill_device(const_buffer pattern) : m_pattern(pattern)
        {
            JIO_Expects_critical(!pattern.empty());
        }

        /// @return buf, filled with the pattern
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            auto out = buf.begin();
            std::size_t left = buf.size();
            auto index = static_cast<std::size_t>(m_pos % m_pattern.size());
            while (left != 0)
            {
                const std::size_t count = std::min(left, m_pattern.size() - index);
                out =
                    std::copy_n(m_pattern.begin() + static_cast<std::ptrdiff_t>(index), count, out);
                left -= count;
                index = 0;
            }
            m_pos += buf.size();
            return buf;
        }

    private:
        const_buffer m_pattern;
    };

    /// Discards everything written, like /dev/null. The size is always 0, whatever the
    /// offset. [ writer, syncer, truncater, is_eofer, sizer ]
    class null_device
    {
    public:
        /// @return nothing left: everything is written
        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            m_pos += buf.size();
            return buf.last(0);
        }

        void sync(error_code_ptr ec = nullptr) noexcept { clear_error(ec); }

        /// The device cannot grow. Reports errc::invalid_argument if new_size is not 0.
        /// @return the size, 0
        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr)
        {
            if (new_size != 0)
                set_error_or_throw(ec, std::errc::invalid_argument);
            else
                clear_error(ec);
            return 0;
        }

        size64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return m_pos;
        }

        size64_t seek(size64_t off, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            return m_pos = off;
        }

        bool is_eof(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return true;
        }

        size64_t size(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return 0;
        }

    private:
        size64_t m_pos = 0;
    };

    /// Has no data: reads nothing, the offset is always 0. [ reader, is_eofer, sizer ]
    class empty_device
    {
    public:
        /// @return nothing: the end of the data is always reached
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            return buf.first(0);
        }

        size64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return 0;
        }

        /// @return 0, whatever `off` is
        size64_t seek(size64_t /*off*/, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            return 0;
        }

        bool is_eof(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return true;
        }

        size64_t size(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return 0;
        }
    };
} // namespace jetio
