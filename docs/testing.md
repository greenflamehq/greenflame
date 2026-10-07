---
title: Testing Guide
summary: Test philosophy, framework, and execution for Greenflame unit tests.
audience: contributors
status: authoritative
owners:
  - core-team
last_updated: 2026-10-06
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
- `ctest` also runs two non-unit smoke tests of `greenflame_render_bench` on WARP (see [Overlay performance](#overlay-performance-opt-in)). That tool links `greenflame_render`; the rule above is about `greenflame_tests`

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
build\x64-debug\bin\greenflame_tests.exe
```

Run a subset via GoogleTest filters:

```bat
build\x64-debug\bin\greenflame_tests.exe --gtest_filter="RectPx*"
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

## Overlay performance (opt-in)

`greenflame_render_bench` drives the real overlay paint code (`Paint_d2d_frame`,
linked from `greenflame_render`) with synthetic input. It is built by default
(`GREENFLAME_BUILD_BENCH=ON`), except in the `x64-release` preset. Measure with the
`x64-release-pdb` build; debug builds turn on the D3D debug layer and their numbers
mean nothing.

```bat
cmake --preset x64-release-pdb
cmake --build --preset x64-release-pdb
build\x64-release-pdb\bin\greenflame_render_bench.exe --layout jocelyn-desk --scenario all
```

### Render bench (offscreen)

No window and no present: each frame paints into an offscreen bitmap the size of the
virtual desktop.

- `--layout`: `jocelyn-desk` (1920x1200 laptop left of two 2560x1440 panels, 7040x1440),
  `4k-desk` (same with 3840x2160 panels, 9600x2160), `single-1080`, or `current`
  (the real monitors).
- `--adapter default|N|warp`: `default` is what the app uses. `--list-adapters`
  prints the indexes.
- `--scenario hover|select|brush|highlighter|steady|all`: crosshair sweep, live
  selection drag, freehand strokes growing by one point per frame (`--step`,
  `--smooth on|off`), idle selection, or all of them.
- `--points-per-frame N` (default 1): freehand points added per frame, as coalesced
  mouse input delivers them. Larger values draw a longer stroke that wraps into rows
  and crosses itself.
- `--opacity N` (default 100): brush opacity percent. Below 100 shows double
  compositing that an opaque stroke hides.
- `--frames N` (default 300, after 5 warm-up frames), `--repeat N`, `--csv FILE`.

Per scenario it prints p50/p90/p95/p99/max and the share of frames within 33.3 ms and
16.7 ms for:

- `cpu`: QPC around the cache rebuild and `Paint_d2d_frame`;
- `gpu`: D3D11 timestamp queries (`n/a` when the GPU reports them disjoint);
- `frame`: from frame start until the GPU has finished it. The bench sets a 1 ms
  timer resolution; at the default 15.6 ms tick the wait for the GPU rounds every
  frame up to a tick multiple.

It also prints the pixels shaded per frame (`PSInvocations`, compared with the desktop
area; WARP counts only effect passes, so most scenarios read 0 there), and for freehand
the cost by stroke length. The verdict uses the `frame` p95: `PASS-60` at or under 16.7 ms, `PASS-30` at or under 33.3 ms, else `FAIL`.

The bench does not see presentation, the compositor or input coalescing. A `PASS`
here does not mean the screen keeps up; see the present probe.

### Present probe (real windows)

```bat
build\x64-release-pdb\bin\greenflame_render_bench.exe --probe-present --passes 3
```

It puts topmost windows over every monitor for a few seconds (no capture, no input).
It reports the frame interval p50/p95 and fps of steady frames for two arrangements:

- `span`: one window over the whole desktop, through the app's `Create_hwnd_rt`;
- `per-monitor`: one window and swap chain per monitor, each on the adapter that drives
  that monitor.

`--passes N` adds N-1 full-surface blits per frame.

### Comparing two builds

1. Build `x64-release-pdb` in both trees (base and change).
2. Run the same command in each, alternating A, B, A, B in one session, for example
   `--layout jocelyn-desk --scenario all --repeat 5`. Compare the
   `median of 5 frame p95s` lines. Note the power source, power plan and GPU state
   (`nvidia-smi -q -d PERFORMANCE`); numbers from different sessions do not compare.
3. Check pixels: `--scenario <s> --dump-frame 150 base.bmp` in one tree and
   `--dump-frame 150 new.bmp` in the other, then
   `--diff base.bmp new.bmp [--tolerance T]`. It exits non-zero when any channel of
   any pixel differs by more than T (default 0).
4. Run the present probe once per session.

Interactive coverage is `GF-MAN-PERF-001` in [manual_test_plan.md](manual_test_plan.md).
