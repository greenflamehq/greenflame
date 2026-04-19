---
title: Pinned Image Design
summary: Adds a pin-to-desktop export action that creates movable, zoomable
  always-on-top reference windows from the current overlay selection.
audience:
  - contributors
  - qa
status: draft
owners:
  - core-team
last_updated: 2026-04-03
tags:
  - overlay
  - pin
  - export
  - win32
  - toolbar
---

# Pinned Image Design

## Overview

Greenflame currently lets the user save or copy the selected capture result, but
it does not yet support keeping that result visible as a desktop reference.

This feature adds a **pin-to-desktop** action that exports the current overlay
selection into a separate top-level pinned-image window. The pinned window is a
read-only reference surface, not an editor. It can be moved, zoomed, rotated,
and faded for on-screen reference while other work continues underneath it.

Flameshot is the reference product for the basic interaction model, but
Greenflame will diverge in four deliberate ways:

- the halo is always visible and uses Greenflame green rather than purple
- the active pin state has a stronger halo treatment than the idle state
- keyboard shortcuts are first-class and appear in the context menu
- resizing is zoom-only; there are no border or corner resize handles

## Terminology

- **Pin action**: the overlay command that exports the current selection into a
  pinned-image window.
- **Pinned image window**: the frameless always-on-top window that displays the
  pinned bitmap and halo.
- **Active pin**: the pinned-image window that currently has focus and receives
  keyboard input.
- **Image opacity**: the alpha applied to the pinned bitmap itself.
- **Halo opacity**: the alpha applied to the surrounding green halo. Halo state
  is independent from image opacity.

This document uses **active pin** rather than **selected pin** to avoid
confusion with annotation selection inside the overlay.

## Goals

- Let the user pin the current rendered selection to the desktop from the
  interactive overlay.
- Reuse the same final-render pipeline as save and copy so the pinned bitmap
  matches exported output exactly.
- Keep all pinned-image geometry in physical pixels.
- Support multiple independent pinned-image windows in the long-lived app
  process.
- Keep the halo always visible, with a stronger active state.
- Provide both pointer interaction and keyboard shortcuts for pinned-image
  actions.
- Keep the first implementation small and predictable.

## Non-goals

- Turning a pinned image into an annotation surface or mini editor.
- Adding border drag, corner drag, or resize handles.
- Persisting pinned images across app restart.
- Adding CLI pinning, tray-menu pinning, or direct-capture pinning in v1.
- Adding click-through, docking, snapping, or desktop-widget behavior in v1.
- Adding a separate pin-management UI beyond the per-pin context menu.

## Flameshot Reference

The Flameshot research produced a useful baseline:

- the pin tool itself is thin; the real work happens in export handling and in a
  long-lived owner that hosts pin windows
- each pin is a separate frameless top-level window
- drag-to-move, wheel zoom, and a right-click menu are the primary interactions
- copy, save, rotate, opacity, and close actions belong to the pin window, not
  to the original capture UI

Greenflame should keep those broad ideas, but intentionally diverge here:

- use Greenflame green for the halo, not Flameshot purple
- make the halo always visible, not subtle enough to disappear into the source
  image
- expose a distinct active state with stronger halo opacity
- add keyboard shortcuts for pin-window actions
- use zoom-only resizing rather than border hit-testing
- avoid a separate daemon or IPC layer because Greenflame already has a
  long-lived tray app process

## Current Greenflame Baseline

Relevant current behavior:

- the interactive overlay already owns the capture bitmap, selection state, and
  copy/save output plumbing
- annotations are rendered into the final exported bitmap after capture
- the captured-cursor feature already established a toolbar utility button that
  is not an annotation tool
- the current toolbar layout is:
  `[annotation tools] [spacer] [cursor] [spacer] [help]`
- the process is Per-Monitor DPI Aware v2 and Greenflame's internal coordinate
  truth is physical pixels
- the app already has a long-lived Win32 shell process that can outlive the
  overlay window

This matters because the new feature should fit the existing architecture rather
than introducing a second rendering path or a second lifetime model.

## Recommended User Experience

### Overlay entry points

The overlay should expose pinning through:

- a toolbar button with tooltip `Pin to desktop (Ctrl+P)`
- the keyboard shortcut `Ctrl+P`

Rules:

- pinning is available only after a stable selection exists
- `Ctrl+P` is blocked while help, warning dialogs, or any other modal overlay UI
  is open
- activating pinning does not change the active annotation tool before the
  export happens
- on success, pinning is a terminal overlay action: create the pinned image and
  then close the overlay
- on failure, the overlay stays open and the failure is surfaced loudly through
  the normal error path

### Toolbar placement

The toolbar layout should become:

`[annotation tools] [spacer] [cursor] [pin] [spacer] [help]`

Recommendation:

- place the pin button in the same trailing utility cluster as the captured
  cursor button
- do not give pinning its own isolated block in v1
- keep the pin as its own standalone button and glyph; do not merge it into the
  cursor icon

Rationale:

- pinning is a one-shot capture-output action, not an annotation tool
- it is conceptually closer to the captured-cursor utility toggle than to the
  annotation-tool strip
- a separate block would over-emphasize the feature relative to save/copy style
  actions
- a standalone pin glyph is easier to scan than a combined cursor-plus-pin mark

### Pinned-image creation semantics

When the user pins a selection, Greenflame should create the pinned bitmap from
the exact same rendered result that save and copy would use at that moment.

That includes:

- the selected capture region
- all committed annotations currently visible in the overlay
- the captured cursor only if the captured-cursor toggle is currently on

That excludes:

- live overlay chrome
- toolbar UI
- selection borders
- annotation handles
- the green halo itself

The pinned image should open at 100% scale with its initial content bounds
aligned to the original selection's screen position in physical pixels.

### Pinned-image window behavior

Each pinned image should live in its own:

- frameless window
- always-on-top window
- tool-style window that does not create a taskbar button in v1

Pointer behavior:

- left-click activates the pin
- left-button drag anywhere on the image moves the pin
- mouse-wheel up/down zooms the image in or out
- right-click activates the pin and opens the pin context menu
- there are no border or corner resize gestures

V1 should not use double-click-to-close. That gesture is easy to trigger by
accident on a small reference window and offers little value once explicit
shortcuts and a context menu exist.

### Halo treatment

The halo is part of the pinned-image window chrome. It is always visible and is
not affected by the pin's image-opacity setting.

Recommended visual treatment:

- idle state: use Greenflame's existing accent green with a semi-transparent
  outer glow and a visible thin stroke
- active state: use the same hue family, but with materially stronger opacity
  and a slightly stronger stroke/glow so focus is unmistakable

The active-state change should come from strength and clarity, not from swapping
to a different color family.

The window should reserve a fixed halo padding around the bitmap so the glow is
not clipped.

### Focus and activation model

Pins can coexist. Exactly one pin is active at a time.

Rules:

- a newly created pin becomes the active pin
- clicking a pin activates it and raises it above other pins
- clicking another app or another pin removes active state from the previous pin
- only the active pin receives keyboard shortcuts
- opening the context menu should keep the target pin visually active while the
  menu is open

### Zoom model

Zoom is the only resize mechanism.

Recommended v1 behavior:

- default scale: `1.0`
- zoom step: multiply scale by `1.1` per wheel notch or keyboard zoom command
- zoom range: clamp to `0.25 .. 8.0`
- zoom anchor: preserve the image center during zoom

This keeps the implementation simple, DPI-safe, and easy to reason about while
still covering the main use case.

### Rotation model

Rotation is quarter-turn only:

- `Rotate Right` rotates the bitmap by `+90` degrees
- `Rotate Left` rotates the bitmap by `-90` degrees

Rotation changes the pinned image's displayed orientation and should affect later
copy/save actions from that pin. Rotation does not affect the original capture or
the overlay that produced it.

### Opacity model

Opacity is a pin-window presentation control, not an export transform.

Rules:

- opacity changes affect only the pinned bitmap
- opacity changes do not affect halo visibility or halo strength
- opacity changes do not alter the bitmap that gets copied or saved
- copy/save should use the current rotation state, but export fully opaque image
  pixels

Recommended v1 behavior:

- opacity presets: `20% .. 100%` in `10%` steps
- `Increase Opacity` and `Decrease Opacity` move one preset at a time

### Context menu

The pin context menu should preserve the Flameshot command set and grouping:

1. `Copy to clipboard`
2. `Save to file...`
3. separator
4. `Rotate Right`
5. `Rotate Left`
6. separator
7. `Increase Opacity`
8. `Decrease Opacity`
9. separator
10. `Close`

The menu should display keyboard accelerators for all actions that have them.

### Keyboard shortcuts

Pinned-image windows should support these shortcuts:

- `Ctrl+C` - copy the current rotated bitmap to the clipboard
- `Ctrl+S` - save the current rotated bitmap to a file
- `Ctrl+Right` - rotate right
- `Ctrl+Left` - rotate left
- `Ctrl+=` - zoom in
- `Ctrl+-` - zoom out
- `Ctrl+Up` - increase opacity
- `Ctrl+Down` - decrease opacity
- `Esc` - close the active pin

Notes:

- these shortcuts apply only while a pin window is active
- `Ctrl+P` belongs to the overlay pin action, not to the pin window itself
- no global app hotkeys are introduced for pin-window actions in v1

### Multiple pins

Greenflame should allow multiple pinned-image windows at the same time.

Rules:

- each pin keeps its own scale, rotation, opacity, and screen position
- closing the overlay after pin creation does not affect existing pins
- closing one pin does not affect other pins
- all pins close when the app exits

## Architecture

### Top-level ownership

Recommended ownership split:

- `greenflame_core::OverlayController`
  - decides whether the pin command is currently allowed
  - returns a dedicated terminal action such as `PinToDesktop`

- `greenflame::OverlayWindow`
  - maps toolbar input and `Ctrl+P` to the controller
  - builds the final rendered selection bitmap using the existing output path
  - forwards the finished bitmap plus initial placement data to app-level pin
    hosting

- `greenflame::GreenflameApp`
  - owns a long-lived `PinnedImageManager`
  - keeps pin windows alive after the overlay closes

- `greenflame::PinnedImageManager`
  - creates and tracks pinned-image windows
  - owns their lifetime and removes them when closed

- `greenflame::PinnedImageWindow`
  - Win32 shell for one pinned image
  - owns image presentation state such as scale, rotation, and opacity

This keeps policy in core and keeps Win32 windowing and rendering in the
platform layer, consistent with the rest of Greenflame.

### Data flow

Recommended flow:

1. The user creates a stable overlay selection.
2. The user clicks the toolbar pin button or presses `Ctrl+P`.
3. `OverlayController` returns the pin terminal action.
4. `OverlayWindow` renders the final selection bitmap through the same code path
   already used for save/copy output.
5. `OverlayWindow` forwards a pin payload to `GreenflameApp`.
6. `PinnedImageManager` creates a `PinnedImageWindow` using the payload's bitmap
   and initial physical-pixel placement.
7. If creation succeeds, the overlay closes.
8. If creation fails, the overlay remains open and surfaces the error.

### Rendering and DPI rules

Pinned-image state must follow the same coordinate rules as the rest of the app:

- physical pixels are the only geometry truth
- the initial pinned-image content rect comes directly from the overlay
  selection's physical-pixel screen rect
- halo padding is an explicit physical-pixel outset around the image
- zoom and rotation update the window bounds from physical-pixel content size;
  they do not introduce DIP storage or mixed-coordinate state
- negative virtual-desktop coordinates and cross-monitor placement must remain
  valid

The design must not copy any Qt-style logical-pixel adjustment logic from
Flameshot. Greenflame should stay consistent with its Per-Monitor DPI Aware v2
rules and its physical-pixel model.

### Rendering-path reuse

The pin feature should not introduce a new capture renderer.

It should reuse the same rendered-output pipeline already used by save and copy,
so that:

- selection cropping stays identical
- captured-cursor inclusion stays identical
- annotation compositing stays identical
- later bug fixes in the shared export path automatically benefit pinning too

### Resource and toolbar assets

The toolbar should use `resources/pin.png` as the editable source glyph and
derive a tintable alpha-mask runtime asset:

- source: `resources/pin.png`
- derived runtime asset: `resources/pin-mask.png`

Processing should follow [resource_processing.md](resource_processing.md). The
derived asset should later be embedded and cached the same way as the other
toolbar glyph masks.

Design decision:

- keep the pin glyph standalone
- do not build a combined cursor-plus-pin asset

## Testing Plan

Implementation should add automated coverage where practical and extend the
manual plan for Win32-only behavior.

### Automated coverage to add during implementation

- `OverlayController` hotkey routing for `Ctrl+P`
- toolbar action routing for the pin button
- blocked pin behavior while help or other modal overlay UI is open
- no-selection behavior for the pin action
- help-model updates so `Ctrl+P` appears in the overlay shortcut reference

### Manual coverage to add during implementation

Add dedicated pin cases to [manual_test_plan.md](manual_test_plan.md) that cover
at least:

- toolbar placement and tooltip text
- pin creation from a normal selection
- active versus idle halo visibility
- drag-to-move behavior
- wheel zoom and keyboard zoom
- rotation shortcuts and menu actions
- opacity shortcuts and menu actions
- copy/save behavior from a pin
- multiple simultaneous pins
- mixed-DPI placement, negative coordinates, and cross-monitor behavior
- parity with saved/copied output when annotations and captured cursor are
  present
- failure handling if pin creation cannot complete

The existing manual cases that will also need updates are:

- `GF-MAN-UI-002` for the new toolbar layout and tooltip
- `GF-MAN-DPI-001` for mixed-DPI pin placement checks

## Documentation Follow-up

When implementation happens, the following docs should be updated alongside the
code:

- [annotation_tools.md](annotation_tools.md) for the new toolbar layout and
  overlay shortcut
- [manual_test_plan.md](manual_test_plan.md) for pin-specific manual coverage
- any overlay keyboard-shortcut/help documentation that lists the available
  overlay actions

## Summary Of Key Decisions

- `Ctrl+P` is the overlay pin shortcut.
- The pin button belongs beside the captured-cursor button, not in its own
  isolated toolbar block.
- The pin uses its own standalone glyph.
- The pinned window is always-on-top, frameless, and read-only.
- Zoom is the only resize mechanism.
- The halo is always visible and becomes stronger when the pin is active.
- The pin context menu keeps the Flameshot action set.
- Pin-window actions gain explicit keyboard shortcuts.
- Pinning reuses Greenflame's existing final-render export path.
