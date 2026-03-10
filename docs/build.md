---
title: Build Guide
summary: Configure and build Greenflame with MSVC and Clang presets.
audience: contributors
status: authoritative
owners:
  - core-team
last_updated: 2026-03-08
tags:
  - build
  - cmake
  - msvc
  - clang
---

# Build Guide

This document is authoritative for configuring and building Greenflame.

## Prerequisites

The following MUST be available:

- Visual Studio 2026 (18.2.1 or later) with *Desktop development with C++*
- Windows 11 SDK
- CMake >= 3.26
- Ninja (recommended)

## Configure (from repo root)

```bat
cmake --preset x64-debug
```

## Build

```bat
cmake --build --preset x64-debug
```

## Output

The executable is produced at:

```bat
build\x64-debug\greenflame.exe
```

## Release build

```bat
cmake --preset x64-release
cmake --build --preset x64-release
```

## Clang build

With the Visual Studio "C++ Clang compiler for Windows" (or "Clang-cl") component installed:

```bat
cmake --preset x64-debug-clang
cmake --build --preset x64-debug-clang
```

Output: `build\x64-debug-clang\greenflame.exe`.

For release with Clang:

```bat
cmake --preset x64-release-clang
cmake --build --preset x64-release-clang
```

## Resource assets

Runtime resources embedded into `greenflame.exe` live under `resources/`.

- Keep editable source artwork alongside the derived runtime asset.
- Check in the derived runtime asset; do not add per-build image conversion steps.
- For alpha-mask toolbar glyphs and similar assets, follow [docs/resource_processing.md](resource_processing.md).

## Build Runner Reliability (Ninja + Codex runner)

When builds are launched through the Codex command runner, `pwsh.exe` can intermittently hang at 0% CPU while `cmake`/`ninja` appear idle.

- In sandboxed Codex runs, execute build and test commands sequentially
- In non-sandboxed runs, parallel builds are allowed
- Use a 20-second timeout for each sandboxed build/test command
- Prefer running build/test commands outside sandbox restrictions when possible
- If a run hangs, terminate the stuck `pwsh.exe` process and rerun

## Static analysis and include analysis (non-mandatory)

- **clang-tidy:** `compile_commands.json` is generated in the build dir (from `CMAKE_EXPORT_COMPILE_COMMANDS ON`). Use the Clang preset build dir so include paths and defines match. Example: `clang-tidy -p build\x64-debug-clang src\greenflame\win\gdi_capture.cpp` (and similarly for other sources under `src\greenflame\` and `src\greenflame_core\`).
- **Include timing:** Clang builds use `-ftime-trace`; the compiler emits `.json` trace files in the build dir. Open them in Chrome's `chrome://tracing` to inspect time spent in includes and in the compiler.
- **Include What You Use (IWYU)** can use the same `compile_commands.json` for optional include-cleanup suggestions.

## Clang Unsafe-Buffer Warning Guidance

When building with Clang (`clang-cl`), prefer C++ standard library APIs over raw libc and pointer arithmetic patterns that trigger buffer-safety warnings.

### Avoiding `-Wunsafe-buffer-usage-in-libc-call`

- `swprintf_s`: use `std::to_wstring` and string concatenation. For zero-padded integers (for example `%04u`, `%02u`), pad explicitly with `std::wstring(width - s.size(), L'0') + s`.
- `memcpy`: use `std::copy_n` or `std::copy`. For struct-to-buffer copies, use `std::copy_n(reinterpret_cast<uint8_t const *>(&obj), sizeof(obj), dest)`.
- `wcschr`: use `std::wstring_view(str).find(ch) != std::wstring_view::npos`.
- `wcslen`: use `std::wstring_view(str).size()` or `.size()` for `std::wstring`.
- `wcsrchr`: use `std::wstring_view(str).rfind(ch)` and compare to `std::wstring_view::npos`.
- `wcscmp`: use `std::wstring_view(a) == std::wstring_view(b)` for equality checks.
- `wcscpy_s` / `wcsncpy_s`: use `std::wstring::copy(dest, count)` then null-terminate (`dest[n] = L'\0'`). For fixed literals, use `std::copy_n(std::wstring_view(literal).data(), literal.size(), dest)` then null-terminate.

### Avoiding `-Wunsafe-buffer-usage` (pointer arithmetic and buffer access)

- `pixels.data() + offset` and similar pointer arithmetic: prefer `std::span` indexing (`pixels[row_offset + off]`) over raw pointer math.
- `argv[i]`: wrap as `std::span<LPWSTR>(argv, argc)` and index the span.
- `container.data() + n` for iterators: use `container.begin() + n`.
- C-style arrays with dynamic indices: prefer `std::array<T, N>` over `T arr[N]`.

## Formatting

**clang-format** is enforced automatically via a git pre-commit hook (`.githooks/pre-commit.bat`). The hook reformats any staged `.cpp`/`.h` files in place and re-stages them before the commit is recorded, so no manual formatting step is needed.

The hook is implemented as a `.bat` launcher (`.githooks/pre-commit.bat`) that invokes a PowerShell script (`.githooks/pre-commit.ps1`) via `powershell.exe`. This avoids the MSYS2 `sh.exe` signal-pipe error that occurs in some sandboxed Windows environments (such as Codex agent runners).

### Installing the hook (once per clone)

```bat
git config core.hooksPath .githooks
```

The hook requires `clang-format` to be on `PATH`; if it is not found the hook skips silently and the commit proceeds normally.

**clang-format** is also checked by CI on every push/PR (see `.github/workflows/ci.yml`). If the hook is not installed locally, CI remains the backstop.

## Lint policy

**clang-tidy** is run nightly by CI (see `.github/workflows/nightly.yml`) and is **not required** before considering a task complete. It may be run on demand against individual translation units during development, but a full tidy pass is not a completion gate.

## Completeness and correctness

- Documentation-only changes do not require rebuilds. This exception applies only
  when the task changes documentation files and does not change code, tests, build
  files, resources, or other runtime inputs.
- Code iteration can be done on the MSVC debug build only.
- However, all builds (debug and release) must be run on all compilers (MSVC and Clang) and must pass before any task is considered complete. This is a hard requirement.
