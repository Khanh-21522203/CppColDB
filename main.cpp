#include <iostream>
#include <string>
#include "main/database.hpp"
#include "main/connection.hpp"
#include "execution/executor.hpp"

int main(int argc, char* argv[]) {
    std::string path = (argc > 1) ? argv[1] : ":memory:";
    std::cout << "CppColDB — path: " << path << "\n";
    std::cout << "Type SQL statements (or 'exit' to quit).\n\n";

    cppcoldb::Database db(path);
    auto conn = db.Connect();

    std::string line;
    while (true) {
        std::cout << "cppcoldb> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "exit" || line == "quit" || line == "\\q") break;

        auto result = conn->Query(line);

        if (!result.success) {
            std::cerr << "Error: " << result.error_message << "\n";
            continue;
        }

        // Print column headers.
        for (size_t i = 0; i < result.column_names.size(); ++i) {
            if (i) std::cout << " | ";
            std::cout << result.column_names[i];
        }
        if (!result.column_names.empty()) std::cout << "\n";

        // Print rows.
        size_t total_rows = 0;
        for (const auto& chunk : result.chunks) {
            for (size_t r = 0; r < chunk.count; ++r) {
                for (size_t c = 0; c < chunk.columns.size(); ++c) {
                    if (c) std::cout << " | ";
                    const auto& col = chunk.columns[c];
                    if (col.IsNull(r)) {
                        std::cout << "NULL";
                    } else {
                        switch (col.type) {
                            case cppcoldb::TypeId::BOOLEAN:
                                std::cout << (col.int_data[r] ? "true" : "false"); break;
                            case cppcoldb::TypeId::INT8:
                            case cppcoldb::TypeId::INT16:
                            case cppcoldb::TypeId::INT32:
                            case cppcoldb::TypeId::INT64:
                                std::cout << col.int_data[r]; break;
                            case cppcoldb::TypeId::FLOAT32:
                            case cppcoldb::TypeId::FLOAT64:
                                std::cout << col.float_data[r]; break;
                            case cppcoldb::TypeId::VARCHAR:
                                std::cout << col.str_data[r]; break;
                            default:
                                std::cout << "?"; break;
                        }
                    }
                }
                std::cout << "\n";
                ++total_rows;
            }
        }
        std::cout << "(" << total_rows << " row" << (total_rows == 1 ? "" : "s") << ")\n\n";
    }

    return 0;
}
