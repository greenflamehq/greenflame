---
title: Wheel Keyboard Navigation
summary: Current reference for keyboard and mouse-wheel navigation of the overlay selection wheel.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - selection-wheel
  - keyboard
  - input
---

# Wheel Keyboard Navigation

This document describes the current keyboard and mouse-wheel behavior for the
overlay selection wheel.

For user-visible shortcut wording, [README.md](../../README.md) is the public
reference. This document focuses on the current input model in
`src/greenflame/win/overlay_window.cpp`.

## Current Status

Wheel navigation is implemented.

- `Tab` shows the wheel when it is hidden.
- `Tab` cycles wheel views while the wheel is visible for multi-view tools.
- `Up`, `Down`, and mouse-wheel input navigate ring segments.
- `Enter` confirms the effective segment and dismisses the wheel.
- Hub buttons remain pointer-only.

## Supported Inputs

While the wheel is visible:

- mouse-wheel up: navigate counter-clockwise
- mouse-wheel down: navigate clockwise
- `Up`: navigate counter-clockwise
- `Down`: navigate clockwise
- `Enter`: select the effective segment, then dismiss the wheel
- `Escape`: dismiss the wheel without changing the selection

While the wheel is hidden:

- bare `Tab`: show the wheel at the current cursor position

## Hover Model

The wheel uses two segment-hover sources:

- `mouse_hovered_segment`
- `nav_hovered_segment`

Hover precedence is:

1. `nav_hovered_segment`
2. `mouse_hovered_segment`
3. the currently selected segment

This means keyboard or mouse-wheel navigation temporarily overrides mouse hover
until the pointer enters a ring segment again.

## Mouse Interaction Rules

Current pointer behavior is slightly narrower than the earlier proposal:

- entering a ring segment clears `nav_hovered_segment`
- moving the mouse without entering a ring segment does not clear `nav_hovered_segment`
- hovering a hub updates hub hover state, but does not create a keyboard-navigable
  target

As a result, keyboard navigation remains in control until the pointer actively
re-enters the ring.

## Initial State When Shown

Showing the wheel resets:

- `mouse_hovered_segment`
- `nav_hovered_segment`
- hub hover state
- scroll-delta remainder

The wheel does not immediately recompute hover from the stationary cursor when it
opens. Until the mouse moves or navigation input occurs, the effective segment is
the currently selected segment for the active wheel view.

## `Tab` Behavior

### When hidden

Bare `Tab` shows the wheel at the current cursor position, using the same entry
path as the explicit wheel-open action.

### When visible

Bare `Tab` cycles views only for tools with multiple wheel views:

- Text
- Bubble
- Highlighter

View switching clears both segment-hover sources and the relevant hub hover state.

Current view switching is:

- Text and Bubble: `Color` <-> `Font`
- Highlighter: `Color` <-> `Opacity`

Single-view tools ignore `Tab` while the wheel is visible.

## Navigation Model

### Standard ring views

For ordinary color and font rings, navigation wraps.

Examples:

- moving past the last segment wraps to segment `0`
- moving backward from segment `0` wraps to the last segment

### Highlighter opacity view

The highlighter opacity view uses clamped navigation rather than wraparound.

Implementation details:

- the rendered layout includes one phantom slot to keep the wheel geometry aligned
- navigation excludes that phantom slot
- `Up` and `Down` clamp at the first and last real preset

This is the only current wheel view that uses clamped navigation.

## `Enter` Behavior

Pressing `Enter` while the wheel is visible:

1. resolves the effective segment using the current hover-precedence rules
2. applies that segment through `Select_wheel_segment(...)`
3. dismisses the wheel

Because the effective segment falls back to the currently selected segment, `Enter`
can reaffirm the existing selection even when no explicit hover is active.

## Scope Limits

The current implementation does not provide keyboard navigation for hub buttons.

That applies to:

- the Text/Bubble center hub
- the Highlighter center hub

Those mode switches remain pointer-only.

## Source Locations

Current behavior is split across:

- `src/greenflame/win/overlay_window.cpp`
  - view switching
  - hover precedence
  - wheel navigation
  - `Tab`, `Enter`, `Up`, `Down`, and mouse-wheel routing
- `src/greenflame_core/selection_wheel.*`
  - segment geometry
  - ring hit-testing
  - hub hit-testing
  - clamped-navigation angle helpers
- `tests/selection_wheel_tests.cpp`
  - geometry and hit-testing coverage for ring and hub layouts

## Related Documents

- [README.md](../../README.md): user-facing shortcut documentation
- [docs/annotation_tools.md](../annotation_tools.md): authoritative annotation-tool behavior
