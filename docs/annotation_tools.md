---
title: Annotation Tools
summary: Architecture and behavioral reference for Greenflame's overlay annotation system.
audience: contributors
status: authoritative
owners:
  - core-team
last_updated: 2026-03-08
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
  - click and drag empty space inside the selection to move the selection
  - clicking empty space outside the selection clears the selected annotation
- When the Brush tool is active, ordinary clicks are routed to that tool instead of
  the default selection/move behavior.
- If a gesture is already in progress, competing resize, selection-move, and
  annotation-move interactions do not steal that gesture.

### Tool interaction

- Default mode: no tool selected
- Registered tools:
  - `Brush tool` hotkey `B`
- The toolbar is anchored to the current selection border.
- Toolbar buttons display a tool glyph when one is available.
- Hovering a toolbar button shows a tooltip with the full tool name.
- Pressing `B` or clicking the `Brush tool` toolbar button toggles that tool on or
  off.
- While an annotation tool is active, right-click opens a color wheel centered on
  the cursor.
- While the color wheel is visible:
  - moving the mouse highlights the hovered color slot
  - left-clicking a slot selects that color for future annotations and closes the
    wheel
  - `Esc` closes the wheel without changing color
- While the Brush tool is active, mouse-wheel up/down or `Ctrl+=` / `Ctrl+-`
  increases or decreases brush width within the `1..50` range.
- While the Brush tool is active, the overlay draws an anti-aliased circular size
  preview around the cursor hotspot.
- Brush-width changes show a temporary centered size overlay inside the current
  selection using the same visual treatment as the center selection-size label.
- Brush width persists in the INI file at `[tools] brush_width`.
- The color wheel palette persists in `[tools] color_0` through `[tools] color_7`
  using `#rrggbb`.
- The currently selected palette slot persists in `[tools] current_color`.
- The size-overlay duration persists in the INI file at
  `[ui] tool_size_overlay_duration_ms`.
- Completing an annotation does not change the active tool.
- Starting a selection move clears the selected annotation.

### Selection and deletion

- Selection uses topmost-first hit-testing.
- Hit-testing is pixel-accurate against the annotation's rendered coverage.
- In default mode, clicking a covered pixel both selects the topmost annotation and
  begins moving it immediately.
- Selected annotations are shown by drawing the corners of their bounding box.
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
    - `BrushTool`

Tools are objects so behavior remains encapsulated and future tools can be added
without pushing per-tool logic into the Win32 layer.

## Document and annotation model

- `AnnotationDocument`
  - ordered `annotations`
  - `selected_annotation_id`
  - `next_annotation_id`

- `Annotation`
  - tagged type for future extensibility
  - current kind: `Freehand`

- `FreehandStrokeAnnotation`
  - raw/smoothed points in physical pixels
  - stroke style
  - cached raster coverage
  - committed on mouse-up, then becomes undoable

- Freehand draft state
  - raw draft points live in `AnnotationController`
  - draft style is exposed separately from committed annotations
  - committed raster coverage is built only when the stroke is finalized

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

The in-progress freehand stroke is intentionally different from committed
annotations:

- live preview is rendered from raw draft points in the Win32 paint layer
- the preview uses a lightweight polyline path for responsiveness during long
  strokes
- the stroke is rasterized in core only on commit (`mouse-up`)
- hit-testing and save/copy never use the draft preview path

Important rule: committed annotations must not gain a separate Win32-only paint path
that diverges from core hit-testing. Paint, hit-testing, and output must agree on
coverage once an annotation is committed.

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
- annotation move via `MoveAnnotationCommand`

Tool switching is **not** currently part of undo history.

## Rendering and output

### Live overlay

The overlay paint path draws in this order:

1. captured desktop image
2. capture-region dim/border chrome
3. committed annotations
4. in-progress annotation preview
   - freehand preview is drawn directly from draft points for responsiveness
5. selection labels / crosshair / region handles
6. selected-annotation corner markers
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
4. Extend `Annotation` with a new kind/data payload as needed.
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
