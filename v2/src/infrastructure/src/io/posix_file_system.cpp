#include "cppcoldb/infrastructure/io/posix_file_system.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::infrastructure::io {

engine::io::FileHandle PosixFileSystem::Open(const std::string& path) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t PosixFileSystem::Read(engine::io::FileHandle handle, void* buffer, std::size_t size,
                                   std::uint64_t offset) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t PosixFileSystem::Write(engine::io::FileHandle handle, const void* buffer,
                                    std::size_t size, std::uint64_t offset) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void PosixFileSystem::Sync(engine::io::FileHandle handle) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void PosixFileSystem::Close(engine::io::FileHandle handle) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

bool PosixFileSystem::Exists(const std::string& path) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::uint64_t PosixFileSystem::Size(engine::io::FileHandle handle) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::infrastructure::io
