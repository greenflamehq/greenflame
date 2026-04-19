---
title: Text Style Wheel Redesign
summary: Replaces the combined 12-segment text style wheel with a modal hub-and-ring design.
audience: contributors
status: implemented
owners:
  - core-team
last_updated: 2026-03-14
tags:
  - overlay
  - annotations
  - text
  - proposal
supersedes: text_annotation_design.md §Right-click wheel
---

# Text Style Wheel Redesign

This document is an implementation handoff.  It supersedes the **Right-click wheel**
section of [docs/text_annotation_design.md](text_annotation_design.md) and replaces it
entirely.  Everything else in `text_annotation_design.md` stays in force.

---

## What Is Changing

The current text style wheel packs all 8 annotation colors plus all 4 font choices
into a single 12-segment ring.  This design replaces that layout with a two-layer
**hub-and-ring** wheel:

- A center **hub** contains two mode buttons — one for colors and one for fonts.  Each
  button is a **circular segment** (not a true semicircle): a vertical gap of
  `kTextWheelHubGapPx` is subtracted from the hub disk, and the curved outer edge of
  each button is further inset `kTextWheelHubRingGapPx` from the inner edge of the outer
  ring, so the hub buttons never touch the ring.
- The outer **ring** shows the choices for whichever mode is currently active in the hub.
- Pressing a hub button switches the ring content.  It does not close the wheel or
  apply any selection.
- Pressing a ring segment applies that selection and closes the wheel.

The outer ring in color mode mirrors the standard annotation selection wheel exactly.  If
the palette size ever changes, the color mode ring reflects it automatically.

---

## Visual Layout

```
         ┌───────────────────────────────┐
         │          outer ring           │
         │   ┌───────────────────┐       │
         │   │ hub (semi-circles)│       │
         │   │   [color][font]   │       │
         │   └───────────────────┘       │
         │                               │
         └───────────────────────────────┘
```

### Outer ring

Identical geometry to the existing annotation selection wheel:

- outer diameter: `kSelectionWheelOuterDiameterPx` (currently 134 px)
- ring width: `kSelectionWheelWidthPx` (currently 24 px)
- segment gap: `kSelectionWheelSegmentGapPx` (currently 8 px)
- border and hover/selection emphasis use the shared selection-wheel renderer and its
  current border/inflation constants

In **color mode** the ring shows exactly the annotation color palette (currently 8
segments) at the same geometry produced by `Get_selection_wheel_segment_geometry`.

In **font mode** the ring shows 4 font-choice segments at the same geometry produced by
`Get_selection_wheel_segment_geometry` for 4 segments.  Each segment has a neutral fill
(`kOverlayButtonFillColor`) with the letter `A` rendered in the corresponding
font family, centered at the segment midpoint — identical to the existing font-segment
rendering already in `Draw_selection_wheel`.

### Center hub

Define the two key radii:

- `inner_radius = (kSelectionWheelOuterDiameterPx / 2) − kSelectionWheelWidthPx` — the inner
  boundary of the outer ring (currently 43 px).
- `hub_r = inner_radius − kTextWheelHubRingGapPx` — the radius of the hub button arcs
  (currently 35 px).

The `kTextWheelHubRingGapPx` gap separates the curved outer edge of each hub button
from the inner edge of the outer ring, so they never touch.

Each hub button is a **circular segment with an offset chord**, constructed as follows:

1. Take a circle of radius `hub_r` centered at `center`.
2. Remove a vertical rectangle of width `kTextWheelHubGapPx` centered on `center`
   (extending past the top and bottom of the circle).
3. The left piece and right piece are the two hub buttons.

Each button has:
- A **straight edge**: the vertical chord at `x = center.x ∓ kTextWheelHubGapPx/2`,
  running from `cy − chord_half_h` to `cy + chord_half_h` where
  `chord_half_h = sqrt(hub_r² − (kTextWheelHubGapPx/2)²)`.
- A **curved edge**: the major arc of the `hub_r` circle on the outer (away-from-center)
  side, subtending slightly more than 180° (because the chord is close to but not at
  the diameter).

The hub is therefore split into a left and right button by a vertical gap.

**New constants to add in `selection_wheel.h`:**

```cpp
inline constexpr float kTextWheelHubGapPx = 8.0f;
// Derived: half-gap on each side of the vertical center line.
// kTextWheelHubHalfGapPx = kTextWheelHubGapPx / 2.0f

inline constexpr float kTextWheelHubRingGapPx = 8.0f;
// Gap between the curved outer edge of each hub button and the inner edge of the
// outer ring.  hub_r = inner_radius − kTextWheelHubRingGapPx.

inline constexpr float kTextWheelHubGlyphRectWidthPx  = 20.0f;
inline constexpr float kTextWheelHubGlyphRectHeightPx = 12.0f;

inline constexpr bool kTextWheelHubDrawBorder = true;
// When true, a thin stroke (kSelectionWheelSegmentBorderWidthPx) is drawn along both
// the curved outer edge and the flat chord edge of each hub button.
// Set to false to try the borderless look.
```

**Left button — color:**

- Geometry: circular segment of radius `hub_r`, chord at `x = center.x − kTextWheelHubGapPx/2`,
  curved edge facing left (major arc).
- Fill: inactive = near-neutral light fill; active/hovered = progressively stronger
  mint-tinted fills.  The active and hovered states also get a curved-edge-only outward
  inflation, with hover stronger than active.
- Glyph: a small hue-spectrum gradient rectangle drawn in the center of the button.
  - Dimensions: `kTextWheelHubGlyphRectWidthPx` × `kTextWheelHubGlyphRectHeightPx`.
  - Center x: midpoint of the button's horizontal span = `center.x − (hub_r + kTextWheelHubGapPx/2) / 2`,
    center y: `center.y`.
  - The gradient runs left-to-right through the hue spectrum: red → yellow → green →
    cyan → blue → magenta → red (7 stops, saturation=1, lightness=0.5, full opacity).
  - Use `ID2D1LinearGradientBrush` with 7 gradient stops.  Create it once per
    `D2DOverlayResources` device creation (or on first use) and cache it.
  - When the hub half is inactive, dim the hue strip by blending it toward the current
    hub fill instead of leaving it fully saturated.

**Right button — font:**

- Geometry: circular segment of radius `hub_r`, chord at `x = center.x + kTextWheelHubGapPx/2`,
  curved edge facing right (major arc).  Mirror of the left button.
- Fill: same inactive/active/hovered treatment as the left button, mirrored.
- Glyph: the letter `A` rendered at `kSelectionWheelFontPreviewPointSize` (the
  same constant already used for ring font glyphs) in the current selected font family,
  centered in the right button (`center.x + (hub_r + kTextWheelHubGapPx/2) / 2`, `center.y`).
  This always reflects the currently active `TextFontChoice`
  so the user sees which font is selected even before opening font mode.  The inactive
  hub glyph is color-dimmed rather than flattened to gray, while the active glyph stays
  at full contrast.

**Borders and hover state for hub semi-circles:**

Both semi-circles use the same full-perimeter border language as the outer ring:

- `1.5 px` light-gray outer border
- `1 px` inset black inner border
- the borders wrap both the curved outer edge and the straight center chord edge

The hub does not use dark inverted fills or green structural outlines.  State is shown
with the lighter fill tints plus the small curved-edge-only outward inflation.

---

## State Machine

### Wheel-open modes

The wheel carries a `TextWheelMode` value that controls ring content:

```cpp
enum class TextWheelMode : uint8_t {
    Color,
    Font,
};
```

When the wheel opens it defaults to the mode last used in the current session.  The
initial value per session is `TextWheelMode::Color`.

### Hub interaction

```
Wheel visible
  │
  ├─ hover over left semi-circle  → mark left hub hovered → repaint
  ├─ hover over right semi-circle → mark right hub hovered → repaint
  ├─ hover over outer ring seg    → mark segment hovered → repaint
  │
  ├─ left-click left semi-circle  → set mode = Color → repaint (wheel stays open)
  ├─ left-click right semi-circle → set mode = Font  → repaint (wheel stays open)
  │
  ├─ left-click color ring seg    → apply color → close wheel
  ├─ left-click font ring seg     → apply font  → close wheel
  │
  ├─ Esc                          → close wheel without changes
  └─ draft started                → close wheel without changes
```

Clicking a hub button that is already the active mode is a no-op (idempotent).

### New hub hit-test function

Add to `selection_wheel.h` / `selection_wheel.cpp`:

```cpp
enum class TextWheelHubSide : uint8_t {
    Color, // left button
    Font,  // right button
};

// Returns which hub button was hit, or nullopt if the point is outside
// the hub (in the gap, beyond hub_r, or in the ring-gap annulus between hub_r and inner_radius).
[[nodiscard]] std::optional<TextWheelHubSide>
Hit_test_text_wheel_hub(PointPx center, PointPx point) noexcept;
```

Implementation rules:

Let `hub_r = kSelectionWheelOuterDiameterPx / 2 − kSelectionWheelWidthPx − kTextWheelHubRingGapPx`.

1. Compute `d = distance(point, center)`.
2. If `d > hub_r` → return `nullopt` (point is in the ring-gap, ring, or outside).
3. If `point.x >= center.x − kTextWheelHubGapPx/2` and
   `point.x <= center.x + kTextWheelHubGapPx/2` → return `nullopt` (gap).
4. If `point.x < center.x − kTextWheelHubGapPx/2` → return `TextWheelHubSide::Color`.
5. Otherwise → return `TextWheelHubSide::Font`.

---

## Code Changes

### `src/greenflame_core/selection_wheel.h`

- Add `kTextWheelHubGapPx`, `kTextWheelHubRingGapPx`, `kTextWheelHubGlyphRectWidthPx`,
  `kTextWheelHubGlyphRectHeightPx`, `kTextWheelHubDrawBorder`.
- Add `TextWheelMode` enum.
- Add `TextWheelHubSide` enum.
- Add `Hit_test_text_wheel_hub` declaration.

### `src/greenflame_core/selection_wheel.cpp`

- Implement `Hit_test_text_wheel_hub`.

### `src/greenflame/win/d2d_paint.h` — `D2DPaintInput`

Replace:

```cpp
bool selection_wheel_is_text_style = false;
std::array<std::wstring_view, 4> selection_wheel_font_families = {};
```

With:

```cpp
bool selection_wheel_has_text_hub = false;
core::TextWheelMode text_wheel_active_mode = core::TextWheelMode::Color;
std::optional<core::TextWheelHubSide> text_wheel_hovered_hub = std::nullopt;
std::wstring_view text_wheel_hub_font_family = {};   // font for right-hub "A" glyph
std::array<std::wstring_view, 4> selection_wheel_font_families = {};
```

`selection_wheel_segment_count` is set by the caller to the count for the active mode:
- Color mode: `kAnnotationColorSlotCount` (currently 8).
- Font mode: 4.

`selection_wheel_selected_segment` carries the active color index in color mode and
the active font index in font mode (use `Text_font_choice_index()` to convert).

`selection_wheel_hovered_segment` applies to the outer ring only; hub hover is tracked
separately via `text_wheel_hovered_hub`.

### `src/greenflame/win/d2d_overlay_resources.h` / `.cpp`

Add a cached `ID2D1LinearGradientBrush` for the hue spectrum used in the color hub
glyph:

```cpp
Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> text_wheel_hue_brush;
```

Create it in `Create_shared_resources` alongside other shared brushes.  The 7 gradient
stops and their RGBA values:

| Position | Color   | R   | G   | B   |
|----------|---------|-----|-----|-----|
| 0/6      | Red     | 255 |   0 |   0 |
| 1/6      | Yellow  | 255 | 255 |   0 |
| 2/6      | Green   |   0 | 255 |   0 |
| 3/6      | Cyan    |   0 | 255 | 255 |
| 4/6      | Blue    |   0 |   0 | 255 |
| 5/6      | Magenta | 255 |   0 | 255 |
| 6/6      | Red     | 255 |   0 |   0 |

All stops at alpha = 1.0.  The gradient start point is the left edge of the glyph
rectangle and end point is the right edge; set these at draw time via
`SetStartPoint` / `SetEndPoint` on the cached brush before filling.

### `src/greenflame/win/d2d_paint.cpp` — `Draw_selection_wheel`

When `input.selection_wheel_has_text_hub` is `true`:

**A. Draw the outer ring first** using the existing arc-segment loop, but driven by
`text_wheel_active_mode`:

- Segment content: color slots in color mode (existing fill logic); font glyphs in font
  mode (existing font-segment logic, `is_font_segment = true` for all segments).
- `color_segment_count` = `input.selection_wheel_segment_count` (already set correctly by
  caller).
- Selection / hover halos on the ring use the existing `draw_halo` lambda.
- The font selection halo in font mode: use `draw_halo` at the active font index
  (reuse the existing block that calls `Text_font_choice_index`).
- In color mode, suppress the font selection halo entirely.

**B. Draw the center hub** after the outer ring (so it paints over the inner edge of
the ring):

Draw two filled circular-segment path geometries.

Let:
- `hub_r     = inner_radius − kTextWheelHubRingGapPx`
- `half_gap  = kTextWheelHubGapPx / 2`
- `chord_h   = sqrt(hub_r * hub_r − half_gap * half_gap)`  (half-height of the chord)

*Left button path:*

```
StartFigure at (cx − half_gap, cy − chord_h)        // top of chord
ArcTo       at (cx − half_gap, cy + chord_h)         // bottom of chord
  radius (hub_r, hub_r), small arc, counter-clockwise  // sweeps around the left side
LineTo      (cx − half_gap, cy − chord_h)            // straight back up (the chord)
EndFigure closed
```

*Right button path:* mirror — chord at `x = cx + half_gap`, arc sweeps around the right
side (clockwise).

Fill each path with the appropriate idle or active fill color.  If
`kTextWheelHubDrawBorder` is `true`, draw a thin stroke at
`kSelectionWheelSegmentBorderWidthPx` along both the curved outer edge (arc path) and the
flat chord edge (straight line).

*Left button glyph (hue gradient rectangle):*

- Glyph center x = `cx − (hub_r + half_gap) / 2` (midpoint of the button's horizontal
  span from `cx − hub_r` to `cx − half_gap`), y = `cy`.
- Update `text_wheel_hue_brush` start/end points to span the rect horizontally.
- Fill the `kTextWheelHubGlyphRectWidthPx` × `kTextWheelHubGlyphRectHeightPx` rect.
- Draw a 1 px black border around the glyph rect for visibility.

*Right button glyph ("A" label):*

- Use the same DWrite layout logic already used for ring font segments.
- Font family: `input.text_wheel_hub_font_family`.
- Point size: `kSelectionWheelFontPreviewPointSize`.
- Glyph center x = `cx + (hub_r + half_gap) / 2`, y = `cy`.
- Draw in black, with the inactive state dimmed by blending that black toward the
  current hub fill.  The ring font-segment `A` glyphs use the same black content color.

*Hub hover tint:*

If `input.text_wheel_hovered_hub` has a value, draw a `kBorderColor` at 20 % alpha
rectangle covering the hovered button's bounding box, clipped by the same circular-
segment path.  Use `ID2D1RenderTarget::PushLayer` with a `D2D1_LAYER_PARAMETERS`
using the circular-segment geometry as the geometric mask.

### `src/greenflame/win/overlay_window.h` — `SelectionWheelState`

```cpp
struct SelectionWheelState final {
    bool visible = false;
    core::PointPx center = {};
    std::optional<size_t> hovered_segment = std::nullopt;
    core::TextWheelMode text_mode = core::TextWheelMode::Color;
    std::optional<core::TextWheelHubSide> hovered_hub = std::nullopt;
};
```

The `text_mode` field persists for the lifetime of the overlay session (reset on
`Reset_for_session`).  It is not saved to config.

### `src/greenflame/win/overlay_window.cpp`

**Opening the wheel (right-click with Text tool armed):**

- Keep existing logic to set `selection_wheel_.visible`, `selection_wheel_.center`.
- Do NOT reset `selection_wheel_.text_mode`; it carries over from the previous open.
- Clear `selection_wheel_.hovered_segment` and `selection_wheel_.hovered_hub`.

**Building `D2DPaintInput` (in `Build_paint_input` or equivalent):**

```cpp
bool const is_text_tool =
    controller_.Active_annotation_tool() == core::AnnotationToolId::Text;
input.selection_wheel_has_text_hub = is_text_tool;

if (is_text_tool) {
    input.text_wheel_active_mode = selection_wheel_.text_mode;
    input.text_wheel_hovered_hub = selection_wheel_.hovered_hub;
    input.text_wheel_hub_font_family =
        Resolve_text_font_families(config_)[
            core::Text_font_choice_index(controller_.Text_current_font())];

    if (selection_wheel_.text_mode == core::TextWheelMode::Color) {
        input.selection_wheel_segment_count = core::kAnnotationColorSlotCount;
        input.selection_wheel_selected_segment = Current_annotation_color_index();
    } else {
        input.selection_wheel_segment_count = 4;
        input.selection_wheel_selected_segment =
            core::Text_font_choice_index(controller_.Text_current_font());
    }
    input.selection_wheel_hovered_segment = selection_wheel_.hovered_segment;
} else {
    // Non-text tools: existing logic unchanged.
    input.selection_wheel_segment_count = Current_selection_wheel_segment_count();
    input.selection_wheel_selected_segment = Current_annotation_color_index();
    input.selection_wheel_hovered_segment = selection_wheel_.hovered_segment;
}
```

**Mouse-move while wheel is visible (hit-testing):**

When `selection_wheel_.visible` and `is_text_tool`:

1. Call `Hit_test_text_wheel_hub(selection_wheel_.center, cursor_client)`.
2. If it returns a side: set `selection_wheel_.hovered_hub`, clear `selection_wheel_.hovered_segment`.
3. Else: clear `selection_wheel_.hovered_hub`, run the existing
   `Hit_test_selection_wheel_segment` against the ring with the current mode's segment count.

When `selection_wheel_.visible` and not `is_text_tool`: existing behavior unchanged.

**Left-click while wheel is visible:**

When `selection_wheel_.visible` and `is_text_tool`:

1. Test hub first: `Hit_test_text_wheel_hub(selection_wheel_.center, cursor_client)`.
2. If left hub: `selection_wheel_.text_mode = TextWheelMode::Color` → repaint, do NOT close.
3. If right hub: `selection_wheel_.text_mode = TextWheelMode::Font` → repaint, do NOT close.
4. Else: test ring with current mode's segment count.
   - Color mode hit: `controller_.Set_annotation_color(...)`, close wheel, persist config.
   - Font mode hit: `controller_.Set_text_current_font(...)`, close wheel, persist config.

Clicking outside both the hub and ring (no hit) closes the wheel without changes,
consistent with current behavior.

---

## Geometry Reference Diagram

```
                           outer_radius = 67 px
                    ┌──────────────────────────────┐
                    │         outer ring           │
                    │    inner_radius = 43 px      │
                    │                              │
                    │    ← 8 px ring gap →         │
                    │                              │
          ┌─────────┼──────────────────────────────┼─────────┐
          │         │                              │         │
          │  color  │   ←4px gap→  ←4px gap→      │  font   │
          │ button  │      hub_r = 35 px           │ button  │
          │         │                              │         │
          └─────────┼──────────────────────────────┼─────────┘
                    │                              │
                    └──────────────────────────────┘
```

The hub buttons are circular segments (not semicircles): their curved edge has radius
`hub_r = 35 px`, set in `kTextWheelHubRingGapPx` away from the ring's inner edge.
The flat chord edges are shorter than the full diameter because the gap cuts the circle
off-center.  Numbers based on current constants.  All values in physical pixels.

---

## Non-Goals For This Change

- Changing any behavior of the standard annotation selection wheel (non-text tools).
- Adding a third hub mode or any other mode.
- Persisting `text_mode` across sessions (session-local only).
- Animating the mode transition.

---

## Testing Plan

### Automated unit tests

Add to `tests/selection_wheel_tests.cpp`:

- `Hit_test_text_wheel_hub` returns `nullopt` for a point outside `hub_r`
  (`inner_radius − kTextWheelHubRingGapPx`).
- `Hit_test_text_wheel_hub` returns `nullopt` for a point in the gap
  (`|x − cx| ≤ kTextWheelHubGapPx/2`).
- `Hit_test_text_wheel_hub` returns `Color` for a point in the left half.
- `Hit_test_text_wheel_hub` returns `Font` for a point in the right half.
- `Hit_test_text_wheel_hub` returns `nullopt` for center point (inside gap).

### Manual test additions

Add to `docs/manual_test_plan.md` under the Text tool section:

- Open text style wheel (right-click while Text tool is armed, no draft active).
  - Verify center hub shows two circular-segment buttons with a visible vertical gap
    between them and a visible gap between the buttons and the outer ring.
  - Verify the hub buttons do NOT touch the outer ring segments.
  - Verify left hub shows a hue-spectrum gradient rectangle, and that the inactive
    strip is visibly dimmer than the active one.
  - Verify right hub shows `A` in the current font.
  - Verify outer ring initially shows the 8 annotation colors (color mode).
  - Toggle `kTextWheelHubDrawBorder` to `false` and rebuild; verify curved edge stroke
    disappears.  Reset to `true` for normal use.
- Hover over left hub: verify hover tint; outer ring unchanged.
- Hover over right hub: verify hover tint; outer ring unchanged.
- Click left hub: verify left hub becomes active (inverted fill); outer ring still
  shows 8 color segments; wheel stays open.
- Click right hub: verify right hub becomes active; outer ring changes to 4 font
  segments; wheel stays open.
- While in font mode: hover a font segment; click it; verify font changes; wheel closes.
- While in color mode: hover a color segment; click it; verify color changes; wheel closes.
- Close wheel with `Esc`: verify no changes to color or font.
- Open wheel again: verify it opens in the mode last used (font mode if font was last
  switched to).
- Verify no style wheel while editing (draft active).
- Verify the right-hub `A` glyph updates after a font selection (re-open wheel).
- Add a color to the palette (future: if palette grows beyond 8) and verify color mode
  automatically shows the new count without any code change.
