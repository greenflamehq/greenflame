---
title: Testing Guide
summary: Test philosophy, framework, and execution for Greenflame unit tests.
audience: contributors
status: authoritative
owners:
  - core-team
last_updated: 2026-03-14
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

## Source coverage

LLVM-based source coverage for `greenflame_core` can be generated with:

```powershell
.\scripts\coverage.ps1
```

See [docs/coverage.md](coverage.md) for prerequisites and details.

## Manual verification coverage

Some Win32 overlay behaviors cannot be exercised in the unit-test binary because `greenflame_tests`
must not depend on the GUI executable. For those cases, add or update the detailed case coverage in
[manual_test_plan.md](manual_test_plan.md) and run the applicable cases when the affected feature changes.

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

## Freehand CPU performance check (opt-in)

The disabled `freehand_smoothing.LongStrokePerformance` test times the production
smoother on a fixed 8,192-point stroke. It reports microseconds per call and output
point count, with no machine-dependent timing assertion:

```bat
build\x64-release\bin\greenflame_tests.exe --gtest_filter=freehand_smoothing.DISABLED_LongStrokePerformance --gtest_also_run_disabled_tests --gtest_repeat=5
```

Compare the median of repeated optimized runs on the same machine, after builds
finish. This measures CPU smoothing cost only, not input-to-display latency or GPU
frame pacing. Interactive coverage is `GF-MAN-ANN-002C` in
[manual_test_plan.md](manual_test_plan.md).
