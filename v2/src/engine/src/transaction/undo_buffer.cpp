#include "cppcoldb/engine/transaction/undo_buffer.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::transaction {

void UndoBuffer::ForEachForward(const std::function<void(const UndoEntry&)>& /*fn*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void UndoBuffer::ForEachReverse(const std::function<void(const UndoEntry&)>& /*fn*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::transaction
