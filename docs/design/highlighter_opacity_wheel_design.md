---
title: Highlighter Opacity Wheel
summary: Adds a modal hub-and-ring opacity selector to the highlighter selection wheel, mirroring the text style wheel's color/font hub pattern.
audience: contributors
status: implemented
owners:
  - core-team
last_updated: 2026-03-31
tags:
  - overlay
  - annotations
  - highlighter
  - proposal
---

# Highlighter Opacity Wheel

This document describes the design for a new **opacity mode** on the selection wheel
that appears when the highlighter tool is active.  The feature mirrors the existing
text-style hub-and-ring design ([docs/text_style_wheel_redesign.md](text_style_wheel_redesign.md))
but replaces the font mode with an opacity mode.

---

## Motivation

The highlighter currently exposes opacity as a scroll-only setting with no visual
reference.  Users have no quick way to pick a specific opacity level or see what the
other options look like against the current highlight color.  The hub-and-ring pattern
already established for text/font selection is the natural fit.

---

## Preset Opacity Values

Five fixed levels expressed as **opacity** percentages (how opaque the stroke is),
stored as `int32_t` in the existing `[0, 100]` range — no type change.  The **50 %**
level sits at the top of the wheel.  Moving **left** from center increases opacity (less
see-through); moving **right** decreases it (more see-through).

| Wheel position | Opacity % |
|---|---|
| Leftmost        | 75 % |
| Second from left | 66 % |
| Top (center)    | 50 % |
| Second from right *(default)* | 33 % |
| Rightmost       | 25 % |

**Default:** 33 %, replacing the current default of 35 %.  At 33 % the highlight is
more transparent than opaque — visually light, letting the underlying content read
through clearly.

Expose the presets and the updated default as constants in `selection_wheel.h`:

```cpp
inline constexpr int32_t kDefaultHighlighterOpacityPercent = 33;
inline constexpr std::array<int32_t, 5> kHighlighterOpacityPresets = {
    75, 66, 50, 33, 25
};
```

---

## Data Model Changes

The only change to stored data is the new default value: `kDefaultHighlighterOpacityPercent`
changes from 35 to 33.  The field type (`int32_t highlighter_opacity_percent`), JSON
key (`"opacity_percent"`), valid range (0–100), and all related accessors remain
unchanged.

---

## Hub Icon

The opacity hub icon is generated at paint time as a small rectangular strip, matching
the hue strip geometry. It is filled with black vertical bands whose alpha decreases
across the strip, so the icon reads as opacity rather than as the current color.

---

## Visual Layout

The highlighter wheel shares the same geometry as the text style wheel
(`kSelectionWheelOuterDiameterPx`, `kSelectionWheelWidthPx`, hub radii, gap constants).

```
         ┌──────────────────────────────────┐
         │           outer ring             │
         │   ┌────────────────────────┐     │
         │   │  hub (semi-circles)    │     │
         │   │  [color] | [opacity]   │     │
         │   └────────────────────────┘     │
         │                                  │
         └──────────────────────────────────┘
```

### Outer ring

**Color mode** — 6 segments, one per highlighter color slot (`kHighlighterColorSlotCount`).
Each segment is filled with the corresponding palette color at full opacity so the hues
are clearly distinguishable regardless of the active opacity level.  The active color
index gets the selection halo.

**Opacity mode** — 5 segments arranged symmetrically around the top.  The 50 % opacity
segment is centered at 12 o'clock; segments become progressively more opaque going
counter-clockwise (left) and more transparent going clockwise (right).

Each segment is rendered as two layers:

1. **Checker background** — a repeating two-tone checker tile drawn with a D2D bitmap
   brush in `WRAP` mode, with its transform anchored to the **wheel center** (not the
   segment origin).  This means the checker grid is axis-aligned and continuous across
   all five segments — the exact same tiles appear under a given segment regardless of
   where on screen the wheel is opened.  Recommended cell size: 6–8 physical pixels,
   using a neutral light/dark pair (e.g. `#999` / `#666`) that reads clearly against the
   overlay background.  The brush is created once per `D2DOverlayResources` device
   creation and cached.

2. **Color fill** — the current highlighter color drawn at the segment's opacity level
   on top of the checker, clipped to the segment arc geometry.

The active preset gets the selection halo.

### Center hub

Same construction as the text style wheel (two circular-segment buttons split by
`kTextWheelHubGapPx`).

**Left button — Color mode:**

- Glyph: hue-spectrum gradient rectangle (identical to the text wheel's color button).
- Inactive content treatment: dim the hue strip by blending it toward the current hub
  fill so the active side reads more strongly.
- Fill and borders: same quiet inner-segment treatment as the text wheel hub, with
  full light-gray and inset-black perimeter borders plus a near-neutral inactive fill,
  mint-tinted active/hovered fills, and curved-edge-only outward inflation for active
  and hovered states.

**Right button — Opacity mode:**

 - Glyph: a procedurally drawn rectangular opacity strip rendered inside the
  `kTextWheelHubGlyphRectWidthPx` × `kTextWheelHubGlyphRectHeightPx` hub glyph box.
  The strip uses black vertical bands with opacity decreasing across the mark.  The
  inactive icon is dimmed by lowering that alpha range.
- Fill and borders: same quiet inner-segment treatment as the left button.

Gap behavior is identical to the text style wheel.

---

## State Machine

### New enums (`selection_wheel.h`)

```cpp
enum class HighlighterWheelMode : uint8_t { Color, Opacity };
enum class HighlighterWheelHubSide : uint8_t { Color, Opacity };
```

These parallel `TextWheelMode` / `TextWheelHubSide` and keep the two wheels
independently typed.

### `SelectionWheelState` extension (`overlay_window.h`)

Add a `HighlighterWheelMode highlighter_mode` field alongside the existing
`TextWheelMode text_mode`.  Both fields are part of `SelectionWheelState` and reset to
their defaults (`Color`) every time the wheel is dismissed — they are not preserved
across open/close cycles and are not persisted to config.

### Wheel interaction

```
Highlighter wheel visible
  │
  ├─ hover left hub   → mark left hovered → repaint
  ├─ hover right hub  → mark right hovered → repaint
  ├─ hover ring seg   → mark seg hovered → repaint
  │
  ├─ click left hub   → set mode = Color   → repaint (wheel stays open)
  ├─ click right hub  → set mode = Opacity → repaint (wheel stays open)
  │
  ├─ click color ring seg   → apply color   → close wheel, persist config
  ├─ click opacity ring seg → apply opacity → close wheel, persist config
  │
  ├─ Esc / click outside    → close wheel, no change
  └─ draft started          → close wheel, no change
```

Clicking the already-active hub button is a no-op (idempotent).

### New hit-test function

Add to `selection_wheel.h` / `selection_wheel.cpp`:

```cpp
[[nodiscard]] std::optional<HighlighterWheelHubSide>
Hit_test_highlighter_wheel_hub(PointPx center, PointPx point) noexcept;
```

Implementation is identical to `Hit_test_text_wheel_hub` with `HighlighterWheelHubSide`
as the return type.  (The geometry is the same.)

### Active preset resolution

When the wheel opens in opacity mode, the active segment is the entry in
`kHighlighterOpacityPresets` whose value is closest to the current
`highlighter_opacity_percent`.  Ties go to the more-opaque preset (lower index).

---

## Code Changes (High-Level)

### `src/greenflame_core/selection_wheel.h`

- Update `kDefaultHighlighterOpacityPercent` to `33`.
- Add `kHighlighterOpacityPresets`.
- Add `HighlighterWheelMode` and `HighlighterWheelHubSide` enums.
- Declare `Hit_test_highlighter_wheel_hub`.

### `src/greenflame_core/selection_wheel.cpp`

- Implement `Hit_test_highlighter_wheel_hub`.

### `src/greenflame/win/overlay_window.h`

- Add `HighlighterWheelMode highlighter_mode` to `SelectionWheelState`.

### `src/greenflame/win/d2d_paint.h` — `D2DPaintInput`

- Add `bool selection_wheel_has_highlighter_hub`.
- Add `HighlighterWheelMode highlighter_wheel_active_mode`.
- Add `std::optional<HighlighterWheelHubSide> highlighter_wheel_hovered_hub`.
- Add `int32_t highlighter_wheel_current_opacity_percent` — drives segment fill and
  active halo.
- Add `COLORREF highlighter_wheel_current_color` — drives segment fill preview.

### `src/greenflame/win/d2d_overlay_resources.h` / `.cpp`

- No opacity icon asset is loaded; generate the rectangular opacity strip procedurally
  during wheel paint.
- Create and cache the checker bitmap brush for opacity segment backgrounds.

### `src/greenflame/win/d2d_paint.cpp` — `Draw_selection_wheel`

- When `input.selection_wheel_has_highlighter_hub`:
  - Draw the hub (two buttons, same geometry as text wheel).
  - Right button: draw the procedural rectangular opacity strip with state-based dimming.
  - In color mode: draw 6 color segments filled at full opacity (hues must be
    clearly readable independent of the active opacity setting).
  - In opacity mode: draw 5 opacity segments with checker + color overlay.

### `src/greenflame/win/overlay_window.cpp`

- Open highlighter wheel on right-click when highlighter tool is active (mirroring text
  tool).
- Build `D2DPaintInput` fields for highlighter hub when `active_tool == Highlighter`.
- Handle hub click → mode toggle.
- Handle ring segment click in each mode:
  - Color: `controller_.Set_highlighter_color(index)`, persist config.
  - Opacity: `controller_.Set_highlighter_opacity_percent(kHighlighterOpacityPresets[index])`,
    persist config.
- Fix Enter-key confirm ordering: `Select_wheel_segment` now runs before
  `Dismiss_selection_wheel` so that state needed by the selection logic is still intact
  when the call executes.

---

## Testing Plan

### Automated unit tests

Add to `tests/selection_wheel_tests.cpp`:

- `Hit_test_highlighter_wheel_hub` returns `nullopt` outside `hub_r`.
- `Hit_test_highlighter_wheel_hub` returns `nullopt` in the center gap.
- `Hit_test_highlighter_wheel_hub` returns `Color` for left-half point.
- `Hit_test_highlighter_wheel_hub` returns `Opacity` for right-half point.

### Manual test additions

Add to `docs/manual_test_plan.md` under the Highlighter tool section:

- Right-click while Highlighter is armed (no draft active): verify the wheel opens with
  the hub, left and right buttons visible.
- Verify left hub shows hue-spectrum gradient; right hub shows the alpha gradient glyph.
- Verify inactive hub content looks dimmer than active hub content for both the hue
  strip and the opacity icon.
- Verify the opacity icon shows the hub fill through its transparent portions rather
  than a checkerboard inside the icon bounds.
- Verify outer ring opens in color mode (6 highlighter color segments at full opacity).
- Click right hub: verify ring switches to 5 opacity segments; wheel stays open.
  - Confirm the 50 % segment is visually centered at the top.
  - Confirm segments get progressively more opaque going left and more transparent going
    right.
  - Confirm the checker background is axis-aligned and continuous across all segments
    (no discontinuity at segment boundaries).
- Click each opacity segment; verify the highlight stroke updates immediately.
- Re-open wheel: verify the active preset reflects the last selection.
- Click left hub: verify ring reverts to color mode; wheel stays open.
- Click a color segment: verify color changes; wheel closes.
- Esc closes wheel without changes.
- Verify mode resets on dismiss: close and reopen the wheel; confirm it always reopens
  in color mode (mode is not preserved across open/close cycles).
- Verify config persistence: restart the application; confirm the saved opacity is
  restored.
