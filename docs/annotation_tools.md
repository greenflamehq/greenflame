---
title: Annotation Tools
summary: Architecture and behavioral reference for Greenflame's overlay annotation system.
audience: contributors
status: authoritative
owners:
  - core-team
last_updated: 2026-03-09
tags:
  - overlay
  - annotations
  - tools
  - undo
---

# Annotation Tools

This document describes the annotation-tool system used by Greenflame's overlay.
It is intended as the primary reference for future contributors and agents working in
this area.

## Scope

The annotation system applies only to the interactive overlay flow.

- Annotation tools are inactive until a capture region exists.
- The capture-region selection flow remains the outer interaction model.
- Annotations are stored in **physical pixels** in overlay/virtual-desktop coordinates.
- Annotations are **absolute with respect to the desktop**, not relative to the
  capture region.
- Only the portion of annotations intersecting the capture region is included in
  the saved/copied output.

## Current behavior

### Region interaction

- Before a region exists, the overlay behaves exactly like the original capture
  selector.
- After a region exists, border/corner drag still resizes the selection.
- When no annotation tool is selected, the overlay is in its default interaction
  mode:
  - click and drag an annotation to select it and move it
  - if the selected annotation is a line, drag either endpoint handle to edit it
  - click and drag empty space inside the selection to move the selection
  - clicking empty space outside the selection clears the selected annotation
- When an annotation tool is active, ordinary clicks are routed to that tool instead
  of the default selection/move behavior.
- If a gesture is already in progress, competing resize, selection-move, and
  annotation-move, and line-endpoint interactions do not steal that gesture.

### Tool interaction

- Default mode: no tool selected
- Registered tools:
  - `Brush tool` hotkey `B`
  - `Highlighter tool` hotkey `H`
  - `Line tool` hotkey `L`
  - `Arrow tool` hotkey `A`
  - `Rectangle tool` hotkey `R`
  - `Filled rectangle tool` hotkey `F`
  - `Ellipse tool` hotkey `E`
  - `Filled ellipse tool` hotkey `G`
- The toolbar is anchored to the current selection border.
- Toolbar buttons display a tool glyph when one is available.
- Hovering a toolbar button shows a tooltip with the full tool name.
- The last toolbar button on the right opens the keyboard-shortcuts help overlay on
  button release and does not toggle any tool state.
- Pressing `B` or clicking the `Brush tool` toolbar button toggles that tool on or
  off.
- Pressing `H` or clicking the `Highlighter tool` toolbar button toggles that tool on
  or off.
- Pressing `L` or clicking the `Line tool` toolbar button toggles that tool on or
  off.
- Pressing `A` or clicking the `Arrow tool` toolbar button toggles that tool on or
  off.
- Pressing `R` or clicking the `Rectangle tool` toolbar button toggles that tool on
  or off.
- Pressing `F` or clicking the `Filled rectangle tool` toolbar button toggles that
  tool on or off.
- Pressing `E` or clicking the `Ellipse tool` toolbar button toggles that tool on or
  off.
- Pressing `G` or clicking the `Filled ellipse tool` toolbar button toggles that tool
  on or off.
- While an annotation tool is active, right-click opens that tool's color wheel
  centered on the cursor.
- While the color wheel is visible:
  - moving the mouse highlights the hovered color slot
  - left-clicking a slot selects that color for future annotations and closes the
    wheel
  - `Esc` closes the wheel without changing color
- The Brush, Line, Arrow, Rectangle, Filled rectangle, Ellipse, and Filled ellipse tools use the
  configurable 8-slot annotation palette.
- The Highlighter tool uses its own configurable 6-slot palette.
- While the Brush, Highlighter, Line, Arrow, Rectangle, or Ellipse tool is active,
  mouse-wheel up/down or `Ctrl+=` / `Ctrl+-` increases or decreases stroke width
  within the `1..50` range.
- While the Brush tool is active, the overlay draws an anti-aliased circular size
  preview around the cursor hotspot.
- While the Highlighter tool is active, the overlay draws an anti-aliased
  axis-aligned square size preview around the cursor hotspot.
- While the Line or Arrow tool is active, the overlay draws an anti-aliased square
  size preview around the cursor hotspot aligned to the current line direction.
- While the Rectangle or Ellipse tool is active, the overlay draws an axis-aligned
  square size preview around the cursor hotspot.
- The Filled rectangle and Filled ellipse tools do not draw a cursor size preview
  overlay.
- Stroke-width changes show a temporary centered size overlay inside the current
  selection using the same visual treatment as the center selection-size label.
- Stroke width persists in the INI file at `[tools] brush_width`.
- The color wheel palette persists in `[tools] color_0` through `[tools] color_7`
  using `#rrggbb`.
- The currently selected palette slot persists in `[tools] current_color`.
- The Highlighter palette persists in `[tools] highlighter_color_0` through
  `[tools] highlighter_color_5` using `#rrggbb`.
- The currently selected Highlighter slot persists in
  `[tools] highlighter_current_color`.
- Highlighter opacity persists in `[tools] highlighter_opacity_percent`.
- The size-overlay duration persists in the INI file at
  `[ui] tool_size_overlay_duration_ms`.
- Completing an annotation does not change the active tool.
- Starting a selection move clears the selected annotation.

### Selection and deletion

- Selection uses topmost-first hit-testing.
- Hit-testing is pixel-accurate against the annotation's rendered coverage.
- In default mode, clicking a covered pixel both selects the topmost annotation and
  begins moving it immediately.
- Selected freehand annotations are shown by drawing the corners of their bounding
  box.
- Selected line and arrow annotations are shown by drawing 5px hollow endpoint
  handles with a 1px white inner and outer halo.
- Selected rectangle annotations are shown by drawing eight resize handles when
  space permits; corner handles take precedence over side handles when the bounds
  are too small to show all handles without overlap.
- Selected ellipse annotations are shown by drawing eight resize handles using the
  same bounding-rect handle layout as rectangles.
- `Delete` removes the selected annotation.
- Deletion is undoable and redoable.

## Architecture

## Top-level ownership

- `greenflame::OverlayWindow`
  - Win32 shell
  - collects mouse/keyboard input
  - owns capture bitmap and output save/copy plumbing
  - owns toolbar UI objects
  - delegates behavior to `greenflame::core::OverlayController`

- `greenflame::core::OverlayController`
  - session-level coordinator
  - still owns capture-region selection state and the unified `UndoStack`
  - owns `AnnotationController`
  - decides whether an event is handled by region logic or annotation-tool logic

- `greenflame::core::AnnotationController`
  - annotation-session state
  - optional active tool
  - ordered annotation list
  - selected annotation id
  - annotation drag state
  - in-progress freehand draft points and style

## Tool registry and tool objects

The registry lives in core.

- `AnnotationToolRegistry`
  - stores tool objects in display order
  - resolves by id and hotkey
  - builds toolbar button view models

- The default select/move behavior is intentionally **not** a registered tool.
  It is the controller's fallback mode when no annotation tool is active.

- `IAnnotationTool`
  - interface implemented by each tool object
  - current concrete tools:
    - `FreehandAnnotationTool` configured for brush strokes
    - `FreehandAnnotationTool` configured for highlighter strokes
    - `LineAnnotationTool` configured for plain lines
    - `LineAnnotationTool` configured for arrows
    - `RectangleAnnotationTool` configured for outlined rectangles
    - `RectangleAnnotationTool` configured for filled rectangles
    - `EllipseAnnotationTool` configured for outlined ellipses
    - `EllipseAnnotationTool` configured for filled ellipses

Tools are objects so behavior remains encapsulated and future tools can be added
without pushing per-tool logic into the Win32 layer.

## Document and annotation model

- `AnnotationDocument`
  - ordered `annotations`
  - `selected_annotation_id`
  - `next_annotation_id`

- `Annotation`
  - holds an `id` and an `AnnotationData` payload
  - `AnnotationData` is `std::variant<FreehandStrokeAnnotation, LineAnnotation,
    RectangleAnnotation, EllipseAnnotation>`; all dispatch is done with
    `std::visit(Overloaded{...},
    annotation.data)`
  - `AnnotationKind` enum exists separately and is used only by
    `Annotation_shows_corner_brackets`; `Annotation::kind()` derives it from the
    active variant alternative
  - adding a new alternative to the variant causes every `std::visit` site that
    does not cover it to become a compile error, which is the intended enforcement
    mechanism

- `FreehandStrokeAnnotation`
  - raw/smoothed points in physical pixels
  - stroke style
  - cached raster coverage
  - committed on mouse-up, then becomes undoable

- Freehand draft state
  - raw draft points live in `AnnotationController`
  - draft style is exposed separately from committed annotations
  - committed raster coverage is built only when the stroke is finalized

- `LineAnnotation`
  - start/end points in physical pixels
  - stroke style
  - optional filled arrowhead anchored at the dragged end point
  - cached raster coverage with square end caps
  - committed on mouse-up, then becomes undoable

- `RectangleAnnotation`
  - outer bounds in physical pixels using inclusive-dragged corners mapped to an
    exclusive right/bottom rect
  - stroke style and fill flag
  - outlined rectangles draw their border fully inward from the dragged outer edge
  - cached raster coverage
  - committed on mouse-up, then becomes undoable

- `EllipseAnnotation`
  - outer bounds in physical pixels using inclusive-dragged corners mapped to an
    exclusive right/bottom rect
  - stroke style and fill flag
  - outlined ellipses draw their border inward from the dragged outer edge
  - cached raster coverage
  - committed on mouse-up, then becomes undoable

## Rasterization, preview, and hit-testing

Pixel accuracy for committed annotations is achieved by using one shared core raster
path for selection, output compositing, and committed annotation paint.

- `Rasterize_freehand_stroke(...)`
  - converts a stroke polyline into a local coverage mask and bounds

- `Annotation_hits_point(...)`
  - answers whether a given overlay pixel belongs to the annotation

- `Index_of_topmost_annotation_at(...)`
  - scans annotations back-to-front for topmost selection

- `Blend_annotations_onto_pixels(...)`
  - composites committed annotations into a BGRA buffer for:
    - live overlay paint
    - cropped save output
    - clipboard output

### Draft freehand preview

The in-progress Brush and Highlighter strokes are intentionally different from
committed annotations:

- live preview is rendered from raw draft points in the Win32 paint layer
- the preview uses a lightweight polyline path for responsiveness during long
  strokes
- the preview still respects the active stroke style, including round vs square tip
  shape and opacity
- the stroke is rasterized in core only on commit (`mouse-up`)
- hit-testing and save/copy never use the draft preview path

Important rule: committed annotations must not gain a separate Win32-only paint path
that diverges from core hit-testing. Paint, hit-testing, and output must agree on
coverage once an annotation is committed.

### Draft line and arrow preview

The in-progress Line and Arrow previews reuse the committed core raster path:

- the draft line or arrow is rasterized in core during the gesture
- the overlay composites that draft raster above committed annotations
- hit-testing and save/copy still ignore the draft until mouse-up commits it

### Draft rectangle preview

The Rectangle and Filled rectangle tools also reuse the committed core raster path:

- the draft rectangle is rasterized in core during the gesture
- the overlay composites that draft raster above committed annotations
- hit-testing and save/copy still ignore the draft until mouse-up commits it

### Draft ellipse preview

The Ellipse and Filled ellipse tools also reuse the committed core raster path:

- the draft ellipse is rasterized in core during the gesture
- the overlay composites that draft raster above committed annotations
- hit-testing and save/copy still ignore the draft until mouse-up commits it

## Freehand smoothing

Smoothing is intentionally abstracted.

- `IStrokeSmoother`
- `PassthroughStrokeSmoother`

Current behavior is a pass-through. Future smoothing work should change the
smoother implementation, not the tool contract.

For freehand specifically, smoothing is applied when the stroke is committed. The
live preview currently follows the unsmoothed raw draft points.

## Undo and redo

The overlay uses one chronological undo stack for both region operations and
annotation operations.

Current command types:

- selection draw / create via `ModificationCommand<RectPx>`
- selection move / resize via `ModificationCommand<RectPx>`
- annotation add via `AddAnnotationCommand`
- annotation delete via `DeleteAnnotationCommand`
- annotation move / edit via `UpdateAnnotationCommand`

Tool switching is **not** currently part of undo history.

## Rendering and output

### Live overlay

The overlay paint path draws in this order:

1. captured desktop image
2. capture-region dim/border chrome
3. committed annotations
4. in-progress annotation preview
   - freehand preview is drawn directly from draft points for responsiveness
   - line and arrow previews are composited from the draft core raster
   - rectangle and filled-rectangle previews are composited from the draft core
     raster
   - ellipse and filled-ellipse previews are composited from the draft core raster
5. selection labels / crosshair / region handles
6. selected-annotation markers
   - freehand uses bounding-box corners
   - line and arrow use endpoint handles
   - rectangle uses corner/side resize handles
   - ellipse uses corner/side resize handles on its bounding rect
7. toolbar buttons and tooltip
8. color wheel, when visible

### Saved/copied image

The output pipeline is:

1. crop the original desktop capture to the selected region
2. composite committed annotations whose bounds intersect that region
3. encode or copy the composited bitmap

The overlay UI chrome itself is not included in output.

## Adding a new tool

To add a future tool:

1. Add a new tool id and descriptor metadata.
2. Implement a new `IAnnotationTool`.
3. Register it in `AnnotationToolRegistry` in the desired toolbar order.
4. If the tool produces a new annotation shape, add a new payload struct and extend
   `AnnotationData` in `annotation_types.h`:
   - define the new `XxxAnnotation` struct with its fields and `operator==`
   - add it to the `std::variant` alias `AnnotationData`
   - add it to `AnnotationKind` and add an arm to `Annotation::kind()`
   - decide whether `Annotation_shows_corner_brackets` should return `true` or
     `false` for the new kind
   - all `std::visit(Overloaded{...}, annotation.data)` call sites that are missing
     an arm will now fail to compile — fix each one
5. Add raster/hit-test/composite support in core.
6. Decide whether the tool needs a lightweight live-preview path distinct from its
   committed raster path.
7. Add undo commands for create/delete/edit operations.
8. Add controller tests covering tool behavior.
9. Update this document and the overlay help content.

## Non-negotiable rules

- Internal coordinate truth is physical pixels.
- Annotation storage is absolute to the overlay/virtual-desktop coordinate space.
- Topmost annotation wins selection.
- Hit-testing must remain pixel-accurate to rendered coverage.
- Win32 code stays a shell; core owns the behavior.
- Save/copy output must use the same coverage model as committed annotation
  selection.
- If a live-preview path differs from the committed path, the committed path is the
  source of truth.
