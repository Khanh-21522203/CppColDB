#include "cppcoldb/sql/parser/tokenizer.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::parser {

Tokenizer::Tokenizer(std::string_view sql) : sql_(sql) {}

std::vector<Token> Tokenizer::Tokenize() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::sql::parser
