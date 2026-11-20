#pragma once

// Devices over files, as streams: every read and write is a system call at the offset the
// device keeps itself (pread, pwrite), so there is no shared file position and no cached size.
//   file_reader   [ sequence, random_access, forward_access ] [ reader, is_eofer, sizer ]
//   file_writer   [ sequence, random_access, forward_access ] [ writer, syncer, truncater, sizer ]
//   file_device   both of them
//
// Opening is not part of the io protocol: it is the constructor. Each class is a handle on an
// implementation that is private to the library (pimpl), so the headers of the platform are not
// needed to use it; with JIO_HEADER_ONLY, which the target jetio::jetio_header_only defines,
// the implementation is included instead of compiled.
//
// Constructor and destructor follow the error convention of error.hpp. If a constructor fails
// and `ec` is not null, the object holds no file: it can only be destroyed or assigned to. The
// `ec` of the constructor is kept: the destructor closes the file as well as it can and reports
// a failure through it, so `ec` must outlive the device. A failure of the close cannot be
// reported when `ec` is null; it is dropped. Interrupted system calls are retried, except the
// close.

#include <jio/error.hpp>
#include <jio/iocore.hpp>
#include <jio/path.hpp>
#include <jio/types.hpp>

#include <memory>

namespace jio
{
    /// What opening a file does when it exists and when it does not.
    enum class open_mode
    {
        existing,   ///< the file must exist
        create,     ///< an existing file is kept, a missing one is created
        create_new, ///< the file must not exist
        empty,      ///< an existing file is emptied, a missing one is created
    };

    namespace detail
    {
        enum class file_access
        {
            read,
            write,
            read_write
        };
        template <file_access> class file_stream;
    } // namespace detail

    /// Reads a file. Valid operations need an open device: one whose constructor did not fail
    /// and that was not moved from.
    class file_reader
    {
    public:
        /// Opens the file, which must exist, with open_mode::existing.
        explicit file_reader(const path &file, error_code_ptr ec = nullptr);
        file_reader(file_reader &&other) noexcept;
        file_reader &operator=(file_reader &&other) noexcept;
        ~file_reader();

        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr);
        offset64_t offset(error_code_ptr ec = nullptr) const;
        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr);
        size64_t size(error_code_ptr ec = nullptr) const;
        bool is_eof(error_code_ptr ec = nullptr) const;

    private:
        using impl_type = detail::file_stream<detail::file_access::read>;
        impl_type &impl_() const noexcept;
        std::unique_ptr<impl_type> m_impl;
    };

    /// Writes a file. Valid operations need an open device, see file_reader.
    class file_writer
    {
    public:
        file_writer(const path &file, open_mode mode, error_code_ptr ec = nullptr);
        file_writer(file_writer &&other) noexcept;
        file_writer &operator=(file_writer &&other) noexcept;
        ~file_writer();

        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr);
        offset64_t offset(error_code_ptr ec = nullptr) const;
        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr);
        size64_t size(error_code_ptr ec = nullptr) const;
        void sync(error_code_ptr ec = nullptr);
        /// @return the size afterwards; on an error the unchanged size
        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr);

    private:
        using impl_type = detail::file_stream<detail::file_access::write>;
        impl_type &impl_() const noexcept;
        std::unique_ptr<impl_type> m_impl;
    };

    /// Reads and writes a file. Valid operations need an open device, see file_reader.
    class file_device
    {
    public:
        file_device(const path &file, open_mode mode, error_code_ptr ec = nullptr);
        file_device(file_device &&other) noexcept;
        file_device &operator=(file_device &&other) noexcept;
        ~file_device();

        mutable_buffer read(mutable_buffer buf, error_code_ptr ec = nullptr);
        const_buffer write(const_buffer buf, error_code_ptr ec = nullptr);
        offset64_t offset(error_code_ptr ec = nullptr) const;
        offset64_t seek(offset64_t off, error_code_ptr ec = nullptr);
        size64_t size(error_code_ptr ec = nullptr) const;
        bool is_eof(error_code_ptr ec = nullptr) const;
        void sync(error_code_ptr ec = nullptr);
        /// @return the size afterwards; on an error the unchanged size
        size64_t truncate(size64_t new_size, error_code_ptr ec = nullptr);

    private:
        using impl_type = detail::file_stream<detail::file_access::read_write>;
        impl_type &impl_() const noexcept;
        std::unique_ptr<impl_type> m_impl;
    };
} // namespace jio

#ifdef JIO_HEADER_ONLY
#include <jio/detail/file_stream.ipp>
#endif
