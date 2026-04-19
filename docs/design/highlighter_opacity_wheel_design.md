---
title: Highlighter Opacity Wheel
summary: Current reference for the Highlighter tool's color and opacity wheel modes.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - annotations
  - highlighter
  - selection-wheel
---

# Highlighter Opacity Wheel

This document describes the current highlighter-specific behavior of the overlay
selection wheel.

Shared wheel navigation rules such as `Tab`, `Enter`, hover precedence, and
mouse-wheel navigation live in
[wheel_keyboard_navigation.md](wheel_keyboard_navigation.md). This document
covers only the Highlighter tool's extra mode, preset model, and rendering
behavior.

## Current Status

The Highlighter tool uses a two-mode selection wheel:

- `Color`
- `Opacity`

The wheel opens in `Color` mode. While it is visible, the center hub can switch
between the two modes. Dismissing the wheel resets it back to `Color` mode for
the next open.

## Preset Model

Highlighter opacity is stored as an `int32_t` percentage in the existing
`0..100` range.

Current default and presets come from `selection_wheel.h`:

```cpp
inline constexpr int32_t kDefaultHighlighterOpacityPercent = 33;
inline constexpr std::array<int32_t, 5> kHighlighterOpacityPresets = {
    {75, 66, 50, 33, 25}};
```

Meaning:

- default opacity is `33`
- the wheel exposes five fixed presets
- the selected preset persists through `tools.highlighter.opacity_percent`

## Ring Layout

### Color mode

Color mode uses the Highlighter tool's six-slot palette.

Current behavior:

- one ring segment per highlighter color slot
- segments are painted at full opacity
- the currently selected color gets the selected-segment treatment

The ring intentionally ignores the active opacity value in this mode so the user
can read hue choices clearly.

### Opacity mode

Opacity mode uses the current highlighter color plus the five fixed opacity
presets.

Current behavior:

- ring segments are painted over a checker background
- the checker brush is anchored to the wheel center so the pattern stays aligned
  across segments
- each segment overlays the current highlighter color at the preset opacity
- the selected preset is resolved by nearest preset, not by requiring an exact
  stored value

Preset order is symmetric around the top:

| Wheel position | Opacity % |
| --- | --- |
| Leftmost | 75 |
| Second from left | 66 |
| Top | 50 |
| Second from right | 33 |
| Rightmost | 25 |

## Clamped Navigation Detail

Opacity mode is the only current wheel view that uses clamped navigation instead
of wraparound.

Implementation detail:

- the rendered ring layout includes one phantom slot
- that phantom slot is not drawn
- keyboard and mouse-wheel navigation clamp to the five real presets

This keeps the top-centered preset layout while still giving the user a clean
linear preset progression from more opaque on the left to more transparent on the
right.

## Hub Behavior

The Highlighter wheel uses a two-half center hub:

- left half: `Color`
- right half: `Opacity`

Current interaction rules:

- hub halves are pointer-only
- clicking the inactive half switches wheel mode and keeps the wheel open
- clicking the already-active half is a no-op
- `Tab` also switches between `Color` and `Opacity` while the wheel is visible

Current visuals:

- left hub glyph is a hue strip
- right hub glyph is a generated opacity strip using black alpha bands
- active and hovered hub states use the same shared hub visual metrics as the
  text and bubble style wheels

## Selection Semantics

Selecting a highlighter wheel segment immediately applies the chosen value and
closes the wheel.

Current mode-specific results:

- `Color` mode updates `current_highlighter_color_index`
- `Opacity` mode updates `highlighter_opacity_percent`

Both changes are normalized and persisted to app config.

## Source Locations

Current implementation is split across:

- `src/greenflame_core/selection_wheel.h`
  - default opacity constant
  - opacity preset array
  - highlighter wheel enums
  - highlighter hub hit-test declaration
- `src/greenflame_core/selection_wheel.cpp`
  - highlighter hub hit-testing
- `src/greenflame/win/overlay_window.cpp`
  - mode switching
  - selected preset resolution
  - segment selection and config persistence
  - clamped navigation setup for opacity mode
- `src/greenflame/win/d2d_paint.h`
  - highlighter wheel paint input fields
- `src/greenflame/win/d2d_paint.cpp`
  - highlighter ring painting
  - checker-backed opacity segments
  - highlighter hub glyph rendering

## Test Coverage

Current automated coverage includes:

- `tests/selection_wheel_tests.cpp`
  - highlighter hub hit-testing
  - shared wheel geometry and ring hit-testing

Manual coverage lives in:

- [docs/manual_test_plan.md](../manual_test_plan.md)

## Related Documents

- [wheel_keyboard_navigation.md](wheel_keyboard_navigation.md): shared wheel input rules
- [docs/annotation_tools.md](../annotation_tools.md): authoritative annotation-tool behavior
- [README.md](../../README.md): user-facing configuration and shortcut docs
