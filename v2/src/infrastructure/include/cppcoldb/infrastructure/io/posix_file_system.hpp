#pragma once
#include "cppcoldb/engine/abstractions/io/i_file_system.hpp"

namespace cppcoldb::infrastructure::io {

// POSIX adapter for IFileSystem: open/pread/pwrite/fsync/close over a real
// file descriptor.
class PosixFileSystem final : public engine::io::IFileSystem {
public:
    PosixFileSystem() = default;
    ~PosixFileSystem() override = default;

    engine::io::FileHandle Open(const std::string& path) override;
    std::size_t Read(engine::io::FileHandle handle, void* buffer, std::size_t size,
                      std::uint64_t offset) override;
    std::size_t Write(engine::io::FileHandle handle, const void* buffer, std::size_t size,
                       std::uint64_t offset) override;
    void Sync(engine::io::FileHandle handle) override;
    void Close(engine::io::FileHandle handle) override;
    bool Exists(const std::string& path) const override;
    std::uint64_t Size(engine::io::FileHandle handle) const override;
};

} // namespace cppcoldb::infrastructure::io
