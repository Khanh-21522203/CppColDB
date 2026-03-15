#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "common/types.hpp"

namespace cppcoldb {

enum class WALEntryType : uint8_t {
    WAL_CREATE_TABLE  = 1,
    WAL_DROP_TABLE    = 2,
    WAL_INSERT        = 3,
    WAL_DELETE        = 4,
    WAL_UPDATE        = 5,
    WAL_CREATE_SCHEMA = 6,
    WAL_DROP_SCHEMA   = 7,
    WAL_CHECKPOINT    = 8,
};

// Fixed 5-byte entry header (packed to avoid padding).
#pragma pack(push, 1)
struct WALEntryHeader {
    uint8_t  type;      // WALEntryType
    uint32_t data_size; // bytes of payload after header
};
#pragma pack(pop)
static_assert(sizeof(WALEntryHeader) == 5, "WALEntryHeader size changed");

// One decoded WAL entry (used during replay).
struct WALEntry {
    WALEntryType         type;
    std::vector<uint8_t> data; // raw payload
};

class WAL {
public:
    // Create a new WAL file (truncates if exists).
    static std::unique_ptr<WAL> Create(const std::string& path);

    // Open an existing WAL file for sequential replay.
    static std::unique_ptr<WAL> OpenForReplay(const std::string& path);

    ~WAL();

    // Append operations (buffered in memory until Flush()).
    void WriteInsert(const std::string& schema, const std::string& table,
                     const DataChunk& chunk);
    void WriteDelete(const std::string& schema, const std::string& table,
                     const std::vector<row_t>& row_ids);
    void WriteUpdate(const std::string& schema, const std::string& table,
                     const std::vector<size_t>& col_ids,
                     const std::vector<row_t>& row_ids,
                     const DataChunk& new_values);
    void WriteCreateTable(const std::string& schema, const std::string& table,
                          const std::vector<std::string>& col_names,
                          const std::vector<TypeId>& col_types);
    void WriteDropTable(const std::string& schema, const std::string& table);
    void WriteCreateSchema(const std::string& schema);
    void WriteDropSchema(const std::string& schema);
    void WriteCheckpointMarker();

    // Flush write buffer to disk + fsync. Throws IOError on failure.
    void Flush();

    // Replay: read next entry from file. Returns false at EOF / truncated entry.
    bool ReadNextEntry(WALEntry& out);

    // Remove all WAL entries before the last checkpoint marker.
    void Truncate();

    void Close();

    bool   IsEmpty()        const { return file_size_ == 0; }
    size_t FileSizeBytes()  const { return file_size_; }

private:
    explicit WAL(int fd, bool read_only, std::string path);

    void AppendEntry(WALEntryType type, const std::vector<uint8_t>& payload);

    // Serialization helpers (append to buf).
    static void WriteU8 (std::vector<uint8_t>& buf, uint8_t  v);
    static void WriteU16(std::vector<uint8_t>& buf, uint16_t v);
    static void WriteU32(std::vector<uint8_t>& buf, uint32_t v);
    static void WriteU64(std::vector<uint8_t>& buf, uint64_t v);
    static void WriteI64(std::vector<uint8_t>& buf, int64_t  v);
    static void WriteF64(std::vector<uint8_t>& buf, double   v);
    static void WriteStr(std::vector<uint8_t>& buf, const std::string& s);

    static void SerializeChunk(const DataChunk& chunk, std::vector<uint8_t>& buf);

    int                  fd_;
    bool                 read_only_;
    std::string          path_;
    std::vector<uint8_t> write_buffer_;
    size_t               file_size_       = 0;
    size_t               checkpoint_pos_  = 0;
};

} // namespace cppcoldb
