---
title: Bubble Annotation Design
summary: Proposed UX and architecture for a new auto-increment numbered bubble annotation tool.
audience: contributors
status: proposed
owners:
  - core-team
last_updated: 2026-03-15
tags:
  - overlay
  - annotations
  - bubble
  - proposal
---

# Bubble Annotation Design

This document defines the proposed behavior and implementation shape for a new
**Bubble annotation** tool — a click-to-place numbered circle annotation with an
auto-incrementing counter.

It is intended as an implementation handoff. It assumes the current annotation
architecture described in [docs/annotation_tools.md](annotation_tools.md) and the
hub-and-ring style wheel described in
[docs/text_style_wheel_redesign.md](text_style_wheel_redesign.md).

## Goals

- Add a `Bubble tool` to the existing annotation toolbar and hotkey system.
- Clicking on the overlay places a filled numbered circle; the counter increments
  automatically.
- The counter decrements on undo of a placement and is unaffected by deletion.
- The counter resets with each new capture.
- Circle size uses the shared `brush_width` setting (1–50 px).
- The right-click wheel reuses the hub-and-ring design from the Text tool, exposing
  color and font choice.
- Font and color choice persist to config independently of the Text tool settings.

## Non-Goals for V1

- Drag-to-resize or drag-to-place; placement is a single point click.
- Re-editing a placed bubble's number.
- A configurable per-session starting value other than 1.
- A separate size config key independent of `brush_width`.

## UX Specification

### Tool Activation

- New toolbar button: `Bubble tool`, placed after `Text` and before `Help`.
- Suggested hotkey: `N` (numbered annotation).
- Updated toolbar order:
  - Brush, Highlighter, Line, Arrow, Rectangle, Filled Rectangle, Text, **Bubble**, Help
- When the Bubble tool is active, the overlay draws the same anti-aliased circular
  size preview around the cursor hotspot as the Brush tool.
- Border resize handles take priority over bubble tool clicks, the same as all other
  tools.

### Placement Behavior

- Left-click places a bubble centered at the clicked point.
- The bubble displays the current counter value at the moment of the click.
- Placement is instantaneous on `mouse-down` — there is no drag gesture.
- After placement the tool remains active; the next click places another bubble.

### Size Control

- The bubble diameter equals `brush_width` in physical pixels.
- Mouse-wheel up/down and `Ctrl+=` / `Ctrl+-` step `brush_width` within `1..50`,
  identical to all other stroke tools.
- The circular cursor preview updates live as the size changes.
- The transient size-overlay label appears on size change, as with other tools.
- Note: at very small sizes (≤ 4 px) the number cannot render legibly inside the
  circle. This is accepted in V1 without a minimum-size enforcement.

### Right-Click Style Wheel

While the Bubble tool is armed, right-click opens the hub-and-ring style wheel,
reusing the design from `text_style_wheel_redesign.md`:

- **Left hub (color mode)**: the 8-slot annotation palette.
- **Right hub (font mode)**: the 4 font choices — Sans, Serif, Mono, Art.

The font choice affects which font family is used to render the number inside the
bubble.

All wheel interaction (hub switching, ring selection, `Esc` to close, mode
persistence within the session) follows the rules in `text_style_wheel_redesign.md`
without modification.

The `color_wheel_has_text_hub` field in `D2DPaintInput` (and all related usages in
`overlay_window.cpp` and `d2d_paint.cpp`) should be renamed to
`color_wheel_has_style_hub` to reflect that both the Text and Bubble tools activate
it:

```cpp
input.color_wheel_has_style_hub =
    active_tool == AnnotationToolId::Text ||
    active_tool == AnnotationToolId::Bubble;
```

When the Bubble tool is active, the right-hub `A` glyph must reflect the current
`Bubble_font_choice()`, not `Text_current_font()`.

### Counter Semantics

The counter is a session-local integer starting at 1 when a capture begins. The
bubble placed on a given click **shows the counter value before that action**. Each
Add increments the counter; each Undo of an Add decrements it; Delete does not touch
it.

| Before | Action        | After | Bubble shown  |
|--------|---------------|-------|---------------|
| 1      | Add bubble    | 2     | "1"           |
| 2      | Add bubble    | 3     | "2"           |
| 3      | Undo (Add)    | 2     | — "2" removed |
| 2      | Delete bubble | 2     | — "1" removed |
| 2      | Add bubble    | 3     | "2"           |
| 3      | Undo (Delete) | 3     | "1" restored  |
| 3      | Redo (Add)    | 4     | "2" restored  |

Rules:

- **Add**: display counter; then increment counter.
- **Undo(Add)**: remove bubble; decrement counter.
- **Redo(Add)**: re-add bubble; increment counter.
- **Delete**: remove bubble; counter unchanged.
- **Undo(Delete)**: restore bubble; counter unchanged.
- Undo/redo of non-bubble operations: counter unchanged.
- **New capture** (session reset): counter returns to 1.

## Visual Specification

### Circle and Stroke

- **Filled disc**: the circle is filled solid in the current annotation color.
  Diameter = `brush_width` physical pixels.
- **Inner stroke**: a 1 px stroke drawn 1 px inside the outer edge of the circle,
  in the same color as the number (white or black; see below). This ensures a
  visible ring on any fill color.
- Both the disc edge and the inner stroke are anti-aliased.

### Number Color — White vs. Black

The number and inner stroke are always either pure white (`#FFFFFF`) or pure black
(`#000000`), chosen to maximize contrast against the fill color using the WCAG
relative-luminance formula.

Algorithm:

1. Extract `R, G, B` from the fill `COLORREF` (0–255 each).
2. Normalize: `r = R / 255.0`, `g = G / 255.0`, `b = B / 255.0`.
3. Linearize each channel via the sRGB transfer function:
   - If `c ≤ 0.04045`: `c_lin = c / 12.92`
   - Else: `c_lin = ((c + 0.055) / 1.055) ^ 2.4`
4. Relative luminance: `L = 0.2126 × R_lin + 0.7152 × G_lin + 0.0722 × B_lin`
5. If `L > 0.179`: use **black** (`#000000`). Else: use **white** (`#FFFFFF`).

Expose this as a pure function in `greenflame_core`:

```cpp
[[nodiscard]] COLORREF Bubble_text_color(COLORREF fill_color) noexcept;
```

Representative values for documentation and tests:

| Fill      | L (approx.) | Text  |
|-----------|-------------|-------|
| `#000000` | 0.000       | white |
| `#FFFFFF` | 1.000       | black |
| `#808080` | 0.216       | black |
| `#FF0000` | 0.213       | black |
| `#0000FF` | 0.072       | white |
| `#FFFF00` | 0.928       | black |
| `#008000` | 0.154       | white |

### Font Size

The number is rendered in the font family corresponding to the active font choice,
centered inside the circle.

**Same-size rule**: values 1–99 all use the same font size for a given bubble
diameter. Single digits are not given a larger size — the same size computed to fit
`"88"` is used for all values 1–99. Values 100–999 use a smaller font size computed
to fit a three-digit reference string, and so on by digit count.

**Reference string**: the font size for N digits is computed by fitting the string
`"8" × N` (i.e. `"8"`, `"88"`, `"888"`, …) into the target area. Eights are used
as a conservative advance-width bound for both proportional and monospaced fonts.

**Target fitting area**: an inscribed area with 15% per-side inset:

```
fit_diameter_dip = bubble_diameter_dip × 0.70
bubble_diameter_dip = brush_width_px × (96.0 / display_dpi)
```

**Recommended starting formula** (to be verified and tuned during implementation):

```
1–2 digits  (values    1–99):  font_size_dip ≈ bubble_diameter_dip × 0.50
3 digits    (values 100–999):  font_size_dip ≈ bubble_diameter_dip × 0.35
4+ digits   (values  1000+):   font_size_dip ≈ bubble_diameter_dip × 0.25
```

These factors are derived from the condition that the widest N-digit reference string
fits within the target area, using approximate cap-height (≈ 0.70 em) and
advance-width (≈ 0.55 em per digit) ratios for proportional sans-serif fonts. Actual
DWrite metrics vary by typeface; the implementation must verify each of the four font
families and adjust if needed.

If the rasterizer finds that the computed size still overflows the fitting area (e.g.
in a wide font), it must reduce the size until the string fits, using a small
multiplicative backoff. Minimum allowed font size is 1 DIP. If the bubble is too
small to display the current digit count legibly, the number is silently clipped
inside the bitmap boundary.

## Config

Add the following `[tools]` key:

- `bubble_font=sans`

Normalization rules (identical to `text_current_font`):

- Valid values: `sans`, `serif`, `mono`, `art`.
- If invalid or empty, restore `sans`.

Font family resolution reuses the four keys already defined for the Text tool:

- `text_font_sans`, `text_font_serif`, `text_font_mono`, `text_font_art`

Only the active choice key is separate; the family strings are shared.

The Bubble tool uses the existing shared annotation palette:

- `current_color`, `color_0` through `color_7`

The Bubble tool uses the existing shared `brush_width` key.

## Core Model

### New Types

Add:

- `AnnotationToolId::Bubble`
- `AnnotationToolbarGlyph::Bubble`
- `AnnotationKind::Bubble`

Declare `BubbleAnnotation` in a new header `bubble_annotation_types.h`, following
the pattern of `text_annotation_types.h`:

```cpp
struct BubbleAnnotation final {
    PointPx center = {};
    int32_t diameter_px = 0;       // = brush_width at placement time
    COLORREF color = 0;
    TextFontChoice font_choice = TextFontChoice::Sans;
    int32_t counter_value = 0;     // number displayed inside the circle

    // Rasterized bitmap cache (populated at rasterization time).
    int32_t bitmap_width_px = 0;
    int32_t bitmap_height_px = 0;
    int32_t bitmap_row_bytes = 0;
    std::vector<uint8_t> premultiplied_bgra = {};

    bool operator==(BubbleAnnotation const &) const noexcept = default;
};
```

Bubble selection uses the shared selected-annotation marquee. A selected bubble
shows the clockwise marquee around its selection frame bounds and remains movable
from anywhere inside that box.

### Variant Updates

Extend `AnnotationData` in `annotation_types.h`:

```cpp
using AnnotationData = std::variant<FreehandStrokeAnnotation, LineAnnotation,
                                    RectangleAnnotation, TextAnnotation,
                                    BubbleAnnotation>;
```

Adding `BubbleAnnotation` will produce a compile error at every `std::visit` site
that does not cover it. Fix each:

- `Annotation::Kind()`
- `Annotation_bounds(...)`
- `Annotation_visual_bounds(...)`
- `Annotation_hits_point(...)`
- `Translate_annotation(...)`
- `Blend_annotations_onto_pixels(...)`
- Live D2D paint dispatch

For `Annotation_bounds` / `Annotation_visual_bounds`: the bounding box is
`RectPx{ center.x − r, center.y − r, center.x + r, center.y + r }` where
`r = (diameter_px + 1) / 2` (rounds up for odd diameters). `Annotation_visual_bounds`
may add 1 px on each side for the anti-aliasing fringe.

For `Translate_annotation`: shift `center` by the translation vector; the cached
bitmap content is independent of position and remains valid after translation.

### Counter Ownership

The bubble counter lives in `AnnotationController` as a session-local integer.

New public interface:

```cpp
[[nodiscard]] int32_t Current_bubble_counter() const noexcept;
void Increment_bubble_counter() noexcept;
void Decrement_bubble_counter() noexcept;
```

`Reset_for_session()` resets the counter to 1.

### New Undo Command — `AddBubbleAnnotationCommand`

This is a dedicated command, distinct from the existing `AddAnnotationCommand`,
because it must also update the bubble counter.

- **Execute / Redo**:
  1. Add the annotation to the document.
  2. Call `annotation_controller.Increment_bubble_counter()`.
- **Undo**:
  1. Remove the annotation from the document.
  2. Call `annotation_controller.Decrement_bubble_counter()`.

`DeleteAnnotationCommand` requires **no changes** for bubble deletions. The delete
and undo-delete paths must not touch the counter.

## Rasterization

Bubble rasterization is DPI-dependent (font sizes are in DIPs; bubble coordinates
are in physical pixels). It follows the same pattern as `TextAnnotation`.

Add to `ITextLayoutEngine`:

```cpp
virtual void Rasterize_bubble(BubbleAnnotation &annotation) = 0;
```

The method modifies `annotation` in place, consistent with `Rasterize(TextAnnotation&)`.

The WIC render target is created at 96 DPI (matching the pattern used for text
annotation rasterization). The bitmap is `diameter_px × diameter_px` physical pixels
with no additional AA fringe. Because the render target is at 96 DPI (1 DIP = 1 px)
and the font size is computed as a fraction of `diameter_px`, the text correctly fills
the same proportion of the circle at any display DPI without needing a separate DPI
parameter.

The implementation:

1. Resolves the font family from `bubble.font_choice` via the existing
   `text_font_*` config strings.
2. Computes the text color via `Bubble_text_color(bubble.color)`.
3. Computes `font_size_dip` as a fraction of `diameter_px`: approximately `0.55 ×
   diameter_px` for 1–99 and `0.38 × diameter_px` for 100+.
4. Allocates a premultiplied BGRA bitmap of size `diameter_px × diameter_px`.
5. Renders the filled circle in `bubble.color` (radius inset by 0.5 px to avoid
   edge clipping).
6. Renders the inner 1 px stroke at 1 px inset from the outer edge, in the text
   color.
7. Renders `std::to_wstring(bubble.counter_value)` centered in the bitmap using
   `DWRITE_TEXT_ALIGNMENT_CENTER` / `DWRITE_PARAGRAPH_ALIGNMENT_CENTER`, in the
   resolved font at the computed size, in the text color.
8. Populates `bitmap_width_px`, `bitmap_height_px`, `bitmap_row_bytes`, and
   `premultiplied_bgra`.

Hit-testing uses a geometric circle-distance check against `center` and
`diameter_px`, not the cached bitmap alpha. Output blending uses the cached bitmap
alpha channel.

The fake `ITextLayoutEngine` used in unit tests implements `Rasterize_bubble` as a
stub that allocates an all-transparent `diameter_px × diameter_px` bitmap (or a
zero-size bitmap when `diameter_px ≤ 0`). This is sufficient for behavioral tests.

## Tool Object

`BubbleAnnotationTool` implements `IAnnotationTool`.

`On_pointer_press`:

1. Read `controller.Current_bubble_counter()` → `value`.
2. Construct a `BubbleAnnotation`:
   - `center = cursor`
   - `diameter_px = controller.Brush_width()`
   - `color = controller.Current_annotation_color()`
   - `font_choice = controller.Bubble_font_choice()`
   - `counter_value = value`
3. Call `layout_engine.Rasterize_bubble_annotation(bubble, display_dpi)` to
   populate the bitmap.
4. Push an `AddBubbleAnnotationCommand` onto the undo stack.

No drag gesture — placement is complete on press.

`Cursor_preview_type()`: returns the same circular preview type as
`FreehandAnnotationTool` (Brush).

`Right_click_opens_style_wheel()`: returns `true`.

`AnnotationController` gains:

```cpp
[[nodiscard]] TextFontChoice Bubble_font_choice() const noexcept;
void Set_bubble_font_choice(TextFontChoice choice) noexcept;
```

## Overlay Window Integration

No new interaction states are needed beyond placement and wheel handling. The
required changes are:

- **Rename** `color_wheel_has_text_hub` → `color_wheel_has_style_hub` everywhere
  it appears (`D2DPaintInput`, `overlay_window.cpp`, `d2d_paint.cpp`).
- **Extend** the condition to cover both Text and Bubble (see §Right-Click Style
  Wheel).
- **Font family for right hub**: when the Bubble tool is active, supply
  `controller_.Bubble_font_choice()` for the hub `A` glyph, not
  `controller_.Text_current_font()`.
- **Circular cursor preview**: draw the same Brush-style circular preview when
  the Bubble tool is armed.

## Resources

`resources/bubble.png` already exists in the repository. Derive a matching
alpha-mask asset and wire it through the existing toolbar glyph pipeline, using the
same process as all existing toolbar icons.

## Testing Plan

### Automated Unit Tests

**New file: `tests/bubble_annotation_tests.cpp`**

Counter behavior:

- Initial counter value is 1 after session reset.
- `On_pointer_press` places a bubble with the pre-increment counter value; counter
  becomes 2.
- Undo of the placement: bubble is removed; counter returns to 1.
- Redo of the placement: bubble is re-added; counter returns to 2.
- Delete a bubble: counter unchanged.
- Undo of delete: bubble restored; counter unchanged.
- Full sequence matching the table in §Counter Semantics.

`Bubble_text_color`:

- `#000000` → `#FFFFFF` (white).
- `#FFFFFF` → `#000000` (black).
- `#808080` → `#000000` (black; L ≈ 0.216 > 0.179).
- `#FF0000` → `#000000` (black; L ≈ 0.213 > 0.179).
- `#0000FF` → `#FFFFFF` (white; L ≈ 0.072 < 0.179).
- `#FFFF00` → `#000000` (black; L ≈ 0.928 > 0.179).
- `#008000` → `#FFFFFF` (white; L ≈ 0.154 < 0.179).

**Add to `tests/annotation_controller_tests.cpp`**:

- `N` hotkey toggles the Bubble tool on and off.
- `Bubble_current_font()` defaults to `Sans`.
- `Set_bubble_current_font()` persists within the session.
- Switching away from and back to the Bubble tool does not reset the counter.
- `Current_bubble_counter()` starts at 1; increments on Add; decrements on
  Undo(Add); unchanged on Delete and Undo(Delete); restored correctly on Redo(Add).
- Full counter sequence matching the table in §Counter Semantics.

**Add to `tests/app_config_tests.cpp`**:

- `bubble_current_font` round-trips for all four `TextFontChoice` values.
- Invalid `bubble_current_font` value restores `Sans`.

**Add to `tests/bubble_annotation_tests.cpp`**:

- Bubble hit-test uses geometric circle-distance against `center` and `diameter_px`
  (not bitmap alpha): center → hit; point just inside radius → hit; point just
  outside radius → miss; zero diameter → miss.
- `Translate_annotation` on a bubble shifts `center` and leaves other fields
  unchanged.
- `BubbleAnnotation` uses the shared selected-annotation marquee and does not add
  resize handles.

**`color_wheel_has_style_hub` flag** (`D2DPaintInput`) is set in `overlay_window.cpp`
based on the active `AnnotationToolId`. It is not testable from core unit tests and
is verified by manual test plan item: *"Right-click; verify the hub-and-ring style
wheel opens."*

### Manual Coverage Additions

Add to `docs/manual_test_plan.md` under a new **Bubble tool** section:

- Toggle the Bubble tool with hotkey `N` and the toolbar button; verify the
  circular cursor preview appears and the correct toolbar button is highlighted.
- Verify the preview circle diameter visually matches `brush_width`.
- Change size via mouse-wheel and `Ctrl+=` / `Ctrl+-`; verify the size overlay
  label appears and the preview updates.
- Click to place bubbles; verify numbers appear as 1, 2, 3… in order.
- Undo the last bubble; verify it disappears and the next click reproduces the
  same number.
- Redo; verify the bubble reappears with its original number.
- Delete a bubble; verify the counter is unchanged; place a new bubble and confirm
  the number sequence continues from where it was.
- Right-click; verify the hub-and-ring style wheel opens.
- Switch to font mode in the wheel; select each of the four fonts; verify bubble
  numbers render in the chosen typeface.
- Switch to color mode in the wheel; select colors; verify bubbles use the chosen
  fill.
- Verify `Esc` closes the wheel without applying any change.
- Close and reopen the wheel; verify it opens in the last-used mode for the
  session.
- Verify the right-hub `A` glyph reflects the current **bubble** font choice, not
  the text tool font.
- Verify white/black number contrast on a range of fill colors: light fill →
  black number, dark fill → white number, red fill → black number, blue fill →
  white number, medium green fill → white number.
- Verify the inner 1 px stroke is visible on both light and dark fills.
- Verify bubbles appear correctly in the save/copy output image.
- Verify overlay chrome (cursor preview, size label) does not appear in the output.
- Start a new capture session; verify the counter resets to 1.
- On a high-DPI monitor: verify bubble physical size and number size are correct
  compared to a 100% DPI display.
