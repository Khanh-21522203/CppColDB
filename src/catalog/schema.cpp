#include "catalog/schema.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"
#include <algorithm>

namespace cppcoldb {

bool IsCatalogEntryVisible(const CatalogEntry& entry, const Transaction& tx) {
    if (entry.create_tx_id == tx.tx_id) {
        // Own creation — fall through to delete check.
    } else {
        if (entry.create_commit_time == INVALID_TIMESTAMP) return false;
        if (entry.create_commit_time >= tx.start_time)    return false;
    }
    // check_delete:
    if (entry.delete_tx_id == tx.tx_id) return false;
    if (entry.delete_commit_time == INVALID_TIMESTAMP) return true;
    if (entry.delete_commit_time < tx.start_time) return false;
    return true;
}

Schema::Schema(std::string name) : name_(std::move(name)) {}

CatalogEntry* Schema::GetEntry(const std::string& name, const Transaction& tx,
                                OnNotFound policy) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        if (policy == OnNotFound::THROW)
            throw BindError("Entry not found in schema '" + name_ + "': " + name);
        return nullptr;
    }
    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
        if (IsCatalogEntryVisible(*(*rit), tx)) return (*rit).get();
    }
    if (policy == OnNotFound::THROW)
        throw BindError("Entry not found in schema '" + name_ + "': " + name);
    return nullptr;
}

void Schema::CreateEntry(std::unique_ptr<CatalogEntry> entry) {
    auto& vec = entries_[entry->name];
    vec.push_back(std::move(entry));
}

void Schema::MarkDeleted(const std::string& name, const Transaction& tx) {
    auto it = entries_.find(name);
    if (it == entries_.end()) return;
    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
        if (IsCatalogEntryVisible(*(*rit), tx)) {
            (*rit)->delete_tx_id       = tx.tx_id;
            (*rit)->delete_commit_time = INVALID_TIMESTAMP;
            return;
        }
    }
}

void Schema::CommitEntry(const std::string& name, TransactionId tx_id,
                          timestamp_t commit_time) {
    auto it = entries_.find(name);
    if (it == entries_.end()) return;
    for (auto& entry : it->second) {
        if (entry->create_tx_id == tx_id &&
            entry->create_commit_time == INVALID_TIMESTAMP) {
            entry->create_commit_time = commit_time;
        }
        // Only commit a delete marker if the tx actually performed a DROP.
        // Guard against INVALID_TRANSACTION (used by Deserialize for creates).
        if (tx_id != INVALID_TRANSACTION &&
            entry->delete_tx_id == tx_id &&
            entry->delete_commit_time == INVALID_TIMESTAMP) {
            entry->delete_commit_time = commit_time;
        }
    }
}

void Schema::RollbackCreate(const std::string& name, TransactionId tx_id) {
    auto it = entries_.find(name);
    if (it == entries_.end()) return;
    auto& vec = it->second;
    vec.erase(
        std::remove_if(vec.begin(), vec.end(),
            [tx_id](const std::unique_ptr<CatalogEntry>& e) {
                return e->create_tx_id == tx_id &&
                       e->create_commit_time == INVALID_TIMESTAMP;
            }),
        vec.end());
}

void Schema::RollbackDrop(const std::string& name, TransactionId tx_id) {
    auto it = entries_.find(name);
    if (it == entries_.end()) return;
    for (auto& entry : it->second) {
        if (entry->delete_tx_id == tx_id &&
            entry->delete_commit_time == INVALID_TIMESTAMP) {
            entry->delete_tx_id       = INVALID_TRANSACTION;
            entry->delete_commit_time = INVALID_TIMESTAMP;
        }
    }
}

} // namespace cppcoldb
