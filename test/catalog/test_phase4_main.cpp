void RunCatalogTests();
void RunTransactionTests();
void RunMVCCTests();

int main() {
    RunCatalogTests();
    RunTransactionTests();
    RunMVCCTests();
    return 0;
}
