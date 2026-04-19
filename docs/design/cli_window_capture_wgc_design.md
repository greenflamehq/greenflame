---
title: CLI WGC Window Capture Design
summary: Current contributor reference for the shipped WGC window-capture backend, including controller fallback rules and the Win32 capture pipeline.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - cli
  - window
  - capture
  - wgc
---

# CLI WGC Window Capture Design

This document describes the current shipped implementation of the CLI WGC window
capture path.

It replaces the earlier proposal-style version of this file. The implementation,
tests, and higher-priority reference docs are authoritative when this document is
updated.

For the public CLI contract, see [cli_window_capture.md](cli_window_capture.md)
and [README.md](../../README.md).

## Current Status

The `--window-capture auto|gdi|wgc` feature is already shipped.

Current facts:

- the backend selector is CLI-only
- the option applies only to `--window` and `--window-hwnd`
- `auto` is the current default and prefers WGC
- `gdi` preserves the visible-desktop crop path
- `wgc` forces the dedicated WGC capture path and does not fall back

This is no longer a design handoff. The parser, controller policy, request
model, WGC capture implementation, fallback behavior, and coverage already
exist.

## Request Model

The shipped implementation no longer treats window capture as a rect-only
special case.

Current core request types:

- `WindowCaptureBackend`
  - `Auto`
  - `Gdi`
  - `Wgc`
- `CaptureSourceKind`
  - `ScreenRect`
  - `Window`

Current `CaptureSaveRequest` carries the fields needed by both backends:

- `source_kind`
- `window_capture_backend`
- `source_rect_screen`
- `source_window`
- `padding_px`
- `fill_color`
- `include_cursor`
- `preserve_source_extent`
- `annotations`

This allows the controller to keep a shared output pipeline while still giving
the Win32 layer enough information to distinguish:

- rect-based GDI capture
- HWND-targeted WGC window capture

## Parser And Validation

Current parser behavior in `cli_options.cpp`:

- `--window-capture` accepts `auto`, `gdi`, or `wgc`
- parsing is case-insensitive
- duplicate uses are rejected
- the option requires `--window` or `--window-hwnd`
- the default remains `WindowCaptureBackend::Auto`

The parser does not try to choose policy beyond this. Backend selection and
fallback rules remain controller concerns.

## Controller Flow

`AppController::Run_cli_capture_mode(...)` owns backend orchestration.

Current high-level order for window capture is:

1. resolve the target window by title or `HWND`
2. reject unavailable, minimized, or uncapturable windows before backend save
3. resolve the canonical window rect
4. collect window-obscuration state and virtual-desktop bounds
5. load prepared annotations
6. resolve and reserve the output path
7. build `CaptureSaveRequest`
8. dispatch by backend mode

### Shared pre-backend rejection

Current pre-backend hard failures include:

- invalid or disappeared window
- minimized window
- uncapturable window with `WDA_EXCLUDEFROMCAPTURE`

These are intentionally backend-independent.

### Title-based minimized-match handling

The controller uses a backend-sensitive title-match rule:

- `Uses_wgc_title_match_handling(...)` returns true for `auto` and `wgc`
- it returns false for `gdi`

Current behavior for `auto` and `wgc` title queries:

- if no visible window matches remain, the controller counts minimized title
  matches and may return exit code `13` with a minimized-specific message
- if one visible match is selected while additional matches are minimized, the
  controller emits a warning that the minimized matches were skipped

Current behavior for `gdi` title queries:

- the older visible-window-only behavior remains
- minimized matches do not alter the "no visible window matches" result

## Backend Dispatch

### Forced `gdi`

The controller goes directly to the GDI save path and preserves the existing GDI
warning model:

- fully obscured warning
- partially obscured warning
- partially outside visible desktop warning
- padding fill warning when needed

### Forced `wgc`

The controller sends a window-scoped `CaptureSaveRequest` to the capture
service:

- `source_kind = Window`
- `window_capture_backend = Wgc`
- `source_window = resolved HWND`
- `source_rect_screen = resolved window rect`

If the save service returns `CaptureSaveStatus::BackendFailed`, the controller
maps that failure to `CliWindowCaptureBackendFailed` (exit code `15`).

There is no GDI fallback in forced `wgc` mode.

### `auto`

`auto` is implemented as "try WGC first, then maybe fall back to GDI."

Current controller behavior:

1. attempt `Save_capture_to_file(...)` with backend `Wgc`
2. if status is `Success`, finish successfully
3. if status is `SaveFailed`, do not fall back; return the normal capture/save
   failure path
4. if status is `BackendFailed`, emit:

```text
Info: WGC window capture failed; falling back to GDI.
```

5. if the window rect has no capturable intersection with the virtual desktop,
   return the outside-the-virtual-desktop error instead of attempting GDI
6. otherwise, run the normal GDI save path and warnings

This means only backend failures trigger fallback. Later save failures do not.

It also means the original WGC backend error text is intentionally not surfaced
in `auto`; the user sees the fallback info line and then either the GDI result
or the GDI-precheck failure.

## Win32 Capture-Service Boundary

`Win32CaptureService::Save_capture_to_file(...)` is the main backend dispatch
point in the Win32 layer.

Current WGC branch condition:

- `request.source_kind == CaptureSourceKind::Window`
- `request.window_capture_backend == WindowCaptureBackend::Wgc`

When that branch is taken, the service:

1. validates that `source_window` is non-null
2. calls `Capture_window_with_wgc(...)`
3. optionally captures a cursor snapshot through Greenflame's own cursor path
4. passes the captured bitmap into `Save_exact_source_capture_to_file(...)`

This is important because WGC does not fork the rest of the save pipeline.
Padding, cursor composition, annotation rendering, and file encoding still flow
through the same downstream save helper.

All non-WGC window cases continue through the GDI/virtual-desktop path.

## WGC Capture Pipeline

The shipped WGC implementation lives in `wgc_window_capture.cpp`.

Current high-level flow in `Capture_window_with_wgc(...)`:

1. validate the target `HWND` and normalized window rect
2. ensure the WGC capture-thread apartment exists
3. ensure WGC support is available for the process and system
4. create a D3D11 device and context
5. bridge that device into a WinRT `IDirect3DDevice`
6. create a `GraphicsCaptureItem` for the target `HWND`
7. read the capture-item size and validate it
8. create a free-threaded frame pool
9. create a capture session
10. disable WGC-native cursor capture with `IsCursorCaptureEnabled(false)`
11. start capture and wait for the first frame
12. convert the returned frame into the GDI bitmap shape used by the rest of the
    save pipeline
13. require the returned frame size to match the resolved window rect exactly

If any of these steps fail, the function returns `CaptureSaveStatus::BackendFailed`
with a concrete error message.

Representative failure messages include:

- unsupported WGC on the current system
- timeout waiting for the first frame
- failure while acquiring or converting the frame
- returned frame-size mismatch versus the resolved window rect

## Padding, Cursor, And Annotation Semantics

Because successful WGC capture feeds into `Save_exact_source_capture_to_file(...)`,
the downstream composition rules are shared with other output paths.

### Padding

Current WGC padding behavior:

- the source image is the full window image returned by WGC
- padding remains synthetic outer canvas only
- partially off-screen WGC success does not rely on synthetic fill inside the
  source image the way the GDI preserved-source-extent path can

### Cursor

The current WGC path intentionally disables WGC-native cursor capture.

If `include_cursor` is enabled:

- Greenflame captures its own cursor snapshot after the WGC image is acquired
- that snapshot is composited into the source bitmap
- annotation rendering still happens later

This preserves one consistent cursor-composition path and avoids duplicate
cursor rendering from WGC itself.

### Annotations

Current annotation behavior is unchanged by backend choice:

- local coordinates remain relative to the top-left of the window image
- global coordinates remain relative to the resolved screen rect
- annotations render after capture, cursor composition, and padding

## Current Policy Decisions

Several decisions that were open in the proposal are now settled in code.

### `auto` currently prefers WGC unconditionally

The shipped `auto` behavior always tries WGC first. There is no extra runtime
policy gate beyond support and backend success/failure.

### Minimized windows remain unsupported

Minimized windows are still rejected before backend save work. WGC is not used
to capture minimized windows.

### GDI warning text remains backend-specific

The current obscuration and off-desktop warning text remains part of the GDI
path. Successful WGC captures do not emit that warning model.

### Yellow-border observations are no longer a design blocker

The old proposal treated the possible WGC capture border as unresolved. The
shipped implementation no longer blocks `auto` on that question.

Manual coverage still records whether any yellow border appears on screen or in
saved output, but the controller does not gate backend choice on that result.

## Coverage

Current automated coverage includes:

- `cli_options_tests.cpp`
  - backend parsing
  - duplicate rejection
  - invalid-value rejection
  - source requirement enforcement
- `app_controller_tests.cpp`
  - `auto` defaulting to WGC
  - explicit `gdi` warning behavior
  - minimized title-match warning and error behavior for WGC-capable modes
  - auto fallback info-line behavior
  - off-screen auto fallback short-circuit
  - forced-WGC exit code `15`
  - WGC frame-size mismatch handling
  - uncapturable-window rejection before backend save
- `docs/manual_test_plan.md`
  - case `GF-MAN-CLI-011` for visible, obscured, off-screen, `HWND`, padded,
    annotated, minimized, and yellow-border observations

At this point, the highest remaining drift risk is usually around user-facing
warning wording or fallback semantics, not missing implementation coverage.
