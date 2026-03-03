---
title: Testing Guide
summary: Test philosophy, framework, and execution for Greenflame unit tests.
audience: contributors
status: authoritative
owners:
  - core-team
last_updated: 2026-03-03
tags:
  - tests
  - ctest
  - gtest
---

# Testing Guide

This document is authoritative for building and running tests in Greenflame.

## Unit test philosophy

The test suite serves as **behavioral documentation**: it captures what the system is designed
to do so that future refactors have a solid, verifiable foundation. Tests are written for
behaviors that have been manually verified and consciously decided to be correct.

- The GUI executable (`greenflame`) must remain a thin Win32 shell — testable logic must not live there
- Tests MUST NOT depend on Win32 UI, GDI capture, or WGC
- Win32 service calls must be abstracted behind mockable service interfaces; mocking is the
  approved way to test orchestration logic that would otherwise require real Win32 APIs
- Tests are required for:
  - geometry and math
  - DPI and coordinate conversions
  - cross-monitor selection rules
  - string utilities, window filtering, output path logic
  - app-level orchestration: capture flows, last-capture state, CLI mode, output path resolution

## Build tests

Tests are enabled via CMake's standard `BUILD_TESTING` option (ON by default).

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

## Run tests

```bat
ctest --test-dir build\x64-debug
```

If using a multi-config generator (Visual Studio):

```bat
ctest --test-dir build\x64-debug -C Debug
```

For Clang debug builds:

```bat
ctest --test-dir build\x64-debug-clang
```

For Codex sandboxed command-runner sessions, follow the Build Runner Reliability policy in [docs/build.md](build.md).

Test must be run and must pass before any task is considered complete. This is a hard requirement.

## GoogleTest integration (authoritative)

- Test framework: **GoogleTest**
- Integration method: **CMake FetchContent** in `tests/CMakeLists.txt` (tag v1.14.0). First configure (or when the FetchContent cache is missing) **requires network**; request network permission for that configure step in sandboxed/CI environments.
- No global install and no vcpkg requirement
- The test binary is: `greenflame_tests`
- Link targets: `GTest::gtest_main`, `GTest::gmock` (gmock ships with the same FetchContent pull)

## Where to add tests

- Add new test files under `tests/`
- Register them in `tests/CMakeLists.txt` as sources of `greenflame_tests`
- Tests must only link against `greenflame_core` and the testable logic library — never against `greenflame` directly

## Writing a test

Use GoogleTest macros for plain logic tests:

```cpp
TEST(Suite, Name)
{
    EXPECT_EQ(1 + 1, 2);
}
```

Use GoogleMock for tests that require injected service dependencies.
`gmock/gmock.h` must be added to `tests/pch.h` (not included directly in source files):

```cpp
// In tests/pch.h: #include <gmock/gmock.h>

class MockDisplayQueries : public IDisplayQueries {
  public:
    MOCK_METHOD(core::RectPx, Get_virtual_desktop_bounds_px, (), (const, override));
    // ...
};

TEST(AppControllerTest, CopiesDesktopBounds)
{
    MockDisplayQueries display;
    EXPECT_CALL(display, Get_virtual_desktop_bounds_px())
        .WillOnce(testing::Return(core::RectPx::From_ltrb(0, 0, 1920, 1080)));
    // ...
}
```

## Running tests directly

You can run the test executable (useful for filters):

```bat
build\x64-debug\greenflame_tests.exe
```

Run a subset via GoogleTest filters:

```bat
build\x64-debug\greenflame_tests.exe --gtest_filter="RectPx*"
```

Prefer `ctest` for standard runs; use direct execution for local filtering.
