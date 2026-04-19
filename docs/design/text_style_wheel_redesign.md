---
title: Text Style Wheel
summary: Current reference for the hub-and-ring style wheel used by the Text and Bubble tools.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - annotations
  - text
  - bubble
  - selection-wheel
supersedes: text_annotation_design.md §Right-click wheel
---

# Text Style Wheel

This document describes the current hub-and-ring style wheel used by the Text and
Bubble tools.

Shared wheel navigation rules such as `Tab`, `Enter`, hover precedence, and
mouse-wheel navigation live in
[wheel_keyboard_navigation.md](wheel_keyboard_navigation.md). This document
covers the Text/Bubble-specific hub, mode model, and selection behavior.

## Current Status

The Text and Bubble tools use the same two-mode style wheel:

- `Color`
- `Font`

Current behavior:

- the wheel opens in `Color` mode
- the center hub switches between `Color` and `Font`
- choosing a ring segment applies that selection and closes the wheel
- dismissing the wheel resets the next open back to `Color`

The same hub-and-ring renderer is shared by both tools. The selected font source
depends on the active tool:

- Text uses `tools.text.current_font`
- Bubble uses `tools.bubble.current_font`

## Ring Layout

### Color mode

Color mode uses the shared annotation palette.

Current behavior:

- eight ring segments, one per annotation color slot
- same geometry and visual metrics as the standard annotation color wheel
- selected color comes from the shared annotation color index

### Font mode

Font mode uses four font-choice segments.

Current behavior:

- four ring segments
- each segment uses a neutral fill
- each segment shows the letter `A` in its corresponding font family
- the selected segment reflects the active tool's current font choice

The current font choices are:

- `Sans`
- `Serif`
- `Mono`
- `Art`

## Hub Behavior

The style wheel hub has two halves:

- left half: `Color`
- right half: `Font`

Current interaction rules:

- hub halves are pointer-only
- clicking the inactive half switches ring mode and keeps the wheel open
- clicking the already-active half is a no-op
- `Tab` also toggles between `Color` and `Font` while the wheel is visible

Current visuals:

- left hub glyph is a hue strip
- right hub glyph is an `A` rendered in the current tool font
- active and hovered hub states use the shared hub visual metrics from
  `selection_wheel.*`

## Open And Reset Behavior

The wheel does not preserve `Font` mode across open/close cycles.

Current implementation details:

- showing the wheel keeps the selection-wheel state object for the current open
- dismissing the wheel resets `text_mode` back to its default value
- reopening the wheel therefore starts in `Color` mode again

This is current behavior for both Text and Bubble.

## Selection Semantics

Selecting a segment applies immediately and closes the wheel.

Mode-specific results:

- `Color` mode updates the shared annotation color selection
- `Font` mode updates `text_current_font` when Text is active
- `Font` mode updates `bubble_current_font` when Bubble is active

Changes are normalized and persisted to app config.

## Source Locations

Current implementation is split across:

- `src/greenflame_core/selection_wheel.h`
  - text-wheel enums
  - hub hit-test declaration
  - shared wheel constants
- `src/greenflame_core/selection_wheel.cpp`
  - text hub hit-testing
- `src/greenflame/win/overlay_window.cpp`
  - mode switching
  - tool-specific font selection
  - wheel-open and wheel-dismiss behavior
  - segment selection and config persistence
- `src/greenflame/win/d2d_paint.h`
  - style-hub paint input fields
- `src/greenflame/win/d2d_paint.cpp`
  - ring drawing
  - hub drawing
  - hue-strip and font glyph rendering

## Test Coverage

Current automated coverage includes:

- `tests/selection_wheel_tests.cpp`
  - text hub hit-testing
  - shared wheel geometry and segment hit-testing

Manual coverage lives in:

- [docs/manual_test_plan.md](../manual_test_plan.md)

## Related Documents

- [wheel_keyboard_navigation.md](wheel_keyboard_navigation.md): shared wheel input rules
- [docs/annotation_tools.md](../annotation_tools.md): authoritative annotation-tool behavior
- [README.md](../../README.md): user-facing shortcut and configuration docs
