---
title: Pinned Image Design
summary: Current reference for Greenflame's interactive pin-to-desktop action, pinned-image window behavior, and export parity rules.
audience:
  - contributors
  - qa
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - pin
  - export
  - win32
  - toolbar
---

# Pinned Image Design

This document describes the current shipped pin-to-desktop implementation.

It replaces the earlier proposal-style version of this file. The implementation,
tests, and higher-priority reference docs are authoritative when this document is
updated.

For user-facing behavior, see [README.md](../README.md). For the shared overlay
toolbar and output pipeline, see [docs/annotation_tools.md](../annotation_tools.md).
For captured-cursor behavior that affects pin output parity, see
[captured_cursor_design.md](captured_cursor_design.md).

## Current Status

Pinned images are already shipped for the interactive overlay flow.

Current scope:

- pinning is available only from the interactive overlay
- the entry points are the toolbar pin button and `Ctrl+P`
- on success, pinning is a terminal overlay action: the pin is created and the
  overlay closes
- on failure, the overlay stays open and shows a warning dialog

Current non-goals that remain true in implementation:

- no CLI pinning
- no tray-menu pinning
- no pin persistence across app restart
- no border or corner resize handles
- no pin editing surface

## User Model

### Overlay entry and creation

The current overlay toolbar layout is:

`[annotation tools][spacer][cursor][pin][spacer][help]`

Current pin entry points:

- toolbar tooltip: `Pin to desktop (Ctrl+P)`
- keyboard shortcut: `Ctrl+P`

`OverlayController::On_pin_requested()` returns `PinToDesktop` only when a stable
selection exists. With no selection, the action is rejected cleanly.

Current success/failure behavior:

- `OverlayWindow` builds the rendered selection bitmap
- `GreenflameApp` forwards it to `PinnedImageManager`
- if pin-window creation succeeds, the overlay closes
- if creation fails, the overlay stays open and shows
  `Failed to pin the selection to the desktop.`

### Rendered-bitmap parity

Pinned images reuse the same rendered bitmap that the overlay uses for save and
copy output.

Current pin content includes:

- the selected capture region
- committed annotations that intersect that region
- the captured cursor only when the overlay toggle currently shows it

Current pin content excludes:

- toolbar UI
- help overlay
- selection borders
- resize handles
- annotation selection chrome
- the pin halo itself

This parity is intentional current behavior, not design intent waiting on future
implementation.

### Pin window behavior

Each pin is a separate top-level window that is:

- frameless
- always on top
- a tool window, so it does not create a taskbar button
- layered, so the halo and bitmap can be composited together

Current pointer behavior:

- left-click activates the pin
- left-button drag anywhere on the pin moves it
- mouse wheel zooms in or out
- right-click activates the pin and opens its context menu
- there are no border or corner resize gestures

Current keyboard behavior for the active pin:

- `Ctrl+C` copies the current rotated pin image
- `Ctrl+S` saves the current rotated pin image
- `Ctrl+Right` rotates right
- `Ctrl+Left` rotates left
- `Ctrl+=` / `Ctrl+-` zooms in or out
- `Ctrl+Up` / `Ctrl+Down` increases or decreases on-screen opacity
- `Esc` closes the pin

Opacity key repeats are intentionally supported. Zoom and rotation remain
single-step per key press.

### Multiple pins and lifetime

Multiple pins can coexist at once.

Current lifetime rules:

- a newly created pin starts active
- each pin keeps independent position, scale, rotation, and opacity
- closing one pin does not affect other pins
- existing pins outlive the overlay that created them
- exiting Greenflame closes all remaining pins

## Pin Window Presentation Model

### Halo treatment

The pin halo is always visible.

Current presentation model:

- idle pins use a visible green halo and stroke
- active pins use the same hue family with materially stronger alpha values
- halo strength is independent from image opacity
- the window reserves explicit halo padding so the glow is not clipped

Current implementation constants in `pinned_image_window.cpp`:

- halo padding: `8 px`
- corner radius: `6 px`
- three outer halo bands plus one inner stroke

### Zoom, rotation, and opacity

Current zoom model:

- default scale: `1.0`
- zoom step factor: `1.1`
- scale clamp: `0.25 .. 8.0`
- zoom preserves the window center

Current rotation model:

- quarter-turn only
- rotation state is stored as clockwise quarter turns modulo `4`
- rotation preserves the window center

Current opacity model:

- on-screen opacity range: `20% .. 100%`
- step size: `10%`
- halo opacity does not change with image opacity

Copy and save export the rotated image fully opaque. Reduced on-screen opacity is
display-only and is never baked into exported pixels.

### Context menu

The current context-menu order is:

1. `Copy to clipboard`
2. `Save to file`
3. separator
4. `Rotate Right`
5. `Rotate Left`
6. separator
7. `Increase Opacity`
8. `Decrease Opacity`
9. separator
10. `Close`

The menu shows keyboard accelerators inline, disables opacity commands at the
clamps, and keeps the target pin visually active while the menu is open.

## Architecture And Ownership

### Top-level ownership

The current ownership split is:

- `greenflame_core::OverlayController`
  - decides whether pinning is currently allowed
  - returns `OverlayAction::PinToDesktop`
- `greenflame::OverlayWindow`
  - routes toolbar pin clicks and `Ctrl+P`
  - builds the rendered selection capture
  - forwards the finished bitmap and placement rect to the app layer
- `greenflame::GreenflameApp`
  - owns the long-lived `PinnedImageManager`
- `greenflame::PinnedImageManager`
  - creates pin windows
  - tracks them until they close
  - closes all of them on app shutdown
- `greenflame::PinnedImageWindow`
  - owns one captured bitmap plus its presentation state

This keeps policy in core and all windowing/rendering behavior in the Win32 layer.

### Data flow

Current creation flow:

1. The user creates a stable selection.
2. The user presses `Ctrl+P` or clicks the pin button.
3. `OverlayController` returns `PinToDesktop`.
4. `OverlayWindow::Build_rendered_selection_capture(...)` builds the final bitmap.
5. `GreenflameApp::On_selection_pinned_to_desktop(...)` forwards that bitmap to
   `PinnedImageManager::Add_pin(...)`.
6. `PinnedImageManager` creates a `PinnedImageWindow`.
7. The pin window takes ownership of the rendered capture bitmap.
8. The overlay closes only after successful pin creation.

## Rendering And Export Pipeline

### Initial placement

The initial pin placement comes directly from the overlay selection's physical
screen rect.

Current behavior:

- the pin opens at `100%` scale
- the bitmap is positioned at the original selection location
- the window adds only the fixed halo padding around that bitmap

For interactive `Ctrl` window selection that captured a full off-screen or
oversized window, the pin uses the full rendered window bitmap at `100%` scale.
That means pins created from those selections may also start partly off-screen.

### On-screen rendering

`PinnedImageWindow::Refresh_layered_window(...)` rebuilds the window surface from:

- the owned capture bitmap
- current scale
- current rotation
- current on-screen opacity
- current active/idle halo state

The window uses `UpdateLayeredWindow(...)` to publish the halo and bitmap
together as one layered surface.

### Copy and save export

`PinnedImageWindow::Build_export_capture(...)` creates a fresh export bitmap that:

- applies the current rotation
- ignores the current display opacity
- does not include the halo
- does not include any window chrome

This export bitmap is then used for both:

- `Copy_to_clipboard()`
- `Save_to_file()`

Current save-dialog behavior:

- default format comes from the app config's current save-format preference
- default filename is `greenflame-pin-YYYYMMDD-HHMMSS` plus that format's extension
- successful saves update `last_save_as_dir` in memory

## Capture Exclusion And DPI Rules

Pinned images follow the repository's physical-pixel coordinate rules.

Current invariants:

- initial placement uses physical screen coordinates
- drag movement updates the layered window in physical pixels
- zoom and rotation compute new window dimensions from physical-pixel content size
- cross-monitor moves do not introduce any DIP storage path

Current capture-exclusion behavior:

- `PinnedImageWindow::Create(...)` calls `SetWindowDisplayAffinity(..., WDA_EXCLUDEFROMCAPTURE)`

As a result, later Greenflame captures should not include existing pin windows in
their captured content.

## Existing Coverage

Automated coverage in this area is limited but non-zero.

Key automated coverage currently exists in:

- `tests/overlay_controller_tests.cpp`
- `tests/app_controller_tests.cpp`

Covered areas include:

- `OverlayController::On_pin_requested()` returning `PinToDesktop` only when a
  selection exists
- overlay help content including the `Ctrl + P` shortcut

Most pin-window behavior is Win32- and rendering-heavy, so primary coverage is
manual. Key manual coverage already exists in `docs/manual_test_plan.md`,
especially:

- `GF-MAN-PIN-001`
- `GF-MAN-PIN-002`
- `GF-MAN-PIN-003`
- `GF-MAN-PIN-004`
- `GF-MAN-PIN-005`
- `GF-MAN-PIN-006`

Future changes in this area should preserve rendered-bitmap parity with the
overlay export pipeline instead of introducing a separate pin-only render path.
