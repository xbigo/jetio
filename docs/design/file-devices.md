# File devices

Decisions for the devices over files. They are implemented one kind at a time; what is not listed as implemented is a plan.

## Kinds

Streams and maps are separate implementations, each independent of the other, composed with adaptors (a map to a stream, a stream to a map: `reader_to_view`, `writer_to_view`).

| Role | Names | Status |
| --- | --- | --- |
| Stream | `file_reader`, `file_writer`, `file_device` (reads and writes, like `memory_device`) | POSIX implemented (`file_stream.hpp`) |
| Map | `file_rd_map`, `file_wr_map`, `file_map` (`read_map`, `write_map`) | Planned |
| View | `file_rd_view`, `file_wr_view` (the objects `view_rd` and `view_wr` return, like `buffer_rd_view`) | Planned |

`*_view` stays the suffix of view objects, so the map devices are `*_map`. No `std::FILE`: it has no `pread`, no size and no `fsync` of its own.

## Interface and implementation

Each class is a handle (pimpl) on an implementation that the library keeps private. The implementation is one template over the access (read, write, read and write), so the three classes share it and a function exists only where the access allows it. The classes hold a `std::unique_ptr`, so a device is movable, not copyable, and a moved from device holds nothing (a precondition of every operation, as for a device whose constructor failed with an error code).

## Streams on POSIX

- Every read and write is `pread` or `pwrite` at the offset the device keeps, so devices of the same file do not move each other, and there is no cached size: `size` is `fstat`. `seek` only sets the offset.
- `read` returns at most 1 GiB per call, which is allowed because a read may be short. A transfer that would pass the largest offset is cut; a write at that offset is an error (`file_too_large`).
- Interrupted calls (`EINTR`) are retried, except `close`.
- The destructor closes the file as well as it can. If the constructor was given an `error_code`, a failure of the close is reported through it (and it is cleared on success, like any operation); it must outlive the device. Without one the failure is dropped.
- Creation is the constructor: `open_mode` is `existing`, `create`, `create_new` (`O_EXCL`) or `empty` (`O_TRUNC`). Files are opened with `O_CLOEXEC` and mode 0666 before the umask. Whether a file is exclusive or shared between processes is a matter of the implementation, not of the protocol.
- File offsets are 64 bit on every system: `_FILE_OFFSET_BITS=64` is defined by both targets, and the code fails to compile if `off_t` is narrower.

## Paths

The type is `jio::path`, for now `std::filesystem::path`. That is a stand-in, which does not follow the decision below. The decision:

- A path is immutable, and its content is always UTF-8. It is converted to what the operating system wants only at the call of the system API.
- A path is the only way to name a file: no function takes a string, since a string carries no encoding, which has been a source of mistakes in the past.

The system API is called only in `detail/file_stream_posix.ipp`, so replacing the type touches that file and the signatures.

## Tests

`tests/unit/file_stream.cpp` runs against the compiled (static) library in every preset. The header-only and the shared build were built and tested once, by hand; they have no preset yet.
