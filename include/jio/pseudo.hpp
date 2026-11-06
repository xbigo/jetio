#pragma once

// Pseudo devices that have no storage: [ sequence, random_access, forward_access ] and as listed
// per device. None of them can fail, so none reports an error. (bad_device.hpp has the one that
// fails on demand.)

#include <jio/iocore.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace jio
{
    namespace detail
    {
        /// Offset handling of the devices that accept any offset and have unknown size.
        class pseudo_common
        {
        public:
            offset64_t offset(error_code_ptr ec = nullptr) const noexcept
            {
                clear_error(ec);
                return m_pos;
            }

            bool is_eof(error_code_ptr ec = nullptr) const noexcept
            {
                clear_error(ec);
                return false;
            }

            offset64_t seek(offset64_t off, error_code_ptr ec = nullptr) noexcept
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
            offset64_t m_pos = 0;
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
            m_pos += static_cast<offset64_t>(buf.size());
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
        explicit constexpr fill_device(const_buffer pattern) noexcept : m_pattern(pattern)
        {
            JIO_Expects_critical(!pattern.empty());
        }

        /// @return buf, filled with the pattern
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr) noexcept
        {
            clear_error(ec);
            auto out = buf.begin();
            std::size_t left = buf.size();
            auto index = static_cast<std::size_t>(m_pos) % m_pattern.size();
            while (left != 0)
            {
                const std::size_t count = std::min(left, m_pattern.size() - index);
                out =
                    std::copy_n(m_pattern.begin() + static_cast<std::ptrdiff_t>(index), count, out);
                left -= count;
                index = 0;
            }
            m_pos += static_cast<offset64_t>(buf.size());
            return buf;
        }

    private:
        const_buffer m_pattern;
    };

    namespace detail
    {
        /// g(offset) gives the byte at that offset, whatever was asked before.
        template <typename G>
        concept positional_generator =
            std::invocable<G &, offset64_t> &&
            std::same_as<std::invoke_result_t<G &, offset64_t>, std::byte>;

        /// g() gives the next byte.
        template <typename G>
        concept sequential_generator =
            std::invocable<G &> && std::same_as<std::invoke_result_t<G &>, std::byte>;
    } // namespace detail

    /// Reads the bytes that a generator makes, forever. [ reader, is_eofer, sizer ]
    /// The generator is called as g(offset), a positional generator, or as g(), a sequential one,
    /// and gives a std::byte. A positional generator must give the same byte for the same offset,
    /// which makes the device random access, and a read of it the same on every run. A sequential
    /// generator (a random number engine, for example) can only go on, so the device is forward
    /// access: it skips bytes by making and dropping them. If a generator can be called both ways,
    /// it is positional.
    /// @pre the generator does not throw
    template <typename Generator>
        requires detail::positional_generator<Generator> || detail::sequential_generator<Generator>
    class generator_device
    {
    public:
        static constexpr bool positional = detail::positional_generator<Generator>;

        explicit generator_device(Generator generator) noexcept(
            std::is_nothrow_move_constructible_v<Generator>)
            : m_generator(std::move(generator))
        {
        }

        offset64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return m_pos;
        }

        bool is_eof(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return false;
        }

        size64_t size(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return unknown_size;
        }

        /// Positional generator only.
        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr) noexcept
            requires positional
        {
            clear_error(ec);
            return m_pos = off;
        }

        /// Moves the offset forward; a sequential generator makes and drops the bytes in between.
        /// @pre off >= offset()
        offset64_t seek_forward(offset64_t off, error_code_ptr ec = nullptr)
        {
            JIO_Expects(off >= m_pos);
            clear_error(ec);
            if constexpr (positional)
                m_pos = off;
            else
            {
                for (; m_pos < off; ++m_pos)
                    std::ignore = m_generator();
            }
            return m_pos;
        }

        /// @return buf, filled with the bytes of the generator
        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
        {
            clear_error(ec);
            if constexpr (positional)
            {
                offset64_t at = m_pos;
                for (std::byte &b : buf)
                    b = m_generator(at++);
            }
            else
            {
                for (std::byte &b : buf)
                    b = m_generator();
            }
            m_pos += static_cast<offset64_t>(buf.size());
            return buf;
        }

    private:
        Generator m_generator;
        offset64_t m_pos = 0;
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
            m_pos += static_cast<offset64_t>(buf.size());
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

        offset64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return m_pos;
        }

        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr) noexcept
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
        offset64_t m_pos = 0;
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

        offset64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            clear_error(ec);
            return 0;
        }

        /// @return 0, whatever `off` is
        offset64_t seek(offset64_t /*off*/, error_code_ptr ec = nullptr) noexcept
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
} // namespace jio
