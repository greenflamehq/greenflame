# AGENTS — Project Greenflame

This repository builds **greenflame.exe**, a Windows-only screenshot and annotation tool.

Agents interacting with this repo MUST follow the build, test, and architectural rules below.

---

## Mission

Greenflame is a high-correctness screenshot tool designed for:

- mixed-DPI, multi-monitor Windows setups
- capture-first region selection (GDI capture, then overlay to select and crop)
- optional in-place annotation
- optional CLI mode

Correctness (especially DPI and coordinates) takes priority over convenience.

Inspired by:

- Greenshot (capture and multi-monitor support)
- Flameshot (in-place annotation and CLI)
- Microsoft Snipping Tool (multi-monitor selection rules)

---

## Platform & Toolchain (authoritative)

- OS: **Windows 11**
- Compiler: **MSVC (Visual Studio 2026 (18.2.1 or later))**; **Clang (clang-cl)** is supported as an alternative when the Visual Studio "C++ Clang compiler for Windows" component is installed. MSVC remains the primary compiler.
- Build system: **CMake**
- Generator: **Ninja** (preferred) or **Visual Studio**
- Language standard: **C++20**
- IDE/editor: Cursor, Visual Studio, or CLI

---

## Repository Structure (authoritative)

- `src/greenflame/`
    Win32 GUI application (overlay, rendering, capture glue)

- `src/greenflame_core/`
    **Pure, testable core logic** (NO Win32 UI, NO rendering)
  - geometry types (`*Px`, `*Dip`)
  - coordinate conversions
  - selection rules
  - annotation and undo/redo models

- `tests/`
    Unit tests for `greenflame_core` only

---

## Build & Test Execution (MANDATORY)

Build and test commands have been moved to docs:

- [docs/build.md](docs/build.md) for configure/build/analysis/lint expectations
- [docs/testing.md](docs/testing.md) for test build/run commands, framework, and test policy

These docs are authoritative for execution steps and command lines.

Before considering a task complete:

- all required builds in [docs/build.md](docs/build.md) must pass
- all required test runs in [docs/testing.md](docs/testing.md) must pass

### Build runner reliability rule (Ninja + Codex runner)

When builds are launched through the Codex command runner, `pwsh.exe` may intermittently hang at 0% CPU while `cmake`/`ninja` appear idle.

- In **sandboxed** Codex runs, run build/test commands **sequentially**.
- In **non-sandboxed** runs, parallel builds are allowed.
- Use a **20-second timeout** for each sandboxed build/test command.
- Prefer running build/test commands **outside sandbox restrictions** when possible.
- If a run hangs, terminate the stuck **`pwsh.exe`** process and rerun.

---

## Unit Test Policy (MANDATORY)

See **[docs/testing.md](docs/testing.md)** — authoritative for philosophy, framework, what
requires tests, and how to write them.

Non-negotiables (repeated here for visibility):

- Tests MUST pass before any task is considered complete
- Do not move testable orchestration logic into the `greenflame` GUI executable
- Tests must never link against `greenflame` directly
- Tests should link against `greenflame_core` and `greenflame_logic` for unit coverage

---

## Rendering & Capture Stack (authoritative)

- **Capture:** GDI (desktop DC, CreateDIBSection, BitBlt with CAPTUREBLT) for full virtual-desktop capture. Capture happens once before the overlay is shown (capture-first pattern).
- **Overlay:** Fullscreen borderless topmost window; drawing via GDI (e.g. BitBlt of captured bitmap, then dim and selection rect). No Direct3D, DirectComposition, or Direct2D in the current path.
- **Future:** Windows Graphics Capture (WGC) may be added later for capture; overlay may remain GDI-based.

---

## DPI & Coordinate Rules (critical)

- Process is **Per-Monitor DPI Aware v2**
- Internal coordinate truth is **physical pixels**
- Explicit types MUST be used:
  - `*Px` for physical pixels
  - `*Dip` for device-independent pixels
- No implicit conversions
- DPI conversion happens exactly once, explicitly

Breaking these rules is considered a correctness bug.

---

## Notes for Agents

- **Do not use `goto`.** Use structured control flow instead (flags, loops with `break`, helper functions, RAII).
- **Indent with 4 spaces (no tabs).**
- **Prefer forward declarations over `#include` in header files.** Avoid including headers in other headers whenever possible; use forward declarations for types that are only used as pointers or references. Include the full header only in the `.cpp` that needs the definition.
- **Do not include standard library headers or Windows headers directly in non-PCH files.** If a new standard/Win32 header is needed, add it to the appropriate precompiled header (`src/greenflame/pch.h`, `src/greenflame_core/pch.h`, or `tests/pch.h`) and consume it through PCH.
- **Prefer OOP for Win32/UI-side code when ownership or lifecycle boundaries are non-trivial.** Keep API surfaces small and cohesive; separate responsibilities into focused objects instead of accumulating procedural/global state.
- Do not introduce third-party libraries without explicit justification
- Do not bypass or “simplify” DPI logic
- Do not move testable logic into the GUI executable
- Prefer failing loudly over silently doing the wrong thing
- When unsure, preserve correctness over convenience
- Keep process/CLI exit codes in a single enum with globally unique numeric values (no reuse). When codes change, update the README exit-code table in the command-line section.
- Your main instinct should be to debug issues and find problems' root causes rather than introduce new abstractions or code paths, or changing the architecture or existing code.

### Avoiding Clang `-Wunsafe-buffer-usage-in-libc-call` warnings

When building with Clang (clang-cl), prefer C++ standard library APIs over raw libc calls to avoid `-Wunsafe-buffer-usage-in-libc-call`:

- **`swprintf_s`** → Use `std::to_wstring` and string concatenation. For zero-padded integers (e.g. `%04u`, `%02u`), use a small helper that pads with `std::wstring(width - s.size(), L'0') + s`.
- **`memcpy`** → Use `std::copy_n` or `std::copy` from `<algorithm>`. For struct-to-buffer copies, use `std::copy_n(reinterpret_cast<uint8_t const *>(&obj), sizeof(obj), dest)`.
- **`wcschr`** → Use `std::wstring_view(str).find(ch) != std::wstring_view::npos` to test if a character is in a string.
- **`wcslen`** → Use `std::wstring_view(str).size()` or, when the source is `std::wstring`, `.size()`.
- **`wcsrchr`** → Use `std::wstring_view(str).rfind(ch)`; returns `std::wstring_view::npos` if not found, otherwise the position.
- **`wcscmp`** → Use `std::wstring_view(a) == std::wstring_view(b)` for equality; `std::wstring_view` compares correctly with string literals.
- **`wcscpy_s`** / **`wcsncpy_s`** → Use `std::wstring::copy(dest, count)` (returns chars copied), then `dest[n] = L'\0'`. For fixed literals, use `std::copy_n(std::wstring_view(literal).data(), literal.size(), dest)` then null-terminate.

### Avoiding Clang `-Wunsafe-buffer-usage` (pointer arithmetic and buffer access)

- **`pixels.data() + offset`** / **raw pointer arithmetic** → Use `std::span` indexing instead. Replace `uint8_t *row = pixels.data() + y * row_bytes` and `row[off]` with `size_t row_offset = y * row_bytes` and `pixels[row_offset + off]`. The span carries bounds information.
- **`argv[i]`** → Wrap in `std::span<LPWSTR>(argv, argc)` so the compiler knows the bounds, then use `argv_span[i]`.
- **`container.data() + n`** for iterators → Use `container.begin() + n` (e.g. for `std::copy_n` destination or `insert` range).
- **C-style array with dynamic index** → Use `std::array<T, N>` instead of `T arr[N]` for better bounds awareness.

