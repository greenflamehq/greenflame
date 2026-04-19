---
title: Bubble Annotation Design
summary: Current reference for Greenflame's numbered bubble annotation tool in the interactive overlay.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - annotations
  - bubble
  - counter
---

# Bubble Annotation Design

This document describes the current shipped Bubble tool implementation in the
interactive overlay.

For shared annotation architecture, see [docs/annotation_tools.md](../annotation_tools.md).
For the shared Text/Bubble hub-and-ring wheel, see
[text_style_wheel_redesign.md](text_style_wheel_redesign.md).

## Current Status

Bubble annotations are shipped as numbered circular annotations with:

- a session-local auto-incrementing counter
- a dedicated Bubble size setting
- a shared color/font style wheel with the Text tool
- committed bitmap rendering for overlay paint and exported output

The original proposal in this file drifted in a few important places. Current
implementation differs as follows:

- Bubble size does not reuse Brush width. It uses `tools.bubble.size`.
- Placement is not instant-on-press commit. The tool shows a live draft on press,
  tracks the cursor while dragging, and commits on release.
- Bubble wheel behavior is shared with the current Text/Bubble wheel model,
  including reset-to-color on reopen.

## User Model

### Armed Bubble tool

Current armed-state behavior:

- `N` toggles the Bubble tool
- the cursor shows the same circular size preview family used by Brush
- mouse-wheel and `Ctrl+=` / `Ctrl+-` adjust Bubble size while the tool is armed
- right-click or bare `Tab` opens the shared Text/Bubble style wheel

Bubble size is stored as a step, not directly as a diameter.

Current mapping:

- config key: `tools.bubble.size`
- default step: `10`
- physical diameter for new bubbles: `size_step + 20`

So the default interactive bubble diameter is currently `30 px`.

### Placement flow

Bubble placement is a press-drag-release gesture.

Current behavior:

- primary press starts a live bubble draft at the cursor
- moving the pointer while the button remains down updates the draft center
- primary release commits the bubble at the latest cursor position
- `Esc` during the gesture cancels the live draft

This means the Bubble tool supports a small live repositioning gesture before
commit, even though the final annotation remains a single centered circle.

### Counter semantics

The Bubble tool owns a session-local counter in `AnnotationController`.

Current rules:

- the placed bubble shows the counter value that was current before commit
- committing a bubble increments the counter
- undoing the latest bubble add decrements the counter
- redoing that add increments the counter again
- deleting a committed bubble does not change the counter
- undoing or redoing a delete does not change the counter
- `Reset_for_session()` resets the counter to `1`

Representative sequence:

| Before | Action | After | Bubble shown |
| --- | --- | --- | --- |
| `1` | Commit bubble | `2` | `"1"` |
| `2` | Commit bubble | `3` | `"2"` |
| `3` | Undo bubble add | `2` | removed `"2"` |
| `2` | Delete `"1"` | `2` | removed `"1"` |
| `2` | Commit bubble | `3` | `"2"` |

### Style selection

Bubble style selection uses the same shared hub-and-ring wheel as the Text tool.

Current Bubble-specific style behavior:

- color comes from the shared annotation palette
- font choice comes from `tools.bubble.current_font`
- configured font families come from the shared `tools.font.*` slots
- reopening the wheel starts in `Color` mode again

Wheel navigation, hover precedence, `Tab`, `Enter`, and dismiss behavior are
documented in:

- `wheel_keyboard_navigation.md`
- `text_style_wheel_redesign.md`

### Selection and editing semantics

Committed bubbles behave like movable annotations in default mode.

Current behavior:

- bubbles can be selected and moved after commit
- there is no number re-edit workflow for interactive bubbles
- bubbles do not expose resize handles
- selected bubble body drag uses the selection frame bounds, so dragging can begin
  from transparent corner pixels inside that frame

For bubbles, the selection frame currently matches the bubble visual bounds.

## Visual Model

### Bubble fill and contrast text color

Committed bubbles rasterize as filled circles.

Current visuals:

- filled disc in the annotation color
- inner `1 px` stroke in the number color
- centered number rendered in the selected Bubble font

The number and inner stroke use `Bubble_text_color(...)`, a core helper that picks
pure black or pure white from the bubble fill color using the WCAG relative-luminance
threshold `L > 0.179`.

Representative current outcomes:

- black fill -> white text
- white fill -> black text
- pure red fill -> black text
- pure blue fill -> white text
- medium green fill -> white text

### Number sizing

The current rasterizer uses a simple digit-count heuristic.

Current behavior in `D2DTextLayoutEngine::Rasterize_bubble(...)`:

- values `1..99` use `0.55 * diameter_px`
- values `100+` use `0.38 * diameter_px`
- text is rendered with `DWRITE_FONT_WEIGHT_BOLD`
- text layout is centered and uses `DWRITE_WORD_WRAPPING_NO_WRAP`

This is intentionally simple and is the current implementation to document unless
the rasterizer changes.

## Data Model And Ownership

### Core data type

Committed bubbles use `BubbleAnnotation`, which currently stores:

- `center`
- `diameter_px`
- `color`
- `font_choice`
- optional `font_family`
- `counter_value`
- committed premultiplied BGRA bitmap fields

Interactive Bubble tool placement uses `font_choice` and leaves `font_family`
empty. The optional explicit family exists so CLI-imported bubbles can carry a
resolved family override when needed.

### Controller ownership

`AnnotationController` owns Bubble tool session state:

- `bubble_size_step_`
- `bubble_current_font_`
- `bubble_counter_`

Current Bubble build path:

1. `Build_bubble_annotation(...)` creates a `BubbleAnnotation`
2. it fills:
   - current cursor center
   - physical diameter from `bubble_size_step_ + 20`
   - shared annotation color
   - current Bubble font choice
   - current counter value
3. it calls `ITextLayoutEngine::Rasterize_bubble(...)`
4. it returns a ready-to-preview or ready-to-commit `Annotation`

### Undo command model

Bubble adds use `AddBubbleAnnotationCommand`, not plain `AddAnnotationCommand`.

Current command behavior:

- `Redo()`
  - inserts the bubble annotation
  - increments the bubble counter
- `Undo()`
  - removes the bubble annotation
  - decrements the bubble counter

Bubble deletes still use the normal `DeleteAnnotationCommand`, so delete/undo-delete
does not affect the counter.

## Rendering, Bounds, And Output

### Draft rendering

During an active Bubble gesture, the tool keeps one live draft bubble cache.

Current draft behavior:

- press builds the first draft bubble
- pointer move rebuilds the draft at the latest cursor
- stroke-style changes invalidate and rebuild the draft cache
- release commits the latest draft

### Committed rendering

Committed bubbles use a cached premultiplied BGRA bitmap.

Current raster path:

- `D2DTextLayoutEngine::Rasterize_bubble(...)` builds a `diameter x diameter` bitmap
- the render target uses the overlay target DPI
- the cached bitmap is then reused for overlay paint and exported image output

Moving a bubble updates only its `center`; the cached bitmap remains valid because
position is stored separately from bitmap content.

### Bounds and hit testing

Bubble bounds are currently derived from the center and diameter:

- visual bounds are the centered `diameter_px x diameter_px` square
- selection frame bounds match those visual bounds
- point hit testing uses circle-distance geometry rather than bitmap alpha

This is the current implementation and the expected contributor baseline.

## Configuration

The interactive Bubble tool currently depends on:

- `tools.bubble.size`
- `tools.bubble.current_font`
- `tools.font.sans`
- `tools.font.serif`
- `tools.font.mono`
- `tools.font.art`
- the shared annotation color selection in `tools.current_color`

Current defaults:

- `tools.bubble.size = 10`
- `tools.bubble.current_font = sans`

Font-family resolution is shared with the Text tool, but the active font slot is
independent.

## Existing Coverage

Key automated coverage already exists in:

- `tests/bubble_annotation_tests.cpp`
- `tests/annotation_tool_tests.cpp`
- `tests/annotation_controller_tests.cpp`
- `tests/annotation_edit_interaction_tests.cpp`
- `tests/app_config_tests.cpp`

Key manual coverage already exists in `docs/manual_test_plan.md`, especially:

- `GF-MAN-ANN-002A`
- `GF-MAN-ANN-015` for shared Text/Bubble wheel behavior

Future Bubble changes should keep wheel-specific detail in
`text_style_wheel_redesign.md` and keep shared annotation behavior in
`docs/annotation_tools.md` instead of duplicating those sections here.
