void RunTableScanTests();
void RunFilterTests();
void RunProjectionTests();
void RunLimitTests();

int main() {
    RunTableScanTests();
    RunFilterTests();
    RunProjectionTests();
    RunLimitTests();
    return 0;
}
