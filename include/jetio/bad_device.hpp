#pragma once

// A device that goes wrong when its policy says so, to test code that must cope with errors and
// with transfers that stop short.

#include <jetio/iocore.hpp>
#include <jetio/memory.hpp>

#include <algorithm>
#include <cstddef>
#include <system_error>
#include <utility>
#include <vector>

namespace jetio
{
    /// A memory device whose operations ask the policy first. [ the capabilities of
    /// memory_device<> ]
    ///
    /// The policy has a member function for each operation it wants to judge, and the others pass.
    /// It gets the arguments by reference, and returns an error code:
    ///
    ///   error_code read(mutable_buffer &buf)        error_code write(const_buffer &buf)
    ///   error_code seek(size64_t &off)              error_code truncate(size64_t &new_size)
    ///   error_code sync()
    ///   error_code view_rd(offset64_range &rng)     error_code view_wr(offset64_range &rng)
    ///
    /// An error is reported, like the device would, and the operation does not happen. Without
    /// one, the operation happens with the arguments as the policy left them: it may have made
    /// `buf` shorter (the first bytes of it) for a short read or write, or changed the others. A
    /// policy keeps its own record, of the calls for example. The queries offset, is_eof and size
    /// are not asked.
    /// @pre a policy does not make a non empty buf empty without an error
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

        size64_t offset(error_code_ptr ec = nullptr) const noexcept
        {
            return jetio::offset(m_device, ec);
        }
        bool is_eof(error_code_ptr ec = nullptr) const noexcept
        {
            return jetio::is_eof(m_device, ec);
        }
        size64_t size(error_code_ptr ec = nullptr) const noexcept
        {
            return jetio::size(m_device, ec);
        }

        size64_t seek(size64_t off, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.seek(off); })
            {
                if (refused_(ec, m_policy.seek(off)))
                    return m_device.offset();
            }
            return jetio::seek(m_device, off, ec);
        }

        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr)
        {
            mutable_buffer allowed = buf;
            if constexpr (requires { m_policy.read(allowed); })
            {
                if (refused_(ec, m_policy.read(allowed)))
                    return buf.first(0);
                JIO_Expects(detail::is_prefix(allowed, buf) && (!allowed.empty() || buf.empty()));
            }
            return jetio::read(m_device, allowed, ec);
        }

        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr)
        {
            const_buffer allowed = buf;
            if constexpr (requires { m_policy.write(allowed); })
            {
                if (refused_(ec, m_policy.write(allowed)))
                    return buf;
                JIO_Expects(detail::is_prefix(allowed, buf) && (!allowed.empty() || buf.empty()));
            }
            const const_buffer rest = jetio::write(m_device, allowed, ec);
            return buf.last(buf.size() - (allowed.size() - rest.size()));
        }

        void sync(error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.sync(); })
            {
                if (refused_(ec, m_policy.sync()))
                    return;
            }
            jetio::sync(m_device, ec);
        }

        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.truncate(new_size); })
            {
                if (refused_(ec, m_policy.truncate(new_size)))
                    return m_device.size();
            }
            return jetio::truncate(m_device, new_size, ec);
        }

        buffer_rd_view view_rd(offset64_range rng, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.view_rd(rng); })
            {
                if (refused_(ec, m_policy.view_rd(rng)))
                    return {};
            }
            return jetio::view_rd(m_device, rng, ec);
        }

        buffer_wr_view view_wr(offset64_range rng, error_code_ptr ec = nullptr)
        {
            if constexpr (requires { m_policy.view_wr(rng); })
            {
                if (refused_(ec, m_policy.view_wr(rng)))
                    return {};
            }
            return jetio::view_wr(m_device, rng, ec);
        }

    private:
        /// @return true if the policy gave an error, which is reported
        static bool refused_(error_code_ptr ec, const error_code &e)
        {
            if (!e)
                return false;
            set_error_or_throw(ec, e);
            return true;
        }

        Policy m_policy;
        memory_device<> m_device;
    };

    /// Policies for bad_device.
    namespace bad
    {
        /// The reads and writes together let through `limit` bytes. The call that goes beyond
        /// transfers the rest, the next ones fail with `ec`; the other operations are not asked.
        /// The bytes are counted as let through, even if the device then takes less.
        class after_bytes
        {
        public:
            explicit after_bytes(std::size_t limit,
                                 error_code ec = std::make_error_code(std::errc::io_error))
                : m_left(limit), m_ec(ec)
            {
            }

            error_code read(mutable_buffer &buf) { return take_(buf); }
            error_code write(const_buffer &buf) { return take_(buf); }

        private:
            template <typename Buffer> error_code take_(Buffer &buf)
            {
                if (buf.empty())
                    return {};
                if (m_left == 0)
                    return m_ec;
                buf = buf.first(std::min(buf.size(), m_left));
                m_left -= buf.size();
                return {};
            }

            std::size_t m_left;
            error_code m_ec;
        };

        /// The `call`-th operation, counting from 1 and all the operations that are asked, fails
        /// with `ec`, once.
        class on_call
        {
        public:
            explicit on_call(std::size_t call,
                             error_code ec = std::make_error_code(std::errc::io_error))
                : m_left(call), m_ec(ec)
            {
            }

            error_code read(mutable_buffer &) { return step_(); }
            error_code write(const_buffer &) { return step_(); }
            error_code seek(size64_t &) { return step_(); }
            error_code truncate(size64_t &) { return step_(); }
            error_code sync() { return step_(); }
            error_code view_rd(offset64_range &) { return step_(); }
            error_code view_wr(offset64_range &) { return step_(); }

        private:
            error_code step_()
            {
                if (m_left == 0)
                    return {};
                --m_left;
                return m_left == 0 ? m_ec : error_code{};
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

            error_code read(mutable_buffer &buf) { return cut_(buf); }
            error_code write(const_buffer &buf) { return cut_(buf); }

        private:
            template <typename Buffer> error_code cut_(Buffer &buf) const
            {
                buf = buf.first(std::min(buf.size(), m_most));
                return {};
            }

            std::size_t m_most;
        };
    } // namespace bad
} // namespace jetio
