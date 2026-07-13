#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/io/i_file_system.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IFileSystem. Not executed; every method throws.
class FakeFileSystem final : public engine::io::IFileSystem {
public:
    engine::io::FileHandle Open(const std::string& path) override { CPPCOLDB_NOT_IMPLEMENTED(); }

    std::size_t Read(engine::io::FileHandle handle, void* buffer, std::size_t size,
                      std::uint64_t offset) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    std::size_t Write(engine::io::FileHandle handle, const void* buffer, std::size_t size,
                       std::uint64_t offset) override { CPPCOLDB_NOT_IMPLEMENTED(); }

    void Sync(engine::io::FileHandle handle) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void Close(engine::io::FileHandle handle) override { CPPCOLDB_NOT_IMPLEMENTED(); }

    bool Exists(const std::string& path) const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    std::uint64_t Size(engine::io::FileHandle handle) const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
