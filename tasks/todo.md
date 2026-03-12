- [x] Inspect repository state and current build configuration for README accuracy.
- [x] Create `README.md` with project overview, prerequisites, build/run/test commands, and structure.
- [x] Validate documented commands and finalize review notes.

## Review
- Added a top-level `README.md` aligned with the current scaffolded state.
- Verified commands:
  - `cmake -S . -B build && cmake --build build -j4` succeeds.
  - `./build/cppcoldb` prints `Hello, World!`.
  - `ctest --test-dir build --output-on-failure` runs and reports no registered tests yet.
