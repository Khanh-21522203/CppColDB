#pragma once

namespace cppcoldb {

class Catalog;
class Transaction;

struct ClientContext {
    Catalog*     catalog     = nullptr;
    Transaction* transaction = nullptr;
};

} // namespace cppcoldb
