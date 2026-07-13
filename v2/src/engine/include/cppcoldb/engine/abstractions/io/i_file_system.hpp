#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace cppcoldb::engine::io {

// Opaque handle to an open file, as returned by IFileSystem::Open.
using FileHandle = int;
inline constexpr FileHandle INVALID_FILE_HANDLE = -1;

// A small POSIX-like port over file/block I/O. Adapters implement this in the
// infrastructure module (e.g. a real POSIX file system, or a fake for tests).
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // Open (creating if necessary) the file at path; returns a handle for use
    // with Read/Write/Sync/Close/Size.
    virtual FileHandle Open(const std::string& path) = 0;

    // Read size bytes from the file at the given byte offset into buffer.
    // Returns the number of bytes actually read.
    virtual std::size_t Read(FileHandle handle, void* buffer, std::size_t size,
                              std::uint64_t offset) = 0;

    // Write size bytes from buffer to the file at the given byte offset.
    // Returns the number of bytes actually written.
    virtual std::size_t Write(FileHandle handle, const void* buffer, std::size_t size,
                               std::uint64_t offset) = 0;

    // Flush the file's in-kernel buffers to stable storage (fsync).
    virtual void Sync(FileHandle handle) = 0;

    // Close the handle; it must not be used afterwards.
    virtual void Close(FileHandle handle) = 0;

    // Whether a file exists at path.
    virtual bool Exists(const std::string& path) const = 0;

    // Current size, in bytes, of the open file.
    virtual std::uint64_t Size(FileHandle handle) const = 0;
};

} // namespace cppcoldb::engine::io
