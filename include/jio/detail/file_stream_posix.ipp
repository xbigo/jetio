#pragma once

// The file streams on POSIX, see file_stream.ipp. The offset is kept here, not in the file
// description, so devices that share a file do not move each other.

#include <jio/file_stream.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <limits>
#include <memory>
#include <system_error>

static_assert(sizeof(off_t) == sizeof(jio::offset64_t),
              "file offsets must be 64 bit: define _FILE_OFFSET_BITS=64");

namespace jio::detail
{
    /// The most that one call transfers, so the count fits every platform's limit; a read or a
    /// write may be short anyway.
    inline constexpr std::size_t max_transfer = std::size_t{1} << 30;

    inline error_code errno_code() noexcept
    {
        return error_code(errno, std::generic_category());
    }

    template <typename Call> auto retry_on_eintr(Call call)
    {
        auto result = call();
        while (result == -1 && errno == EINTR)
            result = call();
        return result;
    }

    template <file_access Access> class file_stream
    {
    public:
        static constexpr bool readable = Access != file_access::write;
        static constexpr bool writable = Access != file_access::read;

        /// Takes the open file `fd`. The destructor reports through `ec`, which may be null.
        file_stream(int fd, error_code_ptr ec) noexcept : m_fd(fd), m_ec(ec) {}
        file_stream(const file_stream &) = delete;
        file_stream &operator=(const file_stream &) = delete;

        ~file_stream()
        {
            if (::close(m_fd) == -1)
            {
                if (m_ec)
                    *m_ec = errno_code();
            }
            else
            {
                clear_error(m_ec);
            }
        }

        mutable_buffer read(mutable_buffer buf, error_code_ptr ec)
            requires readable
        {
            const std::size_t count = transfer_count_(buf.size());
            if (count == 0) // nothing wanted, or nothing exists at the largest offset
            {
                clear_error(ec);
                return buf.first(0);
            }
            const ssize_t got =
                retry_on_eintr([&] { return ::pread(m_fd, buf.data(), count, m_off); });
            if (got < 0)
            {
                set_error_or_throw(ec, errno_code());
                return buf.first(0);
            }
            m_off += got;
            clear_error(ec);
            return buf.first(static_cast<std::size_t>(got));
        }

        const_buffer write(const_buffer buf, error_code_ptr ec)
            requires writable
        {
            if (buf.empty())
            {
                clear_error(ec);
                return buf;
            }
            const std::size_t count = transfer_count_(buf.size());
            if (count == 0)
            {
                set_error_or_throw(ec, std::errc::file_too_large);
                return buf;
            }
            const ssize_t put =
                retry_on_eintr([&] { return ::pwrite(m_fd, buf.data(), count, m_off); });
            if (put < 0)
            {
                set_error_or_throw(ec, errno_code());
                return buf;
            }
            m_off += put;
            clear_error(ec);
            return buf.last(buf.size() - static_cast<std::size_t>(put));
        }

        offset64_t offset(error_code_ptr ec) const noexcept
        {
            clear_error(ec);
            return m_off;
        }

        offset64_t seek(offset64_t off, error_code_ptr ec) noexcept
        {
            clear_error(ec);
            m_off = off;
            return m_off;
        }

        size64_t size(error_code_ptr ec) const
        {
            struct stat info;
            if (retry_on_eintr([&] { return ::fstat(m_fd, &info); }) == -1)
            {
                set_error_or_throw(ec, errno_code());
                return unknown_size;
            }
            clear_error(ec);
            return static_cast<size64_t>(info.st_size);
        }

        bool is_eof(error_code_ptr ec) const
            requires readable
        {
            error_code size_ec;
            const size64_t end = size(&size_ec);
            if (size_ec)
            {
                set_error_or_throw(ec, size_ec);
                return true; // no data can be read
            }
            clear_error(ec);
            return std::cmp_greater_equal(m_off, end);
        }

        void sync(error_code_ptr ec)
            requires writable
        {
            if (retry_on_eintr([&] { return ::fsync(m_fd); }) == -1)
                set_error_or_throw(ec, errno_code());
            else
                clear_error(ec);
        }

        size64_t truncate(size64_t new_size, error_code_ptr ec)
            requires writable
        {
            const bool fits = new_size <= static_cast<size64_t>(std::numeric_limits<off_t>::max());
            if (!fits)
            {
                set_error_or_throw(ec, std::errc::file_too_large);
            }
            else if (retry_on_eintr(
                         [&] { return ::ftruncate(m_fd, static_cast<off_t>(new_size)); }) == -1)
            {
                set_error_or_throw(ec, errno_code());
            }
            else
            {
                clear_error(ec);
                return new_size;
            }
            // the error is reported; the size is what the file has now
            error_code ignored;
            return size(&ignored);
        }

    private:
        /// @return how much of `wanted` one call transfers at the current offset
        std::size_t transfer_count_(std::size_t wanted) const noexcept
        {
            const auto room = static_cast<size64_t>(std::numeric_limits<offset64_t>::max() - m_off);
            return static_cast<std::size_t>(std::min<size64_t>({wanted, max_transfer, room}));
        }

        int m_fd{-1};
        error_code_ptr m_ec{nullptr};
        offset64_t m_off{0};
    };

    /// Opens `file`; `ec` is also what the returned device reports its close through.
    /// @return the device, or null if the open failed and `ec` is not null
    template <file_access Access>
    std::unique_ptr<file_stream<Access>> open_stream(const path &file, open_mode mode,
                                                     error_code_ptr ec)
    {
        int flags = O_CLOEXEC;
        switch (Access)
        {
        case file_access::read:
            flags |= O_RDONLY;
            break;
        case file_access::write:
            flags |= O_WRONLY;
            break;
        case file_access::read_write:
            flags |= O_RDWR;
            break;
        }
        switch (mode)
        {
        case open_mode::existing:
            break;
        case open_mode::create:
            flags |= O_CREAT;
            break;
        case open_mode::create_new:
            flags |= O_CREAT | O_EXCL;
            break;
        case open_mode::empty:
            flags |= O_CREAT | O_TRUNC;
            break;
        }

        const int fd = retry_on_eintr([&] { return ::open(file.c_str(), flags, 0666); });
        if (fd == -1)
        {
            set_error_or_throw(ec, errno_code());
            return nullptr;
        }
        clear_error(ec);
        try
        {
            return std::make_unique<file_stream<Access>>(fd, ec);
        }
        catch (...)
        {
            ::close(fd);
            throw;
        }
    }
} // namespace jio::detail
