---
title: CLI WGC Window Capture Design
summary: Proposed CLI backend-selection design for using Windows Graphics Capture for window captures.
audience: contributors
status: proposed
owners:
  - core-team
last_updated: 2026-03-22
tags:
  - cli
  - window
  - capture
  - wgc
  - proposal
---

# CLI WGC Window Capture Design

This document defines the proposed behavior and implementation shape for adding
an optional Windows Graphics Capture backend to CLI window capture.

The user-facing feature is:

- `--window-capture auto|gdi|wgc`

This option applies only to CLI window capture modes:

- `--window`
- `--window-hwnd`

The current GDI path remains available. The new WGC path is CLI-only and does
not change interactive overlay capture behavior.

## Recommendation Summary

- Recommended option name: `--window-capture`
- Recommended values:
  - `auto`
  - `gdi`
  - `wgc`
- Recommended default: `auto`
- Option is valid only with:
  - `--window`
  - `--window-hwnd`
- `auto` should prefer WGC when runtime/platform support is acceptable, and
  otherwise fall back to GDI
- `gdi` preserves current behavior
- `wgc` forces the WGC backend and must not fall back to GDI
- Minimized windows remain errors even under WGC
- Current obscuration and partially-off-screen warnings should disappear when
  WGC is the selected backend
- Recommended new exit code: `15` for forced-window-backend failure or
  unavailability
- The yellow-border question is explicitly unresolved and must be decided later
  before the final `auto` policy is locked

## Goals

- Let CLI users capture window content even when the target window is:
  - obscured by other windows
  - partially outside the visible desktop
- Keep the existing GDI path as an explicit, deterministic backend
- Make backend selection explicit and scriptable
- Keep the feature limited to CLI window capture
- Preserve current exit-code behavior wherever practical
- Preserve existing annotation and padding behavior on the final saved image
- Keep core orchestration in `greenflame_core`
- Keep Win32/WGC implementation details in `src/greenflame/win/`

## Non-Goals For V1

- Changing interactive overlay capture
- Switching monitor, desktop, or region capture to WGC
- Supporting minimized windows under WGC
- Solving the yellow-border question in this document
- Shipping a packaged/MSIX-specific deployment model
- Copy-to-clipboard WGC support
- Recording or multi-frame capture

## Current Behavior

Today, CLI window capture is rect-based and GDI-backed:

- `AppController` resolves an `HWND` and a screen rect
- the capture service receives a `CaptureSaveRequest` containing only a screen
  rect and padding/fill metadata
- the Win32 capture service captures the desktop bitmap and crops the requested
  rect

This means current CLI `--window` behavior inherits desktop-visibility
limitations:

- obscured windows may include other windows in the result
- partially off-desktop windows may clip or require fill
- minimized windows are rejected before capture

Relevant current seams:

- [app_controller.cpp](../src/greenflame_core/app_controller.cpp)
- [app_services.h](../src/greenflame_core/app_services.h)
- [gdi_capture.cpp](../src/greenflame/win/gdi_capture.cpp)
- [win32_services.cpp](../src/greenflame/win/win32_services.cpp)

## Platform Facts

The following platform facts are relevant to the design:

- `IGraphicsCaptureItemInterop::CreateForWindow` is documented for desktop-app
  `HWND` capture starting with Windows 10 version 1903, build 18362
- the broader `Windows.Graphics.Capture` API family starts earlier, but the
  `CreateForWindow` path is the relevant one here
- Microsoft also documents a system capture border for screen-capture APIs
- the newer border-control API, `GraphicsCaptureSession::IsBorderRequired`, is
  newer and tied to separate access/capability requirements

Reference sources:

- [Windows screen capture overview](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture)
- [IGraphicsCaptureItemInterop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nn-windows-graphics-capture-interop-igraphicscaptureiteminterop)
- [GraphicsCaptureSession::IsBorderRequired](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.isborderrequired?view=winrt-26100)

## User-Facing Behavior

### Option grammar

New option:

```bat
--window-capture auto|gdi|wgc
```

Rules:

- default is `auto`
- option is valid only when the capture mode is window-based:
  - `--window`
  - `--window-hwnd`
- it is invalid with:
  - `--region`
  - `--monitor`
  - `--desktop`
  - no capture mode
- duplicate uses are errors
- values are case-insensitive for parsing but should normalize internally

### Backend semantics

`gdi`

- use the current desktop-capture-plus-crop path
- preserve current warnings and current minimized rejection

`wgc`

- force the WGC window-capture path
- if WGC cannot be used on the current system or for the current target window,
  fail loudly rather than silently falling back
- do not fall back to GDI
- minimized windows still fail before capture

`auto`

- prefer WGC when the runtime/platform policy says it is acceptable
- otherwise use GDI
- if `auto` resolves to GDI because WGC is unavailable or disallowed by policy,
  the command may emit an informational stderr line

Recommended info message shape:

- `Info: WGC window capture unavailable; falling back to GDI.`

## Resolved Behavior Decisions

### Applies to both `--window` and `--window-hwnd`

The backend selector must work for both title-based and handle-based window
selection.

Reason:

- both flows resolve to one `HWND`
- supporting one but not the other would be artificial and confusing

### Minimized windows remain errors

Even under WGC, minimized windows remain a hard error in V1.

Reason:

- current CLI behavior already rejects minimized windows
- minimized WGC capture may produce stale or otherwise ambiguous content
- minimized windows complicate coordinate semantics for annotations
- obscured and partially off-desktop windows are the primary value of this
  feature without introducing those ambiguities

Recommended behavior:

- keep existing exit code `13`
- adjust the error text only if needed to mention WGC is not used for minimized
  windows

### WGC removes current obscuration and out-of-desktop warnings

When WGC is the resolved backend, the current CLI window warnings about
obscuration or partially outside visible desktop bounds should not be emitted.

Reason:

- those warnings describe limitations of the GDI desktop-crop path
- they become misleading once the source is the window itself rather than the
  visible desktop pixels

### Padding remains outer synthetic padding

`--padding` still means synthetic outer canvas around the final captured image.

With GDI:

- source holes caused by off-desktop window areas may still be filled using the
  existing logic

With WGC:

- partially off-desktop windows should no longer create source holes that need
  fill
- only the requested outer padding should remain

### Annotations remain a final composite step

The existing CLI annotation flow remains conceptually unchanged:

- capture source bitmap
- build fill/padding canvas if needed
- composite annotations last

For non-minimized WGC window capture:

- local coordinates remain relative to the window capture origin
- global coordinates remain relative to desktop coordinates using the matched
  window rect

Because minimized windows remain unsupported, the ambiguous global-coordinate
case is avoided in V1.

## Unresolved Decision: Yellow Capture Border

This point is intentionally not resolved in this document.

The open question is:

- if WGC causes Windows to draw a system capture border, does that border:
  - appear only around the live on-screen window
  - appear inside the captured pixel content
  - change by OS build, capture path, or deployment model

This matters because it directly affects the final `auto` policy.

### Why it is deferred

The feature can still be designed structurally without deciding this yet, but
the final product default cannot be fully locked until it is tested.

### Required follow-up before final implementation sign-off

A manual spike must determine, for the actual `CreateForWindow(HWND)` path used
by Greenflame:

- whether the border appears on screen
- whether it appears in the captured bitmap
- whether it sits outside the returned content bounds or contaminates edge
  pixels
- whether behavior differs across tested Windows builds

### Interim design rule

Implementation work must preserve a clear policy seam for deciding whether
`auto` should choose WGC on systems where the border may appear.

Do not hardcode:

- `auto = always prefer WGC whenever API support exists`

until this question is closed.

## Recommended `auto` Policy Shape

The final policy is deferred, but the implementation shape should support this
decision table:

1. If user requested `gdi`, use GDI.
2. If user requested `wgc`, require WGC and fail if unavailable.
3. If user requested `auto`:
   - evaluate whether WGC is supported and permitted by policy
   - if yes, use WGC
   - if no, use GDI

The “permitted by policy” part is where the unresolved yellow-border decision
plugs in.

## Architecture Impact

The current save path is not enough for WGC because it only expresses a screen
rect source.

Current shape:

```cpp
struct CaptureSaveRequest {
    RectPx source_rect_screen;
    InsetsPx padding_px;
    COLORREF fill_color;
    bool preserve_source_extent;
    std::vector<Annotation> annotations;
};
```

That is appropriate for:

- desktop capture
- monitor capture
- region capture
- current GDI window capture

It is not a clean fit for:

- WGC capture of a specific `HWND`

### Recommended model change

Introduce an explicit capture-source kind in `CaptureSaveRequest`.

Recommended conceptual shape:

```cpp
enum class CaptureSourceKind : uint8_t {
    ScreenRect,
    WindowHwnd,
};

enum class WindowCaptureBackend : uint8_t {
    Auto,
    Gdi,
    Wgc,
};
```

And then extend the request so the service can distinguish:

- rect-based capture
- HWND-based window capture
- desired window backend

The exact C++ layout is an implementation detail, but the request must be able
to carry:

- requested capture source kind
- resolved or requested backend
- target `HWND` for window capture
- canonical window rect for output geometry and annotation coordinate mapping

### Why this is preferable

- keeps backend selection out of the Win32 service internals alone
- keeps controller policy explicit and testable
- avoids shoehorning WGC into a rect-only interface

## Controller Behavior

`AppController` should continue to own:

- CLI parse validation
- window lookup and ambiguity handling
- minimized-window rejection
- output-path policy
- warning selection
- annotation parse/preparation sequencing

For window capture specifically, the controller should additionally own:

- resolving the requested backend from `--window-capture`
- evaluating `auto`
- deciding whether to emit an info line when `auto` falls back to GDI
- suppressing GDI-only warnings when WGC is the resolved backend

## Win32 Implementation Behavior

The Win32 layer should own:

- WGC capability probing
- `HWND` -> `GraphicsCaptureItem` interop
- device/session/frame acquisition
- conversion of the captured frame into the bitmap format expected by the save
  pipeline

The GDI helper code should remain intact as the explicit GDI backend.

## Error Handling

Recommended behavior:

- parse errors for `--window-capture` remain CLI argument failures
- minimized window remains exit code `13`
- forced `wgc` on an unavailable/unsupported system should fail with a dedicated
  backend-failure exit code
- forced `wgc` runtime failures for the matched window should also fail with that
  same backend-failure exit code in V1
- `auto` fallback to GDI is not an error

Recommended stderr wording examples:

- `Error: WGC window capture is not available on this system.`
- `Error: Forced WGC window capture failed for the matched window.`
- `Info: WGC window capture unavailable; falling back to GDI.`

Recommended new exit code:

- `15 = CliWindowCaptureBackendFailed`

Reason:

- `wgc` is an explicit strict request rather than a hint
- silently or generically collapsing this into the normal capture-save failure
  path would make scripting less reliable
- callers should be able to distinguish:
  - generic save/write failures
  - window unavailability
  - minimized-window rejection
  - forced backend unavailability/failure

## Testing Impact

### Automated tests

Tests should cover:

- CLI parse of `--window-capture auto|gdi|wgc`
- option validity only with `--window` / `--window-hwnd`
- duplicate rejection
- controller behavior for:
  - `gdi`
  - `wgc`
  - `auto` resolving to WGC
  - `auto` falling back to GDI
- minimized-window rejection regardless of backend
- warning suppression when WGC is resolved

### Manual verification

Manual cases should cover:

- visible unobscured window, `gdi`
- visible unobscured window, `wgc`
- fully obscured window, `gdi`
- fully obscured window, `wgc`
- partially off-desktop window with padding, `gdi`
- partially off-desktop window with padding, `wgc`
- `--window-hwnd` with `wgc`
- `auto` on a system where WGC is accepted
- `auto` on a system where WGC falls back to GDI
- minimized window under `wgc` still failing with exit `13`
- yellow-border investigation cases

## Open Questions

These questions remain open for later decision:

1. Yellow border:
   - does it appear in captured pixels
   - on which systems/builds
   - should `auto` avoid WGC if the border is visible on screen but not in
     captured pixels
2. Exact `auto` eligibility criteria:
   - runtime API support only
   - or runtime API support plus additional product-policy gates
3. Messaging:
   - should `auto` fallback always print an info line
   - or only in verbose/debug-style scenarios

## Recommended Implementation Order

1. Extend CLI parsing with `--window-capture`.
2. Add a backend-selection enum and controller policy.
3. Extend the capture request model to represent window-HWND capture explicitly.
4. Add a WGC-backed Win32 capture path for CLI window capture.
5. Keep minimized-window rejection unchanged.
6. Remove GDI-only warnings when WGC is the resolved backend.
7. Add tests and manual cases.
8. Resolve the yellow-border policy before finalizing `auto`.
