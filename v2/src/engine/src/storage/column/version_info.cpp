#include "cppcoldb/engine/storage/column/version_info.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::storage {

void VersionInfo::MarkDeleted(std::uint32_t /*row_offset*/, common::TransactionId /*tx_id*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::CommitDelete(std::uint32_t /*row_offset*/, common::Timestamp /*commit_time*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::ClearDeleteMarker(std::uint32_t /*row_offset*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::MarkInserted(std::uint32_t /*row_offset*/, common::TransactionId /*tx_id*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::CommitInsert(std::uint32_t /*row_offset*/, common::Timestamp /*commit_time*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::RevertInsert(std::uint32_t /*row_offset*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::MarkUpdated(std::uint32_t /*row_offset*/, common::TransactionId /*tx_id*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::CommitUpdate(std::uint32_t /*row_offset*/, common::Timestamp /*commit_time*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void VersionInfo::RevertUpdate(std::uint32_t /*row_offset*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

bool VersionInfo::IsVisible(std::uint32_t /*row_offset*/,
                             const cppcoldb::engine::transaction::Transaction& /*tx*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

bool VersionInfo::HasUncommittedMarkers() const { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::storage
