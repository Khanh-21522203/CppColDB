- [x] Inspect current workspace artifacts that should not be committed.
- [x] Add a `.gitignore` for CMake, build outputs, IDE files, and local metadata.
- [x] Verify ignore rules cover `build/`, `cmake-build-debug/`, `.idea/`, and `.claude/`.

## Review
- Added a project-level `.gitignore` with CMake/C++ artifact patterns and local IDE/tooling exclusions.
- Existing generated directories are now matched by ignore rules.
