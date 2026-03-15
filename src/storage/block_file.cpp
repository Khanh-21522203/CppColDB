#include "storage/block_file.hpp"
#include "common/exception.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <vector>

namespace cppcoldb {

BlockFile::BlockFile(const std::string& path, size_t block_size)
    : block_size_(block_size), block_count_(0) {
    fd_ = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0)
        throw IOError("BlockFile: cannot open '" + path + "'");

    struct stat st{};
    if (fstat(fd_, &st) < 0)
        throw IOError("BlockFile: fstat failed");

    block_count_ = static_cast<size_t>(st.st_size) / block_size_;
}

BlockFile::~BlockFile() {
    if (fd_ >= 0) close(fd_);
}

void BlockFile::ReadBlock(BlockId id, uint8_t* buffer) {
    if (id >= block_count_)
        throw IOError("BlockFile: read on invalid block " + std::to_string(id));

    off_t offset = static_cast<off_t>(id) * static_cast<off_t>(block_size_);
    size_t remaining = block_size_;
    uint8_t* ptr = buffer;

    while (remaining > 0) {
        ssize_t n = pread(fd_, ptr, remaining, offset);
        if (n <= 0)
            throw IOError("BlockFile: read failed for block " + std::to_string(id));
        ptr       += n;
        offset    += n;
        remaining -= static_cast<size_t>(n);
    }
}

void BlockFile::WriteBlock(BlockId id, const uint8_t* buffer) {
    off_t offset = static_cast<off_t>(id) * static_cast<off_t>(block_size_);
    size_t remaining = block_size_;
    const uint8_t* ptr = buffer;

    while (remaining > 0) {
        ssize_t n = pwrite(fd_, ptr, remaining, offset);
        if (n <= 0)
            throw IOError("BlockFile: write failed for block " + std::to_string(id));
        ptr       += n;
        offset    += n;
        remaining -= static_cast<size_t>(n);
    }
}

void BlockFile::Sync() {
    fsync(fd_);
}

BlockId BlockFile::AllocateBlock() {
    BlockId id = static_cast<BlockId>(block_count_++);
    // Extend file by writing a zero block at the new position.
    std::vector<uint8_t> zeros(block_size_, 0);
    off_t offset = static_cast<off_t>(id) * static_cast<off_t>(block_size_);
    size_t remaining = block_size_;
    const uint8_t* ptr = zeros.data();

    while (remaining > 0) {
        ssize_t n = pwrite(fd_, ptr, remaining, offset);
        if (n <= 0)
            throw IOError("BlockFile: AllocateBlock failed for block " + std::to_string(id));
        ptr       += n;
        offset    += n;
        remaining -= static_cast<size_t>(n);
    }
    return id;
}

} // namespace cppcoldb
