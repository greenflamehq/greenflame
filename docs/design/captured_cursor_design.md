---
title: Captured Cursor Design
summary: Current reference for Greenflame's captured-cursor capture, overlay toggle, tray persistence, and live-capture export behavior.
audience:
  - contributors
  - qa
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - capture
  - overlay
  - cursor
  - cli
  - config
---

# Captured Cursor Design

This document describes the current shipped captured-cursor implementation.

It replaces the earlier proposal-style version of this file. The implementation,
tests, and higher-priority reference docs are authoritative when this document is
updated.

For user-facing behavior, see [README.md](../README.md). For shared overlay and
toolbar behavior, see [docs/annotation_tools.md](../annotation_tools.md). For CLI
window-capture backend semantics, see [cli_window_capture.md](cli_window_capture.md).

## Current Status

Captured-cursor support is already shipped across the live-capture paths:

- interactive overlay capture
- direct tray and global-hotkey clipboard captures
- CLI live captures
- pinned-image creation from the interactive overlay

Current invariants:

- the captured cursor is sampled from the live screen capture, not from imported
  `--input` images
- the captured cursor is part of screenshot content, not an annotation
- when shown, it is composited below annotations
- it is never selectable, movable, resizable, deletable, or undoable as an
  annotation object
- the persisted default lives at `capture.include_cursor`

Current default behavior:

- default value is `false`
- the key is omitted from serialized config when it remains `false`
- absent config parses as `false`

## User Model

### Interactive overlay

When the overlay opens, `OverlayWindow` always attempts to capture both:

- the full virtual-desktop bitmap
- one frozen cursor snapshot for that capture session

That snapshot is retained for the full overlay lifetime even when the persisted
setting currently hides it. This is why turning the feature on mid-session can
still reveal the cursor that was sampled at capture time.

Current overlay controls:

- `Ctrl+K` toggles captured-cursor visibility
- the toolbar contains a dedicated captured-cursor button
- the button uses the same stable cursor glyph in both states
- the button is active when the captured cursor is currently shown

Current toolbar layout is:

`[annotation tools][spacer][cursor][pin][spacer][help]`

Current tooltip behavior:

- shown state: `Hide captured cursor (Ctrl+K)`
- hidden state: `Show captured cursor (Ctrl+K)`
- when the current capture has no usable sampled cursor, the tooltip appends
  ` - no captured cursor in this image`

Current interaction rules:

- toggling the captured cursor does not change the active annotation tool
- the toggle affects the frozen screenshot only, never the live editing pointer
- the cursor remains outside hit testing, selection, annotation copy/paste, and
  undo/redo
- saved output, copied output, and pinned output all reflect the current visible
  state

### Direct tray and hotkey clipboard captures

The non-overlay clipboard paths have no post-capture editor, so they use only the
persisted default.

Current behavior:

- tray `Capture current window`
- tray `Capture current monitor`
- tray `Capture full screen`
- tray `Capture last region`
- tray `Capture last window`
- `Ctrl + Prt Scrn`
- `Shift + Prt Scrn`
- `Ctrl + Shift + Prt Scrn`
- `Alt + Prt Scrn`
- `Ctrl + Alt + Prt Scrn`

All honor `capture.include_cursor` directly.

### CLI live captures

Live CLI captures use the persisted config value by default and allow per-run
override flags:

- `--cursor`
- `--no-cursor`

Current CLI rules:

- default behavior resolves from `capture.include_cursor`
- `--cursor` forces inclusion for one invocation
- `--no-cursor` forces exclusion for one invocation
- `--cursor` and `--no-cursor` are mutually exclusive
- neither flag mutates the saved config
- both flags are rejected for `--input`

`--input` images never have a live cursor sample, so excluding these flags is
intentional current behavior, not a future proposal.

## Data Model And Ownership

### Core policy state

`greenflame_core` owns the policy-level state only:

- `AppConfig::include_cursor`
- `CliCursorOverride`
- `CaptureSaveRequest::include_cursor`

`AppController` resolves effective CLI behavior through
`Resolve_include_cursor(...)`, which folds:

- persisted config
- `--cursor`
- `--no-cursor`

The direct clipboard paths also read `config_.include_cursor` directly when they
dispatch copy requests to the capture service.

### Win32 snapshot model

The Win32 layer owns cursor acquisition and compositing.

The current `CapturedCursorSnapshot` stores:

- an owned copied `HCURSOR`
- `image_width`
- `image_height`
- `hotspot_screen_px`
- `hotspot_offset_px`

Current helper boundaries:

- `Capture_cursor_snapshot(...)`
  - samples the visible cursor
  - copies the icon handle
  - records hotspot position in physical screen pixels
  - derives size from either the color bitmap or the mask bitmap
- `Composite_cursor_snapshot(...)`
  - translates hotspot-based screen coordinates into the target bitmap's local
    coordinate space
  - draws with `DrawIconEx`
  - omits fully out-of-bounds cursors without treating that as an error

This keeps cursor-specific Win32 details out of the annotation model and out of
CLI parsing code.

### Overlay ownership

`OverlayWindow` owns the interactive session artifacts:

- `base_capture`
- `display_capture`
- optional `captured_cursor`
- optional lifted full-window capture bitmaps for the `Ctrl` quick-select path

Current responsibility split:

- `OverlayWindow`
  - captures and stores the per-session cursor snapshot
  - rebuilds display bitmaps when visibility changes
  - applies the current visible state to save/copy/pin flows
- `GreenflameApp`
  - owns persisted app config in memory
  - exposes tray-menu toggle events
- `TrayWindow`
  - exposes the persisted default as a checked menu item
- `Win32CaptureService`
  - handles direct clipboard capture and CLI save composition

## Capture And Composition Pipeline

### Overlay session pipeline

Current interactive pipeline:

1. Capture the virtual desktop into `resources_->base_capture`.
2. Attempt `Capture_cursor_snapshot(...)`.
3. Build `resources_->display_capture` from the base capture plus the current
   visibility flag.
4. Upload the display bitmap into the D2D overlay renderer.
5. Paint annotations and overlay chrome separately above that screenshot layer.

Because annotations are painted after `display_capture`, the captured cursor
always stays below committed annotations and draft previews.

### Mid-session toggle behavior

Current toggle behavior is implemented by
`OverlayWindow::Toggle_captured_cursor_visibility()`.

On each toggle, the overlay:

1. flips `config_->include_cursor`
2. rebuilds the display bitmap from `base_capture` plus the stored cursor snapshot
3. rebuilds toolbar state and uploads the new screenshot layer
4. attempts to persist the updated config

If rebuilding the display bitmap fails, the in-memory toggle is rolled back.

### Save, copy, pin, and export layering

Interactive output uses two stages:

- `Build_selection_capture(...)`
  - crops the base capture for the current selection
  - composites the captured cursor only when it is currently visible
- `Build_rendered_selection_capture(...)`
  - starts from that selection capture
  - rasterizes annotations into it

That rendered result is then used by:

- save
- copy to clipboard
- pin to desktop

As a result, pinned images match the rendered overlay export: the captured cursor
is included only when currently shown, and it still sits below annotations.

### Direct clipboard and CLI composition

`Win32CaptureService` uses the same basic ordering for non-overlay outputs:

1. capture source bitmap
2. capture cursor snapshot when `include_cursor` is enabled
3. composite cursor into the source or padded canvas
4. render annotations
5. copy or save final bitmap

The helper `Maybe_composite_captured_cursor(...)` centralizes the
"include only when requested and valid" gate for the save paths.

## Window Capture And WGC Interaction

The current WGC path intentionally disables backend-native cursor capture:

- `wgc_window_capture.cpp` calls `session.IsCursorCaptureEnabled(false)`

Greenflame then samples its own cursor snapshot and composites it later using the
same helper path as the non-WGC flows.

Current consequences:

- WGC window capture never bakes in a second backend-provided cursor
- `Ctrl+K` still fully hides or shows the one Greenflame-managed captured cursor
- exported results keep one consistent layering rule regardless of window-capture
  backend

The interactive `Ctrl` window quick-select path also rebuilds its lifted
full-window display bitmap through the same cursor-compositing helper, so the
lifted preview stays consistent with the main screenshot layer.

## Persistence And Config Semantics

The captured-cursor default is persisted at:

- `capture.include_cursor`

Current parse/serialize behavior in `app_config_json.*`:

- parser accepts only booleans
- non-boolean values are rejected
- serializer writes the key only when it differs from the default

Current toggle entry points differ slightly:

- tray menu toggle:
  - flips the persisted default through `GreenflameApp::On_set_include_cursor_enabled`
  - rolls back on save failure
  - shows a warning balloon when persistence fails
- overlay toggle:
  - updates the current overlay immediately
  - attempts to persist the new default after rebuilding the display

Both are current implementation details and should stay aligned if this area is
refactored.

## Correctness Rules

Captured-cursor handling follows Greenflame's normal coordinate rules:

- hotspot and target origins are tracked in physical pixels
- overlay placement is derived from virtual-desktop screen coordinates
- composition uses explicit origin subtraction, not implicit DPI conversion
- partially intersecting cursors clip normally
- fully out-of-bounds cursors are omitted without error

The current acquisition helper supports both:

- color-bitmap cursors
- mask-based cursors

I-beam, resize, crosshair, busy/wait, and custom cursors remain important manual
validation targets, but they are validation focus areas rather than open design
questions in this document.

## Existing Coverage

Key automated coverage already exists in:

- `tests/app_config_tests.cpp`
- `tests/cli_options_tests.cpp`
- `tests/app_controller_tests.cpp`

Covered areas include:

- default `capture.include_cursor = false`
- parse/serialize behavior for `capture.include_cursor`
- rejection of non-boolean config values
- CLI parsing for `--cursor`
- CLI parsing for `--no-cursor`
- rejection of `--cursor --no-cursor`
- rejection of cursor overrides with `--input`
- direct clipboard paths honoring the persisted config
- CLI save requests honoring both config defaults and explicit overrides

Key manual coverage already exists in `docs/manual_test_plan.md`, especially:

- `GF-MAN-CURSOR-001`
- `GF-MAN-CURSOR-002`
- `GF-MAN-CURSOR-002A`
- `GF-MAN-CURSOR-003`
- `GF-MAN-PIN-001` for rendered pin parity

Future changes in this area should keep the cursor as captured scene content and
should not route it through the annotation model.
