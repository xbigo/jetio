#pragma once

// A device that goes wrong when its policy says so, to test code that must cope with errors and
// with transfers that stop short.

#include <jio/iocore.hpp>
#include <jio/memory.hpp>

#include <algorithm>
#include <cstddef>
#include <system_error>
#include <utility>
#include <vector>

namespace jio
{
    /// A memory device whose operations are handed to the policy. [ the capabilities of
    /// memory_device<> ]
    ///
    /// The policy has a member function for each operation it takes over, with the arguments of
    /// the operation after the device, and decides what happens: whether and how the device does
    /// it, what is reported, what is returned. An operation the policy has no function for goes
    /// to the device.
    ///
    ///   mutable_buffer read(memory_device<> &, mutable_buffer buf, error_code_ptr ec)
    ///   const_buffer write(memory_device<> &, const_buffer buf, error_code_ptr ec)
    ///   offset64_t seek(memory_device<> &, offset64_t off, error_code_ptr ec)
    ///   offset64_t seek_forward(memory_device<> &, offset64_t off, error_code_ptr ec)
    ///   void sync(memory_device<> &, error_code_ptr ec)
    ///   size64_t truncate(memory_device<> &, size64_t new_size, error_code_ptr ec)
    ///   buffer_rd_view view_rd(memory_device<> &, offset64_range rng, error_code_ptr ec)
    ///   buffer_wr_view view_wr(memory_device<> &, offset64_range rng, error_code_ptr ec)
    ///
    /// and, as `const` functions that get a `const` device, offset, is_eof and size. Without a
    /// seek_forward of its own, a seek_forward goes to the seek of the policy. A policy keeps its
    /// own record, of the calls for example.
    template <typename Policy> class bad_device
    {
    public:
        explicit bad_device(Policy policy = {}, std::vector<std::byte> data = {})
            : m_policy(std::move(policy)), m_device(vector_buffer_represent{std::move(data), 0})
        {
        }

        /// The device behind the policy, which does not ask it.
        memory_device<> &memory() noexcept { return m_device; }
        const memory_device<> &memory() const noexcept { return m_device; }

        Policy &policy() noexcept { return m_policy; }
        const Policy &policy() const noexcept { return m_policy; }

        offset64_t offset(error_code_ptr ec = nullptr) const
        {
            if constexpr (requires { m_policy.offset(m_device, ec); })
                return m_policy.offset(m_device, ec);
            else
                return jio::offset(m_device, ec);
        }

        bool is_eof(error_code_ptr ec = nullptr) const
        {
            if constexpr (requires { m_policy.is_eof(m_device, ec); })
                return m_policy.is_eof(m_device, ec);
            else
                return jio::is_eof(m_device, ec);
        }

        size64_t size(error_code_ptr ec = nullptr) const
        {
            if constexpr (requires { m_policy.size(m_device, ec); })
                return m_policy.size(m_device, ec);
            else
                return jio::size(m_device, ec);
        }

        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.seek(m_device, off, ec); })
                return m_policy.seek(m_device, off, ec);
            else
                return jio::seek(m_device, off, ec);
        }

        offset64_t seek_forward(offset64_t off, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.seek_forward(m_device, off, ec); })
                return m_policy.seek_forward(m_device, off, ec);
            else
                return seek(off, ec);
        }

        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.read(m_device, buf, ec); })
                return m_policy.read(m_device, buf, ec);
            else
                return jio::read(m_device, buf, ec);
        }

        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.write(m_device, buf, ec); })
                return m_policy.write(m_device, buf, ec);
            else
                return jio::write(m_device, buf, ec);
        }

        void sync(error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.sync(m_device, ec); })
                m_policy.sync(m_device, ec);
            else
                jio::sync(m_device, ec);
        }

        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.truncate(m_device, new_size, ec); })
                return m_policy.truncate(m_device, new_size, ec);
            else
                return jio::truncate(m_device, new_size, ec);
        }

        buffer_rd_view view_rd(offset64_range rng, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.view_rd(m_device, rng, ec); })
                return m_policy.view_rd(m_device, rng, ec);
            else
                return jio::view_rd(m_device, rng, ec);
        }

        buffer_wr_view view_wr(offset64_range rng, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.view_wr(m_device, rng, ec); })
                return m_policy.view_wr(m_device, rng, ec);
            else
                return jio::view_wr(m_device, rng, ec);
        }

    private:
        Policy m_policy;
        memory_device<> m_device;
    };

    /// Policies for bad_device.
    namespace bad
    {
        /// The reads and writes together let through `limit` bytes. The call that goes beyond
        /// transfers the rest, the next ones fail with `ec`; the other operations pass.
        /// The bytes are counted as let through, even if the device then takes less.
        class after_bytes
        {
        public:
            explicit after_bytes(std::size_t limit,
                                 error_code ec = std::make_error_code(std::errc::io_error))
                : m_left(limit), m_ec(ec)
            {
            }

            mutable_buffer read(memory_device<> &device, mutable_buffer buf, error_code_ptr ec)
            {
                const std::size_t n = grant_(buf.size(), ec);
                return n == 0 ? buf.first(0) : jio::read(device, buf.first(n), ec);
            }

            const_buffer write(memory_device<> &device, const_buffer buf, error_code_ptr ec)
            {
                const std::size_t n = grant_(buf.size(), ec);
                if (n == 0)
                    return buf;
                const const_buffer rest = jio::write(device, buf.first(n), ec);
                return buf.last(buf.size() - (n - rest.size()));
            }

        private:
            /// @return how many of `wanted` bytes may go, 0 for none: the error is reported if
            ///         some were wanted
            std::size_t grant_(std::size_t wanted, error_code_ptr ec)
            {
                if (wanted != 0 && m_left == 0)
                {
                    set_error_or_throw(ec, m_ec);
                    return 0;
                }
                clear_error(ec);
                const std::size_t n = std::min(wanted, m_left);
                m_left -= n;
                return n;
            }

            std::size_t m_left;
            error_code m_ec;
        };

        /// The `call`-th operation, counting from 1 and all the operations of the device except
        /// the queries, fails with `ec`, once.
        class on_call
        {
        public:
            explicit on_call(std::size_t call,
                             error_code ec = std::make_error_code(std::errc::io_error))
                : m_left(call), m_ec(ec)
            {
            }

            mutable_buffer read(memory_device<> &device, mutable_buffer buf, error_code_ptr ec)
            {
                return failed_(ec) ? buf.first(0) : jio::read(device, buf, ec);
            }

            const_buffer write(memory_device<> &device, const_buffer buf, error_code_ptr ec)
            {
                return failed_(ec) ? buf : jio::write(device, buf, ec);
            }

            offset64_t seek(memory_device<> &device, offset64_t off, error_code_ptr ec)
            {
                return failed_(ec) ? device.offset() : jio::seek(device, off, ec);
            }

            size64_t truncate(memory_device<> &device, size64_t new_size, error_code_ptr ec)
            {
                return failed_(ec) ? device.size() : jio::truncate(device, new_size, ec);
            }

            void sync(memory_device<> &device, error_code_ptr ec)
            {
                if (!failed_(ec))
                    jio::sync(device, ec);
            }

            buffer_rd_view view_rd(memory_device<> &device, offset64_range rng, error_code_ptr ec)
            {
                return failed_(ec) ? buffer_rd_view{} : jio::view_rd(device, rng, ec);
            }

            buffer_wr_view view_wr(memory_device<> &device, offset64_range rng, error_code_ptr ec)
            {
                return failed_(ec) ? buffer_wr_view{} : jio::view_wr(device, rng, ec);
            }

        private:
            /// @return true if this call is the one that fails: the error is reported
            bool failed_(error_code_ptr ec)
            {
                if (m_left == 0)
                    return false;
                --m_left;
                if (m_left != 0)
                    return false;
                set_error_or_throw(ec, m_ec);
                return true;
            }

            std::size_t m_left;
            error_code m_ec;
        };

        /// Reads and writes transfer at most `most` bytes per call, and never fail.
        class at_most
        {
        public:
            /// @pre most > 0
            explicit at_most(std::size_t most) : m_most(most) { JIO_Expects(most > 0); }

            mutable_buffer read(memory_device<> &device, mutable_buffer buf, error_code_ptr ec)
            {
                return jio::read(device, buf.first(std::min(buf.size(), m_most)), ec);
            }

            const_buffer write(memory_device<> &device, const_buffer buf, error_code_ptr ec)
            {
                const std::size_t n = std::min(buf.size(), m_most);
                const const_buffer rest = jio::write(device, buf.first(n), ec);
                return buf.last(buf.size() - (n - rest.size()));
            }

        private:
            std::size_t m_most;
        };
    } // namespace bad
} // namespace jio
