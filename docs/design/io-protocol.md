# IO protocol

A device is any type with some of the operations below, each as a member function or as a free function found by argument dependent lookup, so a custom device need not be written in namespace `jetio`; a free function takes precedence over a member, so a type can be extended or overridden from outside (a derived type can override an inherited member). Do not write an unconstrained function template with the name of an operation: it would also take over the types of this library that wrap yours. The operations of namespace `jetio` (`jetio::read`, ...) are customization point objects: they call the member or the free function, supply the default `error_code_ptr` argument and check the protocol-level contract. A concept names each capability.

The operations are objects, so they are not overloaded with functions of the same name: call them qualified (`jetio::read(device, buf)`), because `using namespace jetio` makes an unqualified `read` ambiguous where `::read` of `<unistd.h>` is declared. Code in namespace `jetio` does not name a local variable or parameter `read`, `write`, `sync`, `size`, `offset`, `seek`, ... (clang's `-Wshadow`).

| Capability | Concept | Operation |
| --- | --- | --- |
| Offset | `sequence` | `offset` |
| Seek forward | `forward_access` | `seek_forward` (falls back to `seek`) |
| Seek anywhere | `random_access` | `seek` |
| Read | `reader` | `read` |
| End of data | `is_eofer` | `is_eof` |
| Size | `sizer` | `size` |
| Zero-copy read | `read_map` | `view_rd` |
| Write | `writer` | `write` |
| Flush | `syncer` | `sync` |
| Resize | `truncater` | `truncate` |
| Zero-copy write | `write_map` | `view_wr` |
| Options | `options` | `getopt`, `setopt` |

`read_view` and `write_view` describe what `view_rd` and `view_wr` return: anything with `address()`, or a pointer to such a thing.

## Conventions

- Sizes and offsets are bytes, 64 bit (`size64_t`, `offset64_t`) on every platform. `offset64_range` is half open; its end may be `unknown_offset`, meaning up to the end.
- `error_code_ptr ec`: null means failures are thrown as `io_error`; non-null means they are stored there, and it is cleared on success.
- Seeking beyond the end is allowed. Reading there returns nothing; writing there extends the device if it can grow, and the gap reads as zero.
- A device is always left valid and usable after an error. An operation has made progress after an error only where it says so.

## Left out on purpose

- Positional operations (`read_at`, `write_at`). A device is not thread safe and carries no cost for threads; concurrent use is made of adaptors that give each user a cooperating device (see `multiplex_device`) and take care of the synchronization.
- Buffering. Devices have no buffer, so there is nothing to flush; a buffer, if needed, is an adaptor, which composes with the other adaptors and keeps the devices simple.

## Protocol-level contracts

Checked by the operations of namespace `jetio`, for every device, whether it provides them as members or as free functions. These are plain checks: debug builds only.

| Operation | Contract |
| --- | --- |
| `read` | Result is a prefix of the buffer. It may be shorter than the buffer, but it is empty only for an empty buffer, at the end of data or after an error (only the prefix is checked) |
| `write` | Result is the unwritten suffix of the buffer, not empty only after an error |
| `view_rd`, `view_wr` | Precondition `is_valid_range(rng)`; unless an error occurred, the view has `range_size(rng)` bytes when the end is known |
| `seek_forward` | Precondition (audit): target is not before the current offset |

## Devices

| Device | Header | Capabilities |
| --- | --- | --- |
| `memory_device<Represent>` | `memory.hpp` | Everything the represent allows |
| `zero_device` | `pseudo.hpp` | Reads zeros, unknown size |
| `fill_device` | `pseudo.hpp` | Reads a repeated pattern, unknown size |
| `null_device` | `pseudo.hpp` | Discards writes, size 0 |
| `empty_device` | `pseudo.hpp` | No data, offset 0 |
| `multiplex_device<Device>` | `adaptor.hpp` | Own offset over a shared random access device |
| `shift_device<Device>` | `adaptor.hpp` | Hides the first `shift` bytes of a forward access device |
| `sub_device<Device>` | `adaptor.hpp` | Window `[begin, end)` of a random access device, own offset |

### Memory represents

`vector_buffer_represent` (growable, owns), `mutable_buffer_represent` and `const_buffer_represent` (fixed size, borrowed). A custom represent has the members `data` and `pos`, or the accessors `data()`, `pos()` and `pos(size_t)`; `data` is a contiguous range of `std::byte`. Writing and truncating require a writable respectively resizable `data`.

A write to a fixed-size represent stores what fits, then reports `errc::no_space_on_device` and returns the rest. Every other memory device operation gives the strong guarantee.

### Adaptors

They refer to the wrapped device and do not own it, and provide the capabilities of the wrapped device that survive the change (their members are constrained by the concepts of the wrapped device).

- `multiplex_device`: before each operation the device is moved to the own offset, afterwards the own offset advances by what was read or written. An operation that throws leaves the own offset unchanged. `is_eof` is not `const`: it moves the wrapped device.
- `shift_device`: the offset is the one of the device less `shift`, or 0 while the device has not reached `shift`; the first read, write or `is_eof` moves the device forward to `shift`. A device shorter than `shift` has size 0, and truncating to 0 makes it `shift` bytes long. A seek, truncation or view range that does not fit with `shift` added reports `errc::value_too_large` (views return an empty view).
- `sub_device`: needs a valid window with a known end (`is_valid_range(section) && section.end != unknown_offset`) and keeps an own offset, which may lie beyond the end (nothing is read there). A write past the end stores what fits and then reports `errc::no_space_on_device`. A view range beyond the window reports `errc::invalid_argument`; an unknown end is the end of the window. `truncate` accepts only the size of the window. If the device is shorter than the window, reads return less. Windows may overlap and share a device.

The views of `shift_device` and `sub_device` are the views of the wrapped device, and an adaptor that reports an error itself returns an empty view, so the view type must be default constructible.

## Not yet migrated

The cache views of ape (`cache_rd_view`, `reader_to_view`, `cache_wr_view`, `writer_to_view`, which emulate views over readers and writers) are not part of the library yet. How `cache_wr_view` writes back is open.
