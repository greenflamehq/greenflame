# AGENTS - Project Greenflame

This repository builds `greenflame.exe`, a Windows-only screenshot and annotation tool.

Agents MUST follow this file and the authoritative docs linked below.

---

## Mission

Greenflame is a high-correctness screenshot tool designed for:

- mixed-DPI, multi-monitor Windows setups
- capture-first region selection (capture, then overlay selection)
- optional in-place annotation
- optional CLI mode

Correctness, especially DPI and coordinate behavior, takes priority over convenience.

---

## Authoritative Docs

- [docs/build.md](docs/build.md): configure/build/lint/static-analysis expectations
- [docs/testing.md](docs/testing.md): test philosophy, build/run commands, and policy
- [README.md](README.md): user-facing behavior, CLI semantics, and exit code table

Before considering any task complete:

- all required builds in `docs/build.md` must pass
- all required test runs in `docs/testing.md` must pass

---

## Platform & Toolchain (authoritative)

- OS: **Windows 11**
- Compiler: **MSVC (Visual Studio 2026 (18.2.1 or later))** (primary), **Clang (clang-cl)** supported
- Build system: **CMake**
- Generator: **Ninja** (preferred) or **Visual Studio**
- Language standard: **C++20**

---

## Repository Structure (authoritative)

- `src/greenflame/`
  Win32 application shell and OS integration
  - `main.cpp`: process entry, CLI parse handoff, single-instance tray guard
  - `greenflame_app.*`: app lifecycle, message loop, wiring between UI events and controllers
  - `win/`: Win32 adapter layer (window classes, Direct2D overlay UI, tray UI, GDI capture, file dialogs/startup, concrete service implementations)

- `src/greenflame_core/`
  Testable core logic and controller policies
  - geometry/rect and monitor rules
  - selection and overlay interaction state machine
  - output path and save policy logic
  - CLI/application orchestration via injected service interfaces

- `tests/`
  GoogleTest unit coverage for `greenflame_core` behavior using mocks/fakes for service interfaces

- `docs/`
  Authoritative build and testing instructions, plus focused design notes

---

## Architecture Notes

Greenflame is controller-centric rather than strict MVC.

- `greenflame_core::AppController` owns capture/save/CLI orchestration and policy decisions.
- `greenflame_core::OverlayController` owns overlay interaction state and action decisions.
- `src/greenflame/win/` owns Win32-specific adapters and concrete service implementations.
- `OverlayWindow` and `TrayWindow` translate Win32 events into controller calls and apply returned actions.
- Keep as much behavior as possible in core; keep OS/UI code in `greenflame`.
- Prefer OOP and encapsulation with clear ownership/lifecycle boundaries over global/procedural state.

---

## Rendering, DPI, and Coordinate Rules (critical)

- Process is **Per-Monitor DPI Aware v2**
- Internal coordinate truth is **physical pixels**
- Explicit types MUST be used:
  - `*Px` for physical pixels
  - `*Dip` for device-independent pixels
- No implicit coordinate conversions
- DPI conversion must happen explicitly and in one place per boundary crossing

Capture/overlay stack (current):

- Capture: GDI virtual-desktop capture before overlay display (capture-first)
- Overlay rendering: Direct2D/DirectWrite in a fullscreen borderless topmost window
- Tray toast rendering: GDI/GDI+ (intentional small Win32 UI path)
- No Direct3D/DirectComposition in the current path

Breaking these rules is a correctness bug.

---

## Coding Rules for Agents

- Do not use `goto`.
- Indent with 4 spaces (no tabs).
- Prefer forward declarations in headers when possible; include full definitions in `.cpp`.
- Do not include standard library or Windows headers directly in non-PCH files. Add needed headers to the relevant PCH (`src/greenflame/pch.h`, `src/greenflame_core/pch.h`, or `tests/pch.h`).
- Do not introduce third-party libraries without explicit justification.
- Do not bypass or simplify DPI logic.
- Do not move testable orchestration/policy logic into the GUI executable.
- Any new functionality or feature MUST either add automated test coverage or add explicit coverage to the documented testing plan under `docs/`, following `docs/testing.md`, if the behavior cannot reasonably be covered automatically.
- Any bug fix MUST either add automated regression coverage for the faulty behavior or add explicit regression coverage to the documented testing plan under `docs/`, following `docs/testing.md`, if the behavior cannot reasonably be covered automatically.
- Prefer failing loudly over silently doing the wrong thing.
- When unsure, preserve correctness over convenience.
- Keep process/CLI exit codes in a single enum with globally unique numeric values. If codes change, update the README exit-code table.
- Debug root causes first; avoid adding new abstractions or code paths without clear need.
- Clang warning hygiene guidance lives in `docs/build.md`.
