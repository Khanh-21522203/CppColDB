#include <iostream>

void RunEndToEndTests();

int main() {
    std::cout << "=== Integration End-to-End Tests ===\n";
    RunEndToEndTests();
    std::cout << "All integration tests PASSED\n";
    return 0;
}
