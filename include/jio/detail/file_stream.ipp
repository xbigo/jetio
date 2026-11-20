#pragma once

// The implementation of file_stream.hpp: included by the header with JIO_HEADER_ONLY, otherwise
// by src/file_stream.cpp only.

#include <jio/file_stream.hpp>

#if defined(_WIN32)
#error "file streams are not implemented for Windows yet"
#else
#include <jio/detail/file_stream_posix.ipp>
#endif

#ifdef JIO_HEADER_ONLY
#define JIO_INLINE inline
#else
#define JIO_INLINE
#endif

namespace jio
{
    // file_reader

    JIO_INLINE file_reader::file_reader(const path &file, error_code_ptr ec)
        : m_impl(detail::open_stream<detail::file_access::read>(file, open_mode::existing, ec))
    {
    }
    JIO_INLINE file_reader::file_reader(file_reader &&) noexcept = default;
    JIO_INLINE file_reader &file_reader::operator=(file_reader &&) noexcept = default;
    JIO_INLINE file_reader::~file_reader() = default;

    JIO_INLINE file_reader::impl_type &file_reader::impl_() const noexcept
    {
        JIO_Expects(m_impl != nullptr);
        return *m_impl;
    }
    JIO_INLINE mutable_buffer file_reader::read(mutable_buffer buf, error_code_ptr ec)
    {
        return impl_().read(buf, ec);
    }
    JIO_INLINE offset64_t file_reader::offset(error_code_ptr ec) const
    {
        return impl_().offset(ec);
    }
    JIO_INLINE offset64_t file_reader::seek(offset64_t off, error_code_ptr ec)
    {
        return impl_().seek(off, ec);
    }
    JIO_INLINE size64_t file_reader::size(error_code_ptr ec) const
    {
        return impl_().size(ec);
    }
    JIO_INLINE bool file_reader::is_eof(error_code_ptr ec) const
    {
        return impl_().is_eof(ec);
    }

    // file_writer

    JIO_INLINE file_writer::file_writer(const path &file, open_mode mode, error_code_ptr ec)
        : m_impl(detail::open_stream<detail::file_access::write>(file, mode, ec))
    {
    }
    JIO_INLINE file_writer::file_writer(file_writer &&) noexcept = default;
    JIO_INLINE file_writer &file_writer::operator=(file_writer &&) noexcept = default;
    JIO_INLINE file_writer::~file_writer() = default;

    JIO_INLINE file_writer::impl_type &file_writer::impl_() const noexcept
    {
        JIO_Expects(m_impl != nullptr);
        return *m_impl;
    }
    JIO_INLINE const_buffer file_writer::write(const_buffer buf, error_code_ptr ec)
    {
        return impl_().write(buf, ec);
    }
    JIO_INLINE offset64_t file_writer::offset(error_code_ptr ec) const
    {
        return impl_().offset(ec);
    }
    JIO_INLINE offset64_t file_writer::seek(offset64_t off, error_code_ptr ec)
    {
        return impl_().seek(off, ec);
    }
    JIO_INLINE size64_t file_writer::size(error_code_ptr ec) const
    {
        return impl_().size(ec);
    }
    JIO_INLINE void file_writer::sync(error_code_ptr ec)
    {
        impl_().sync(ec);
    }
    JIO_INLINE size64_t file_writer::truncate(size64_t new_size, error_code_ptr ec)
    {
        return impl_().truncate(new_size, ec);
    }

    // file_device

    JIO_INLINE file_device::file_device(const path &file, open_mode mode, error_code_ptr ec)
        : m_impl(detail::open_stream<detail::file_access::read_write>(file, mode, ec))
    {
    }
    JIO_INLINE file_device::file_device(file_device &&) noexcept = default;
    JIO_INLINE file_device &file_device::operator=(file_device &&) noexcept = default;
    JIO_INLINE file_device::~file_device() = default;

    JIO_INLINE file_device::impl_type &file_device::impl_() const noexcept
    {
        JIO_Expects(m_impl != nullptr);
        return *m_impl;
    }
    JIO_INLINE mutable_buffer file_device::read(mutable_buffer buf, error_code_ptr ec)
    {
        return impl_().read(buf, ec);
    }
    JIO_INLINE const_buffer file_device::write(const_buffer buf, error_code_ptr ec)
    {
        return impl_().write(buf, ec);
    }
    JIO_INLINE offset64_t file_device::offset(error_code_ptr ec) const
    {
        return impl_().offset(ec);
    }
    JIO_INLINE offset64_t file_device::seek(offset64_t off, error_code_ptr ec)
    {
        return impl_().seek(off, ec);
    }
    JIO_INLINE size64_t file_device::size(error_code_ptr ec) const
    {
        return impl_().size(ec);
    }
    JIO_INLINE bool file_device::is_eof(error_code_ptr ec) const
    {
        return impl_().is_eof(ec);
    }
    JIO_INLINE void file_device::sync(error_code_ptr ec)
    {
        impl_().sync(ec);
    }
    JIO_INLINE size64_t file_device::truncate(size64_t new_size, error_code_ptr ec)
    {
        return impl_().truncate(new_size, ec);
    }
} // namespace jio

#undef JIO_INLINE
