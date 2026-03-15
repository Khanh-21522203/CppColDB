#include "transaction/undo_buffer.hpp"

namespace cppcoldb {

void UndoBuffer::ForEachForward(std::function<void(const UndoEntry&)> fn) const {
    for (const auto& e : entries_) fn(e);
}

void UndoBuffer::ForEachReverse(std::function<void(const UndoEntry&)> fn) const {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) fn(*it);
}

} // namespace cppcoldb
