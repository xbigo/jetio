#pragma once

// Algorithms on devices, written with the protocol of iocore.hpp only.

#include <jetio/iocore.hpp>

#include <cstddef>

namespace jetio
{
    /// Reads until buf is full, the data ends or an error occurs: read may return less.
    /// @return the part of buf that was filled, a prefix of buf; shorter than buf only at the
    ///         end of the data or after an error
    template <typename Device>
        requires reader<Device>
    mutable_buffer read_full(Device &&device, mutable_buffer buf, error_code_ptr ec = nullptr)
    {
        clear_error(ec);
        std::size_t done = 0;
        while (done < buf.size())
        {
            const mutable_buffer part = jetio::read(device, buf.subspan(done), ec);
            done += part.size();
            if (has_error(ec) || part.empty())
                break;
        }
        return buf.first(done);
    }
} // namespace jetio
