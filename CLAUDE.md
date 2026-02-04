@~/.claude/rules/cpp.md

# Building

- Use CMake with preset "clang-debug" for building and testing on Linux with Clang in debug mode.
- Use CMake with preset "clang-release" for performance testing on Linux with Clang in release mode.

# Testing

- Run the tests using `ctest --preset=clang-debug` or `ctest --preset=clang-release` depending on the build type.
