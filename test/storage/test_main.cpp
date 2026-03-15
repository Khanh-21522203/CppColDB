void RunBlockFileTests();
void RunBufferManagerTests();
void RunWALTests();
void RunCompressionTests();

int main() {
    RunBlockFileTests();
    RunBufferManagerTests();
    RunWALTests();
    RunCompressionTests();
    return 0;
}
