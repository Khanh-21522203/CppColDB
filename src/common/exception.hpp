#pragma once
#include <stdexcept>
#include <string>
#include <cstddef>

namespace cppcoldb {

// Base exception for all CppColDB errors.
class CppColDBException : public std::exception {
public:
    explicit CppColDBException(std::string msg)
        : message_(std::move(msg)) {}

    const char* what() const noexcept override { return message_.c_str(); }
    const std::string& Message() const { return message_; }

    virtual std::string Type() const = 0;

protected:
    std::string message_;
};

// SQL syntax error from the Tokenizer or Parser.
class ParseError : public CppColDBException {
public:
    ParseError(std::string msg, size_t pos = std::string::npos)
        : CppColDBException(std::move(msg)), position_(pos) {}

    std::string Type() const override { return "ParseError"; }

    // Byte offset in the original SQL string where the error was detected.
    // std::string::npos if position is not available.
    size_t Position() const { return position_; }

private:
    size_t position_;
};

// Semantic error from the Binder.
class BindError : public CppColDBException {
public:
    explicit BindError(std::string msg)
        : CppColDBException(std::move(msg)) {}

    std::string Type() const override { return "BindError"; }
};

// Execution-time error: constraint violation, division by zero, OOM, type cast failure.
class RuntimeError : public CppColDBException {
public:
    explicit RuntimeError(std::string msg)
        : CppColDBException(std::move(msg)) {}

    std::string Type() const override { return "RuntimeError"; }
};

// Storage / I/O error: block read/write failure, WAL write failure, corrupt file.
class IOError : public CppColDBException {
public:
    explicit IOError(std::string msg)
        : CppColDBException(std::move(msg)) {}

    std::string Type() const override { return "IOError"; }
};

// Convenience macros for common error patterns.
#define THROW_PARSE_ERROR(msg, pos)    throw ::cppcoldb::ParseError((msg), (pos))
#define THROW_BIND_ERROR(msg)          throw ::cppcoldb::BindError(msg)
#define THROW_RUNTIME_ERROR(msg)       throw ::cppcoldb::RuntimeError(msg)
#define THROW_IO_ERROR(msg)            throw ::cppcoldb::IOError(msg)

} // namespace cppcoldb
