# IO protocol

A device is any type with some of the member functions below. Free functions of the same name in namespace `jetio` forward to them, supply the default `error_code_ptr` argument and check the protocol-level contract. A concept names each capability.

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

## Protocol-level contracts

Checked by the free functions, for every device. These are plain checks: debug builds only.

| Operation | Contract |
| --- | --- |
| `read` | Result is a prefix of the buffer, shorter only at the end of data or after an error |
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

### Memory represents

`vector_buffer_represent` (growable, owns), `mutable_buffer_represent` and `const_buffer_represent` (fixed size, borrowed). A custom represent has the members `data` and `pos`, or the accessors `data()`, `pos()` and `pos(size_t)`; `data` is a contiguous range of `std::byte`. Writing and truncating require a writable respectively resizable `data`.

A write to a fixed-size represent stores what fits, then reports `errc::no_space_on_device` and returns the rest. Every other memory device operation gives the strong guarantee.

## Not yet migrated

The adaptors of ape (`multiplex_device`, `shift_device`, `sub_device`, and the cache views that emulate views over readers and writers) are not part of the library yet. They are to be redesigned with this contract model.
