#include "storage/wal.hpp"
#include "common/exception.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

WAL::WAL(int fd, bool read_only, std::string path)
    : fd_(fd), read_only_(read_only), path_(std::move(path)) {
    struct stat st{};
    if (fstat(fd_, &st) == 0)
        file_size_ = static_cast<size_t>(st.st_size);
}

WAL::~WAL() {
    Close();
}

std::unique_ptr<WAL> WAL::Create(const std::string& path) {
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw IOError("WAL::Create failed for path: " + path);
    return std::unique_ptr<WAL>(new WAL(fd, false, path));
}

std::unique_ptr<WAL> WAL::OpenForReplay(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) throw IOError("WAL::OpenForReplay failed for path: " + path);
    return std::unique_ptr<WAL>(new WAL(fd, true, path));
}

void WAL::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

void WAL::WriteU8 (std::vector<uint8_t>& buf, uint8_t  v) {
    buf.push_back(v);
}

void WAL::WriteU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void WAL::WriteU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8)  & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void WAL::WriteU64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void WAL::WriteI64(std::vector<uint8_t>& buf, int64_t v) {
    WriteU64(buf, static_cast<uint64_t>(v));
}

void WAL::WriteF64(std::vector<uint8_t>& buf, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    WriteU64(buf, bits);
}

void WAL::WriteStr(std::vector<uint8_t>& buf, const std::string& s) {
    WriteU16(buf, static_cast<uint16_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ---------------------------------------------------------------------------
// AppendEntry — buffer the header + payload in write_buffer_.
// ---------------------------------------------------------------------------

void WAL::AppendEntry(WALEntryType type, const std::vector<uint8_t>& payload) {
    WALEntryHeader hdr{};
    hdr.type      = static_cast<uint8_t>(type);
    hdr.data_size = static_cast<uint32_t>(payload.size());
    const uint8_t* hdr_ptr = reinterpret_cast<const uint8_t*>(&hdr);
    write_buffer_.insert(write_buffer_.end(), hdr_ptr, hdr_ptr + sizeof(hdr));
    write_buffer_.insert(write_buffer_.end(), payload.begin(), payload.end());
}

// ---------------------------------------------------------------------------
// SerializeChunk
// ---------------------------------------------------------------------------

void WAL::SerializeChunk(const DataChunk& chunk, std::vector<uint8_t>& buf) {
    WriteU32(buf, static_cast<uint32_t>(chunk.count));
    WriteU32(buf, static_cast<uint32_t>(chunk.ColumnCount()));

    for (const auto& col : chunk.columns) {
        WriteU8(buf, static_cast<uint8_t>(col.type));
        // Validity bitmap: ceil(count/8) bytes.
        size_t bitmap_bytes = (chunk.count + 7) / 8;
        for (size_t byte_i = 0; byte_i < bitmap_bytes; ++byte_i) {
            uint8_t byte_val = 0;
            for (size_t bit = 0; bit < 8 && byte_i * 8 + bit < chunk.count; ++bit) {
                if (!col.IsNull(byte_i * 8 + bit))
                    byte_val |= static_cast<uint8_t>(1 << bit);
            }
            buf.push_back(byte_val);
        }
        // Values (non-null only, in row order).
        for (size_t r = 0; r < chunk.count; ++r) {
            if (col.IsNull(r)) continue;
            switch (col.type) {
                case TypeId::BOOLEAN:
                case TypeId::INT8:
                case TypeId::INT16:
                case TypeId::INT32:
                case TypeId::INT64:
                    WriteI64(buf, col.int_data[r]);
                    break;
                case TypeId::FLOAT32:
                case TypeId::FLOAT64:
                    WriteF64(buf, col.float_data[r]);
                    break;
                case TypeId::VARCHAR:
                    WriteStr(buf, col.str_data[r]);
                    break;
                case TypeId::INVALID:
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Write operations
// ---------------------------------------------------------------------------

void WAL::WriteInsert(const std::string& schema, const std::string& table,
                      const DataChunk& chunk) {
    std::vector<uint8_t> buf;
    WriteStr(buf, schema);
    WriteStr(buf, table);
    SerializeChunk(chunk, buf);
    AppendEntry(WALEntryType::WAL_INSERT, buf);
}

void WAL::WriteDelete(const std::string& schema, const std::string& table,
                      const std::vector<row_t>& row_ids) {
    std::vector<uint8_t> buf;
    WriteStr(buf, schema);
    WriteStr(buf, table);
    WriteU32(buf, static_cast<uint32_t>(row_ids.size()));
    for (row_t r : row_ids) WriteU64(buf, r);
    AppendEntry(WALEntryType::WAL_DELETE, buf);
}

void WAL::WriteUpdate(const std::string& schema, const std::string& table,
                      const std::vector<size_t>& col_ids,
                      const std::vector<row_t>& row_ids,
                      const DataChunk& new_values) {
    std::vector<uint8_t> buf;
    WriteStr(buf, schema);
    WriteStr(buf, table);
    WriteU32(buf, static_cast<uint32_t>(col_ids.size()));
    for (size_t c : col_ids) WriteU32(buf, static_cast<uint32_t>(c));
    WriteU32(buf, static_cast<uint32_t>(row_ids.size()));
    for (row_t r : row_ids) WriteU64(buf, r);
    SerializeChunk(new_values, buf);
    AppendEntry(WALEntryType::WAL_UPDATE, buf);
}

void WAL::WriteCreateTable(const std::string& schema, const std::string& table,
                           const std::vector<std::string>& col_names,
                           const std::vector<TypeId>& col_types,
                           const PartitionInfo& partition_info) {
    std::vector<uint8_t> buf;
    WriteStr(buf, schema);
    WriteStr(buf, table);
    WriteU32(buf, static_cast<uint32_t>(col_names.size()));
    for (size_t i = 0; i < col_names.size(); ++i) {
        WriteStr(buf, col_names[i]);
        WriteU8(buf, static_cast<uint8_t>(col_types[i]));
    }
    // Partition info.
    WriteU8(buf, static_cast<uint8_t>(partition_info.type));
    if (partition_info.IsPartitioned()) {
        WriteStr(buf, partition_info.partition_col);
        WriteU32(buf, partition_info.num_partitions);
        if (partition_info.type == PartitionType::RANGE) {
            WriteU32(buf, static_cast<uint32_t>(partition_info.defs.size()));
            for (const auto& def : partition_info.defs) {
                WriteU8(buf, def.upper_bound.IsNull() ? 1u : 0u);
                if (!def.upper_bound.IsNull()) {
                    WriteU8(buf, static_cast<uint8_t>(def.upper_bound.type));
                    if (def.upper_bound.type == TypeId::VARCHAR) {
                        WriteStr(buf, def.upper_bound.GetVarchar());
                    } else if (def.upper_bound.type == TypeId::FLOAT32 ||
                               def.upper_bound.type == TypeId::FLOAT64) {
                        uint64_t bits;
                        double v = def.upper_bound.GetFloat64();
                        std::memcpy(&bits, &v, 8);
                        WriteU64(buf, bits);
                    } else {
                        WriteU64(buf, static_cast<uint64_t>(def.upper_bound.GetInt64()));
                    }
                }
            }
        } else if (partition_info.type == PartitionType::LIST) {
            WriteU32(buf, static_cast<uint32_t>(partition_info.defs.size()));
            for (const auto& def : partition_info.defs) {
                WriteU32(buf, static_cast<uint32_t>(def.list_values.size()));
                for (const auto& v : def.list_values) {
                    WriteU8(buf, static_cast<uint8_t>(v.type));
                    if (v.type == TypeId::VARCHAR) {
                        WriteStr(buf, v.GetVarchar());
                    } else if (v.type == TypeId::FLOAT32 || v.type == TypeId::FLOAT64) {
                        uint64_t bits;
                        double dv = v.GetFloat64();
                        std::memcpy(&bits, &dv, 8);
                        WriteU64(buf, bits);
                    } else {
                        WriteU64(buf, static_cast<uint64_t>(v.GetInt64()));
                    }
                }
            }
        }
        // HASH: num_partitions is sufficient; no defs to write.
    }
    AppendEntry(WALEntryType::WAL_CREATE_TABLE, buf);
}

void WAL::WriteDropTable(const std::string& schema, const std::string& table) {
    std::vector<uint8_t> buf;
    WriteStr(buf, schema);
    WriteStr(buf, table);
    AppendEntry(WALEntryType::WAL_DROP_TABLE, buf);
}

void WAL::WriteCreateSchema(const std::string& schema) {
    std::vector<uint8_t> buf;
    WriteStr(buf, schema);
    AppendEntry(WALEntryType::WAL_CREATE_SCHEMA, buf);
}

void WAL::WriteDropSchema(const std::string& schema) {
    std::vector<uint8_t> buf;
    WriteStr(buf, schema);
    AppendEntry(WALEntryType::WAL_DROP_SCHEMA, buf);
}

void WAL::WriteCheckpointMarker() {
    // Empty payload; record the file position BEFORE flushing.
    checkpoint_pos_ = file_size_ + write_buffer_.size();
    AppendEntry(WALEntryType::WAL_CHECKPOINT, {});
}

// ---------------------------------------------------------------------------
// Flush
// ---------------------------------------------------------------------------

void WAL::Flush() {
    if (write_buffer_.empty()) return;

    const uint8_t* ptr = write_buffer_.data();
    size_t remaining   = write_buffer_.size();

    while (remaining > 0) {
        ssize_t n = write(fd_, ptr, remaining);
        if (n <= 0) throw IOError("WAL::Flush: write failed");
        ptr       += n;
        remaining -= static_cast<size_t>(n);
    }

    if (fsync(fd_) != 0) throw IOError("WAL::Flush: fsync failed");

    file_size_ += write_buffer_.size();
    write_buffer_.clear();
}

// ---------------------------------------------------------------------------
// ReadNextEntry (replay)
// ---------------------------------------------------------------------------

bool WAL::ReadNextEntry(WALEntry& out) {
    WALEntryHeader hdr{};
    ssize_t n = read(fd_, &hdr, sizeof(hdr));
    if (n == 0) return false; // EOF
    if (n < static_cast<ssize_t>(sizeof(hdr))) return false; // truncated

    static constexpr size_t MAX_ENTRY = 128 * 1024 * 1024;
    if (hdr.data_size > MAX_ENTRY) throw IOError("WAL: entry too large (corrupted?)");

    out.type = static_cast<WALEntryType>(hdr.type);
    out.data.resize(hdr.data_size);

    if (hdr.data_size > 0) {
        n = read(fd_, out.data.data(), hdr.data_size);
        if (n < static_cast<ssize_t>(hdr.data_size)) return false; // truncated entry
    }
    return true;
}

// ---------------------------------------------------------------------------
// Truncate
// ---------------------------------------------------------------------------

void WAL::Truncate() {
    if (checkpoint_pos_ == 0) {
        // No checkpoint — remove everything.
        ftruncate(fd_, 0);
        lseek(fd_, 0, SEEK_SET);
        file_size_      = 0;
        checkpoint_pos_ = 0;
        return;
    }

    // Read bytes after the checkpoint marker.
    size_t keep_offset = checkpoint_pos_;
    size_t keep_size   = file_size_ - keep_offset;

    std::vector<uint8_t> tail(keep_size);
    if (keep_size > 0) {
        lseek(fd_, static_cast<off_t>(keep_offset), SEEK_SET);
        size_t remaining = keep_size;
        uint8_t* ptr = tail.data();
        while (remaining > 0) {
            ssize_t n = read(fd_, ptr, remaining);
            if (n <= 0) throw IOError("WAL::Truncate: read failed");
            ptr       += n;
            remaining -= static_cast<size_t>(n);
        }
    }

    // Rewrite the file with only the tail.
    ftruncate(fd_, 0);
    lseek(fd_, 0, SEEK_SET);

    if (keep_size > 0) {
        const uint8_t* ptr = tail.data();
        size_t remaining = keep_size;
        while (remaining > 0) {
            ssize_t n = write(fd_, ptr, remaining);
            if (n <= 0) throw IOError("WAL::Truncate: write failed");
            ptr       += n;
            remaining -= static_cast<size_t>(n);
        }
        fsync(fd_);
    }

    file_size_      = keep_size;
    checkpoint_pos_ = 0;
}

} // namespace cppcoldb
