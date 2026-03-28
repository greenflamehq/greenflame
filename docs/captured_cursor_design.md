---
title: Captured Cursor Design
summary: Adds optional captured-cursor retention for interactive and CLI captures,
  with persistent config, overlay toggle, and CLI overrides.
audience:
  - contributors
  - qa
status: draft
owners:
  - core-team
last_updated: 2026-03-27
tags:
  - capture
  - overlay
  - cursor
  - cli
  - config
---

# Captured Cursor Design

## Overview

Greenflame currently captures the desktop image but does not preserve the cursor
as part of the captured scene. This feature adds support for capturing the cursor
image and its position at capture time, then optionally showing that frozen
cursor inside the overlay and in the final saved or copied output.

The feature is intentionally **not** a live-cursor feature. It applies to the
captured image only.

The desired end state is:

- interactive capture can show or hide the captured cursor after the capture is
  already on screen
- the captured cursor always renders **below annotations**
- the captured cursor is never selectable, movable, resizable, or undoable as an
  annotation object
- a toolbar button and `Ctrl+K` toggle the captured cursor on or off
- the toggle state persists across captures and across app restarts
- CLI capture defaults to the persisted config value and can be overridden by
  `--cursor` or `--no-cursor` without changing the config
- direct tray and hotkey clipboard captures honor the same persisted cursor
  preference

## Terminology

This feature needs strict terminology because "cursor" can refer to two different
things during overlay use.

- **Captured cursor**: a frozen cursor image plus metadata sampled at capture
  time. This is part of the screenshot content.
- **Live cursor**: the current OS pointer the user uses to interact with the
  overlay UI after capture.

This document uses **captured cursor** for the feature and **live cursor** for
normal pointer interaction.

## Goals

- Preserve the cursor from the original screen capture as an optional visual layer.
- Keep captured-cursor positioning correct in physical pixels on mixed-DPI,
  multi-monitor desktops.
- Allow the user to toggle the captured cursor during the interactive overlay.
- Keep the captured cursor outside the annotation model.
- Persist the default include/exclude preference in config.
- Let CLI invocations override that preference without mutating config.
- Reuse one cursor-compositing model for interactive output and CLI output.

## Non-goals

- Treating the captured cursor as an annotation.
- Allowing the captured cursor to be selected, moved, resized, deleted with the
  annotation system, or included in undo/redo.
- Capturing a live cursor for `--input` images or any other imported image source.
- Animating cursors in output. A still image captures a single sampled frame.
- Solving Greenshot's I-beam issue preemptively without evidence. The design must
  reduce the risk and test for it, but the root cause is not confirmed.

## Greenshot Reference

DeepWiki research on `greenshot/greenshot` indicates that Greenshot handles the
cursor as a separate captured artifact rather than baking it into the base screen
bitmap up front.

Confirmed findings from the repo research:

- Greenshot captures cursor data separately from the screenshot bitmap.
- Greenshot tracks cursor image data, visibility, position, and hotspot-aware
  placement.
- Greenshot renders or inserts the cursor later in the editor flow instead of
  requiring it to be part of the original screen bitmap.
- DeepWiki points to `WindowCapture.CaptureCursor`, `Capture` / `ICapture`,
  `CaptureForm`, and editor-surface code as the relevant areas.
- Greenshot documents a known issue that the **I-beam cursor is not displayed
  correctly in the final result**. DeepWiki attributes that note to
  `installer/additional_files/readme.txt`.

What is **not** confirmed from the Greenshot repo research:

- no documented root cause for the I-beam issue was found
- no documented workaround for the I-beam issue was found

Reasonable but unconfirmed inference:

- hotspot handling is a likely risk area, but it is not enough to claim that
  hotspot math alone explains Greenshot's I-beam bug

Design consequence for Greenflame:

- Greenflame should also model the captured cursor separately from annotations
  and from the base screenshot
- Greenflame should preserve explicit hotspot metadata
- Greenflame should treat I-beam and other cursor families as a validation focus,
  not as a solved problem

## Current Greenflame Baseline

Relevant current behavior:

- the interactive overlay starts from a capture-first model
- the virtual desktop is captured as a `GdiCaptureResult`
- annotations are rendered later and composited into the output
- the toolbar currently contains annotation tools, then a spacer, then help
- annotation tools are inactive until a capture region exists
- overlay rendering and interaction operate in physical pixels
- CLI save paths and interactive save paths already have shared output concerns,
  but there is no captured-cursor layer today

This matters because the new feature should fit the existing architecture:

- keep cursor capture and bitmap conversion in the Win32 layer
- keep annotation behavior in `greenflame_core`
- do not introduce the captured cursor as a `core::Annotation`

## Recommended User Experience

### Interactive overlay

When an interactive capture starts, Greenflame should capture:

- the base desktop image
- the current cursor image, if one is available
- the cursor hotspot position in screen physical pixels
- the cursor hotspot offset within the cursor image

The overlay should initialize the captured-cursor visibility from config.

Behavior:

- if the setting is on and a cursor sample exists, the captured cursor is visible
  immediately in the overlay background
- if the setting is off, the captured cursor is hidden even if a cursor sample
  exists
- toggling the setting updates the current overlay immediately
- the captured cursor always renders below annotations
- the captured cursor never participates in hit testing, selection, move, resize,
  delete, undo, redo, copy, or annotation serialization

### Toolbar button

The toolbar order should become:

`[annotation tools] [spacer] [captured cursor toggle] [spacer] [help]`

The button should use a **single stable cursor icon**, not dual "cursor" /
"no-cursor" icons.

Rationale:

- dual icons are ambiguous about whether they represent the **current state** or
  the **action that will happen when clicked**
- adding a separate toggle active/inactive treatment on top of that makes the
  semantics harder to read, not easier
- Greenflame already has an active/inactive button treatment, so state should be
  expressed by button styling and tooltip text, not by swapping the glyph meaning

Button rules:

- icon in both states: `cursor.png`
- toggled-on state means "captured cursor is currently shown"
- toggled-off state means "captured cursor is currently hidden"
- the button is not an annotation tool and does not affect the active annotation
  tool selection
- clicking the button toggles the captured-cursor visibility state

Recommendation:

- do not use `cursor-no.png` for the primary toolbar toggle in the first
  iteration
- if that asset is kept, reserve it for a future explicit action surface only if
  the UX clearly communicates action rather than state

Because the toolbar is currently selection-anchored, the button inherits that
behavior:

- the toolbar button is only visible when the toolbar itself is visible
- the hotkey remains the way to toggle before a selection exists

### Hotkey

`Ctrl+K` toggles captured-cursor visibility.

Rules:

- it should work whether or not an annotation tool is active
- it should not clear or change the active annotation tool
- it should be blocked by higher-priority modal/top-layer UI such as the help
  overlay or the obfuscate warning dialog

### Persistence semantics

The toggle is a persisted preference, not a temporary edit-only switch.

Recommended behavior:

- clicking the toolbar button or pressing `Ctrl+K` updates the current overlay
  state
- the same action also updates the persisted config immediately
- if the user cancels the current capture afterward, the new preference still
  applies to the next capture

This matches the feature request more closely than treating the toggle as a
per-capture-only choice.

Scope of the persisted preference:

- applies to interactive overlay captures
- applies to direct tray and hotkey clipboard captures that perform a live capture
- applies to CLI live captures unless overridden by `--cursor` or `--no-cursor`
- does not apply to `--input` because imported images do not have a live
  capture-time cursor sample

### Captures with no available cursor sample

Some captures may have no usable cursor sample because the cursor was not visible
or the OS capture step could not provide one.

Recommended behavior:

- keep the toggle available because it controls the persisted default for future
  captures
- if the current capture has no cursor sample, toggling changes the preference but
  has no visible effect on the current image
- the tooltip should make this clear for the current capture, for example by
  indicating that no captured cursor is available in this image

This avoids needing a separate settings surface just to change the default.

## Config Design

### Proposed key

Persist the setting as:

`capture.include_cursor`

### Rationale

This should not live under `tools` because the captured cursor is not an
annotation tool.

This should not live under `ui` because the setting affects CLI output behavior
too.

This should not live under `save` because it affects captured content, not naming
or encoding policy.

`capture.include_cursor` scopes the setting to capture content and leaves room for
future capture-related options.

### Behavior

- type: boolean
- default: `false`
- parser default when absent: `false`
- serializer writes only when `true`, matching the current write-non-defaults
  config style

Recommended default: `false`

Reason:

- it preserves today's behavior for existing users
- it avoids surprising users by suddenly including a cursor in screenshots after an
  upgrade

## CLI Design

### New options

- `--cursor`
- `--no-cursor`

### Semantics

For capture-producing CLI flows:

1. Start from `capture.include_cursor`.
2. If `--cursor` is present, force inclusion for that invocation.
3. If `--no-cursor` is present, force exclusion for that invocation.

Rules:

- `--cursor` and `--no-cursor` are mutually exclusive
- neither option writes back to config

Outside CLI:

- direct tray and hotkey clipboard captures use `capture.include_cursor`
- interactive overlay captures initialize from `capture.include_cursor` but allow
  the user to change the current and persisted state with the toolbar button or
  `Ctrl+K`

### Scope

These switches should apply only to CLI flows that actually perform a live screen
or window capture.

Recommended validation rule:

- reject `--cursor` and `--no-cursor` when `--input` is used

Reason:

- imported images do not have a live capture-time cursor sample
- silently accepting the switches for `--input` would imply behavior Greenflame
  cannot honor correctly

The error should fail loudly and name the incompatible options.

## Architecture And Ownership

### Principle

The captured cursor is **capture metadata plus a renderable bitmap**, not an
annotation.

That implies:

- Win32 capture code owns cursor acquisition and bitmap normalization
- overlay rendering owns display of the captured cursor
- output rendering owns compositing the captured cursor into saved or copied images
- `greenflame_core` owns only the persisted preference and CLI parse state that are
  needed for policy

### Keep it out of the annotation model

Do **not** model the cursor as:

- `core::Annotation`
- an annotation tool
- annotation-controller state
- annotation-preparation payload

Why:

- the user explicitly does not want it selectable or movable
- annotations are user-created markup; the captured cursor is captured scene data
- putting the cursor into annotations would create wrong semantics for hit testing,
  undo, copy/paste, and future feature work

### Proposed Win32-side cursor snapshot

The Win32 layer should normalize the cursor into a durable snapshot type. Exact
type names can change, but the snapshot should contain at least:

- `available`: whether a usable cursor sample exists
- `bitmap`: a 32bpp top-down bitmap or equivalent owned image data
- `bitmap_size_px`: width and height in physical pixels
- `hotspot_offset_px`: hotspot offset within the cursor image in physical pixels
- `hotspot_screen_px`: hotspot position in virtual-desktop screen physical pixels

Recommended storage rule:

- store the **hotspot screen position**, not only the image top-left

Reason:

- the hotspot is the real semantic cursor location
- drawing into overlay client space and cropped output space becomes a simple,
  explicit translation:
  - `draw_top_left = hotspot_screen_px - hotspot_offset_px - target_origin_px`

### Ownership lifetime

For interactive capture:

- store the cursor snapshot next to the captured desktop image for the lifetime of
  the overlay session

For CLI capture:

- either capture the cursor inline in the save path or reuse the same lower-level
  helper that produces the snapshot

Recommendation:

- share the same cursor-normalization and compositing helpers between interactive
  and CLI paths
- avoid exposing Win32 cursor handles outside the Win32 layer

## Capture And Render Pipeline

### Interactive capture pipeline

Recommended pipeline:

1. Capture the virtual desktop bitmap.
2. Capture and normalize the cursor snapshot.
3. Open the overlay with both artifacts retained.
4. Render the base desktop image.
5. If captured-cursor visibility is enabled and a cursor snapshot exists, render
   the captured cursor.
6. Render committed annotations.
7. Render in-progress annotation previews and remaining overlay chrome.

This keeps the cursor visually under annotations while still behaving like part of
the original scene.

### Saved and copied output

When the user saves or copies the final selection:

1. Crop the base capture as today.
2. If captured-cursor visibility is enabled and the cursor intersects the cropped
   output, composite the cursor into the cropped capture.
3. Render annotations into the result.
4. Save or copy the final bitmap.

This ordering is required by the feature request.

### Direct tray and hotkey clipboard captures

For non-overlay live captures triggered directly from the tray or capture hotkeys:

1. Perform the live capture.
2. If `capture.include_cursor` is enabled, capture and normalize the cursor
   snapshot.
3. Composite the cursor into the output image.
4. Copy the final bitmap to the clipboard.

These flows do not expose a post-capture toggle because they do not open the
overlay editor.

### CLI output

For live capture CLI flows:

1. Perform the screen or window capture.
2. If cursor inclusion is enabled, capture and normalize the cursor snapshot.
3. Composite the cursor into the output image before annotations.
4. Render annotations.
5. Save output.

If the final CLI path already routes through a common output compositor, reuse it.

## Positioning, DPI, And Correctness Rules

The captured cursor must obey Greenflame's existing correctness rules:

- all stored cursor geometry must be in physical pixels
- no implicit DIP conversion
- all boundary conversions must be explicit and localized

Positioning rules:

- cursor sampling should use virtual-desktop screen coordinates
- overlay rendering converts screen coordinates to overlay client coordinates by
  subtracting the overlay window origin
- output rendering converts screen coordinates to crop-local coordinates by
  subtracting the crop origin

Cropping rules:

- if the cursor lies fully outside the cropped output, draw nothing
- if the cursor partially overlaps the cropped output, clip normally and draw the
  visible portion

Multi-monitor rules:

- support negative-coordinate monitors
- support mixed DPI without reinterpreting cursor coordinates in DIP space

## Cursor Acquisition Notes

The exact Win32 API sequence can be finalized during implementation, but the design
should assume these requirements:

- acquire the currently visible cursor and its hotspot metadata from the OS
- normalize the cursor into owned 32bpp pixel data at capture time
- preserve alpha correctly
- support both normal alpha cursors and older mask-based cursor representations

Recommended implementation guardrails:

- do not store raw `HCURSOR` handles as the long-lived session representation
- do not assume the bitmap origin is the hotspot
- do not special-case only arrow cursors; validate text, resize, crosshair, and
  custom cursors too

## I-beam Risk And Mitigation

Greenshot's known I-beam issue is the strongest external warning for this feature.

Facts:

- Greenshot documents that the I-beam cursor may not display correctly in final
  output
- the available research did not reveal a documented root cause

Risks for Greenflame:

- hotspot math may be correct for most cursors but still wrong for some cursor
  families
- cursor bitmap extraction may differ between alpha cursors and mask-based cursors
- DPI scaling or system-provided cursor resources may behave differently for text
  cursors

Mitigation strategy:

- preserve hotspot metadata explicitly
- normalize every captured cursor through one renderable bitmap path
- add manual coverage for I-beam, resize, crosshair, wait, and custom cursors
- validate on mixed-DPI, multi-monitor setups, especially where monitors have
  negative virtual-desktop coordinates
- if a cursor sample is obviously invalid, prefer omitting it over drawing it in a
  clearly wrong location

The last point is intentionally conservative. A missing captured cursor is less
damaging than a visibly misplaced one that undermines trust in output correctness.

## UI And Help Content

The overlay help content should be updated later to include:

- `Ctrl+K` for toggling the captured cursor
- a short explanation that this affects the **captured** cursor, not the live one

Suggested help wording:

- `Ctrl+K` - show or hide the captured cursor in this screenshot

Tooltip guidance:

- on state: `Hide captured cursor (Ctrl+K)`
- off state: `Show captured cursor (Ctrl+K)`
- if no sample exists in the current capture, append a short note that the current
  image has no captured cursor available

The tooltip should carry the **action** wording. The button styling should carry
the **state** wording. The icon itself should stay semantically stable.

## Proposed Implementation Touchpoints

This document is design-only, but the likely implementation areas are:

- `src/greenflame_core/app_config.*`
  - add `capture.include_cursor`
- `src/greenflame_core/cli_options.*`
  - add `--cursor` and `--no-cursor`
- `src/greenflame/win/gdi_capture.*`
  - capture and normalize the cursor snapshot
  - provide cursor compositing helpers
- `src/greenflame/win/overlay_window.*`
  - add `Ctrl+K`
  - add the toolbar button and spacer layout
  - store per-session cursor snapshot and visibility state
- `src/greenflame/win/d2d_paint.*`
  - draw captured cursor between screenshot and annotations
- `src/greenflame/win/win32_services.*`
  - apply cursor compositing in save/copy/CLI paths as needed
- `src/greenflame_core/app_services.*`
  - flow cursor policy through direct clipboard capture paths if those remain on
    a separate service boundary
- `README.md`
  - document the new toggle, hotkey, config, and CLI switches
- `docs/manual_test_plan.md`
  - add end-to-end validation cases

## Testing Impact

If the implementation adds or changes code in `greenflame_core`, automated tests
are required under the repository rules.

Expected automated coverage:

- config parse and serialize tests for `capture.include_cursor`
- CLI parse tests for:
  - `--cursor`
  - `--no-cursor`
  - mutual exclusion
  - incompatibility with `--input`

Win32-only rendering and OS cursor acquisition are not good unit-test targets in
`greenflame_core`. Those should be covered in the human test plan.

Recommended manual coverage additions:

- toggle on/off with toolbar button after region selection
- toggle on/off with `Ctrl+K`
- persistence across multiple captures
- persistence across application restarts
- save and clipboard output with cursor shown
- save and clipboard output with cursor hidden
- direct tray window/monitor/desktop copy with cursor shown
- direct tray window/monitor/desktop copy with cursor hidden
- mixed-DPI multi-monitor placement
- negative-coordinate monitor placement
- cursor partially intersecting the crop boundary
- text I-beam cursor
- resize cursors
- crosshair cursor
- wait/busy cursor
- a custom application cursor if available
- `--cursor` and `--no-cursor` CLI overrides
- validation failure for `--input --cursor` and `--input --no-cursor`

## Recommended Decisions Summary

To remove ambiguity before implementation, this document recommends:

- config key: `capture.include_cursor`
- default value: `false`
- interactive overlay always captures cursor metadata when possible, even if the
  current setting is off
- toolbar button changes both the current overlay state and the persisted default
- captured cursor remains outside the annotation model
- cursor compositing happens before annotation compositing
- direct tray and hotkey clipboard captures honor the persisted config value
- CLI overrides are transient and rejected for `--input`
- missing or invalid cursor samples should fail safe by omitting the cursor rather
  than drawing it in an obviously wrong location

## Open Questions

These are the main design questions that implementation should confirm early:

- whether the current Win32 cursor-capture path can reliably normalize mask-based
  cursors and alpha cursors through one helper without quality loss
- whether the overlay should expose any stronger visual hint than tooltip text when
  the current capture has no cursor sample
- whether window-capture backends need backend-specific cursor handling, or whether
  one desktop-relative cursor path is sufficient for the first iteration

None of these questions block the core design direction in this document.
