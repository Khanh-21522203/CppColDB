#include <cstdlib>
#include <iostream>

#include "cppcoldb/database.hpp"

// REPL entrypoint stub. The real loop will:
//   1. open a Database (in-memory, or at argv[1] if given),
//   2. Connect() to get a Connection,
//   3. read SQL statements from stdin line by line,
//   4. call Connection::Query(sql) and print the QueryResult.
// Database/Connection/ClientContext bodies are all scaffold stubs today
// (CPPCOLDB_NOT_IMPLEMENTED()), so this entrypoint only prints a banner and
// exits — it must compile and link against cppcoldbv2, but it runs nothing.
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::cout << "CppColDBv2 -- scaffold REPL (query execution not implemented yet)\n";
    std::cout << "Usage: cppcoldbv2_cli [path-to-database]\n";

    return EXIT_SUCCESS;
}
