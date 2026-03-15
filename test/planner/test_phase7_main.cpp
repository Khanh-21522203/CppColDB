void RunBinderTests();
void RunOptimizerTests();
void RunPhysicalPlannerTests();

int main() {
    RunBinderTests();
    RunOptimizerTests();
    RunPhysicalPlannerTests();
    return 0;
}
