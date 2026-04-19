---
title: Obfuscate Tool Design
summary: Design document for the Obfuscate annotation tool, which pixelates or blurs a rectangular region to hide sensitive content.
audience: contributors
status: draft
owners:
  - core-team
last_updated: 2026-03-26
tags:
  - overlay
  - annotations
  - tools
  - obfuscate
---

# Obfuscate Tool Design

## Overview

The Obfuscate tool adds a rectangle-shaped annotation that permanently obscures the
pixels beneath it using either block pixelation or a blur. It is intended for hiding
sensitive information (PII, passwords, tokens) before sharing a screenshot.

This document covers only v1 scope. The tool is a new registered annotation tool
in `AnnotationToolRegistry`, producing a new `ObfuscateAnnotation` payload.

## Tool Behavior

- Hotkey: `O`
- Toolbar button glyph: `resources/obfuscate.png` (mask: `resources/obfuscate-mask.png`)
- Toggling: pressing `O` or clicking the toolbar button toggles the tool on or off.
- While the tool is active, the user clicks and drags to define a rectangular region.
  The drag follows the same corner-pair convention as `RectangleAnnotation`: the
  dragged corner pair is mapped to an exclusive right/bottom rect.
- On mouse-up the annotation is committed and becomes undoable. Completing the
  annotation does not deactivate the tool.
- The tool does not use the annotation color palette. Right-click while the tool is
  active does not open a selection wheel.
- Block size is controlled by mouse-wheel up/down or `Ctrl+=` / `Ctrl+-`, range
  `1..50`, with the same temporary centered size overlay used by other tools.
  Block size 1 means "blur mode" (no pixelation blocks); values 2..50 are block
  pixelation modes. The default block size is 10. The block size persists in the JSON
  file at `tools.obfuscate.block_size`.
- The active mode (blur vs. block) is implied by the current block size value; there
  is no separate mode toggle.
- While the tool is active, the overlay draws an axis-aligned square size preview
  around the cursor hotspot (same treatment as Rectangle/Ellipse tools).

## Algorithm

There are two render paths: a **CPU core path** used to produce the committed
`RasterCoverage` (used for overlay composite, save, copy, and hit-testing), and a
**GPU preview path** used during live gestures (see "GPU-Accelerated Live Preview"
below). The CPU core path is authoritative.

Both paths are invoked not only when the obfuscate annotation itself is created or
resized, but also whenever a lower annotation changes and triggers reactive
recomputation (see "Reactive Recomputation").

### Block pixelation mode — CPU core path (block_size >= 2)

The algorithm is explicit per-block area averaging with no interpolation filter.
This is the only approach that guarantees strict per-block isolation — scale-based
approaches (e.g. Qt's `SmoothTransformation` downscale) use a separable bilinear
filter whose kernel crosses block boundaries, bleeding color from one block into
adjacent blocks and producing visible color gradients across the region.

1. Sample the composited pixel buffer for the annotation bounds (see
   "Composited Sampling" below).
2. Compute the output grid dimensions: `cols = ceil(w / block_size)`,
   `rows = ceil(h / block_size)`.
3. For each cell `(cx, cy)` in the grid:
   a. Collect all source pixels whose physical coordinates fall within the cell's
      footprint (a box-average over at most `block_size × block_size` pixels,
      clamped to the region edge for partial cells).
   b. Compute the arithmetic mean of B, G, R, A channel values separately.
   c. Write that average color to every pixel in the cell's footprint in the
      output buffer.
4. The result is a stamp where every `block_size × block_size` area shows one
   averaged block color. Each block is strictly independent; no color from one
   block influences any other.

### Blur mode — CPU core path (block_size == 1)

Apply a separable box blur to the composited source pixels:

1. Horizontal pass: for each row, convolve with a box kernel of a fixed radius
   (exact value is a named constant; target range 8–12 physical pixels).
2. Vertical pass: repeat with the same kernel applied column-wise.
3. Repeat both passes a second time (two full horizontal+vertical iterations total)
   to approximate a Gaussian.

No user-adjustable radius in v1. The blur radius is a named compile-time constant.

## GPU-Accelerated Live Preview

During both the drag gesture (new annotation) and the move gesture (existing
annotation being repositioned), the overlay uses a Direct2D effect chain for
real-time pixelation preview instead of running the CPU core path on every
mouse-move event.

**Block pixelation preview** — three chained built-in D2D effects:

```
ID2D1Bitmap (capture)
  → CLSID_D2D1Crop          (isolate the current gesture rect)
  → CLSID_D2D1Scale         (scale down by 1/block_size, LINEAR interpolation)
  → CLSID_D2D1Scale         (scale up by block_size, NEAREST_NEIGHBOR interpolation)
  → DrawImage at gesture rect origin
```

**Blur preview** — two chained built-in D2D effects:

```
ID2D1Bitmap (capture)
  → CLSID_D2D1Crop          (isolate the current gesture rect)
  → CLSID_D2D1GaussianBlur  (D2D1_BORDER_MODE_HARD, standard deviation matching blur radius)
  → DrawImage at gesture rect origin
```

The three/two effect objects are created once when the overlay resources are
initialized and reused across gestures. Only the `D2D1_CROP_PROP_RECT` and scale
factor properties are updated on each mouse-move before the repaint.

**Coordinate note:** D2D effect properties are in DIPs. The source capture bitmap
must be created with `dpiX = dpiY = 96` so that DIP coordinates equal physical
pixel coordinates throughout, consistent with Greenflame's physical-pixel-first
internal model.

**Limitation of the preview path:** During gestures the preview samples from the
raw capture only, not the composited image (capture + prior committed annotations).
This is intentional: it avoids the cost and complexity of maintaining an
intermediate composited render target for preview purposes. The committed raster
(computed on mouse-up) always uses the correct composited sampling.

## Composited Sampling

The committed obfuscate raster is sampled from the **composited image** at commit
time, not from the raw capture:

1. Start with the raw capture BGRA buffer.
2. Composite all annotations already present in the document (in their committed
   order, including any prior `ObfuscateAnnotation` entries) onto a temporary
   buffer, clipped to the new annotation's bounds.
3. Apply the pixelation or blur algorithm (CPU core path) to the composited pixels.
4. Store the result as the annotation's raster stamp (`raster_coverage`).

Stacked obfuscations correctly build on each other because each samples the
already-composited state at commit time.

## Reactive Recomputation

Any committed change to an annotation (add, delete, move, resize, or edit) that
affects the composited pixel state may invalidate the raster of any
`ObfuscateAnnotation` whose bounds overlap the changed region and that sits above
the changed annotation in z-order.

**Trigger rule:** after any annotation change is committed, the
`AnnotationController` identifies all obfuscate annotations that are (a) above the
changed annotation in z-order and (b) whose `bounds` intersect the changed
annotation's old or new bounds. These obfuscate annotations are recomputed in
ascending z-order — lower obfuscates first, because a higher obfuscate may sample a
lower one.

**Undo bundling:** the recomputed obfuscate states are folded into the same undo
step as the triggering change. Undoing the triggering change also restores all
obfuscate rasters to their pre-change state. This requires a compound command or an
undo record that carries before/after `ObfuscateAnnotation` values alongside the
primary change.

**Live preview during gestures:** when a gesture is in progress on an annotation
that lies below one or more obfuscates, the overlay paint path shows D2D-rendered
obfuscate previews sourced from the current gesture state rather than the stale
committed rasters. Concretely:

1. The paint path renders the pre-obfuscate state (capture + all committed
   annotations below the affected obfuscates, with the in-progress gesture applied)
   to an intermediate `ID2D1Bitmap`.
2. Each affected obfuscate is drawn by feeding that intermediate bitmap into its
   D2D effect chain at the obfuscate's `bounds` position.
3. Higher obfuscates chain off the output of lower ones (each intermediate bitmap
   is updated in z-order before being fed to the next obfuscate's effect chain).

This ensures that N stacked obfuscates all update in real time as the underlying
annotation moves, without running the CPU rasterization path on every mouse-move.

## ObfuscateAnnotation Type

### Fields

- `bounds: RectPx` — dragged region in physical pixels, stored as exclusive
  right/bottom rect (same convention as `RectangleAnnotation`)
- `block_size: int` — the block size value in effect at commit time (1 = blur)
- `raster_coverage: RasterCoverage` — the precomputed BGRA stamp, same type used
  by other committed annotations

No source-pixel snapshot is stored. The stamp is always recomputed from the current
composited state whenever `bounds` changes or a triggering annotation change occurs.

### Variant extension

`ObfuscateAnnotation` is added to the `AnnotationData` variant in
`annotation_types.h`. Every `std::visit(Overloaded{...}, annotation.data)` call
site that lacks an arm will fail to compile — each must be updated.

`Annotation::kind()` returns a new `AnnotationKind::Obfuscate` for this variant
arm.

### Selection display

Obfuscate uses the shared selected-annotation marquee plus the same
bounding-rect resize handles as `RectangleAnnotation` and `EllipseAnnotation`
when exactly one obfuscate is selected.

### Resize behavior

Resizing an `ObfuscateAnnotation` recomputes the obfuscation at the new bounds.

During the resize gesture, the D2D preview chain renders a real-time pixelation
preview at the current drag bounds (sourced from the intermediate composited bitmap
to reflect any annotations beneath the new region).

On mouse-up, the `AnnotationController` performs full composited sampling +
rasterization at the new `bounds`. The `raster_coverage` is replaced with the
result. An `UpdateAnnotationCommand` records the before/after
`ObfuscateAnnotation` values for undo/redo.

### Move behavior

Moving an `ObfuscateAnnotation` recomputes the obfuscation at the new position,
following the same pattern as resize: D2D live preview during the gesture,
CPU composited rasterization on mouse-up, `UpdateAnnotationCommand` for undo.

### Undo

Obfuscate annotations use the existing `AddAnnotationCommand` and
`DeleteAnnotationCommand` for create/delete. Move and resize commits use
`UpdateAnnotationCommand`. Reactive recomputation records are bundled into the
same undo step as the triggering change (compound record). No new command types
are needed for the obfuscate annotation itself, but the compound undo record for
triggered recomputation is new infrastructure.

## Draft Preview

The in-progress drag gesture shows a live GPU-rendered pixelation preview via the
D2D effect chain (raw capture, not composited). This provides immediate visual
feedback at interactive frame rates for any region size, since the effect graph
executes entirely on the GPU.

On mouse-up, the CPU composited rasterization runs once to produce the definitive
committed stamp. The preview is replaced by the committed raster in the same repaint.

## Icon and Resource Steps

1. Source icon: `resources/obfuscate.png` (already present — check in as-is).
2. Generate the toolbar mask:

   ```
   magick resources\obfuscate.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\obfuscate-mask.png
   ```

3. Check in `resources/obfuscate-mask.png`.
4. Register both files in `resources/greenflame.rc.in` following the same pattern
   as existing tool icons.

## Adding the Tool (Implementation Checklist)

Follow the steps in `docs/annotation_tools.md` § "Adding a new tool". Specific
items for Obfuscate:

1. Add tool id, hotkey `O`, and display name to the tool metadata.
2. Implement `ObfuscateAnnotationTool : IAnnotationTool`.
3. Register it in `AnnotationToolRegistry` in toolbar order after the Ellipse tools.
4. Add `ObfuscateAnnotation` struct, extend the `AnnotationData` variant, add
   `AnnotationKind::Obfuscate`, update `Annotation::kind()`, and define the
   selection-frame and single-selection handle behavior for the new kind.
5. Fix all `std::visit` sites that become compile errors.
6. Implement `Rasterize_obfuscate(...)` in core (CPU, no Win32 dependencies):
   explicit block-iteration pixelation + double-pass box blur.
7. Implement composited sampling in `AnnotationController`: composite capture +
   prior annotations within bounds, then call `Rasterize_obfuscate`.
8. Wire the obfuscate raster into `Blend_annotations_onto_pixels`.
9. Implement hit-testing against `bounds` (rectangular, not coverage-mask based,
   since the stamp fills the entire bounds rect).
10. Add JSON persistence for `tools.obfuscate.block_size` (default 10).
11. Add D2D effect objects (`Crop`, two `Scale`, `GaussianBlur`) to
    `D2DOverlayResources`. Update the D2D paint path to use these effects for drag,
    resize, and move gesture previews, and for the reactive live preview when an
    underlying annotation gesture is in progress. Implement the intermediate
    `ID2D1Bitmap` render for chaining stacked obfuscate previews in z-order.
    Set source bitmap DPI to 96 to align DIP coords with physical pixels.
12. Implement reactive recomputation in `AnnotationController`: after any
    annotation change, find affected obfuscate annotations (above changed annotation
    in z-order, bounds intersect old or new region), recompute in ascending z-order,
    and bundle all resulting `UpdateAnnotationCommand` records into the same undo
    step as the triggering change (compound undo record).
13. Add controller tests covering: block pixelation (no inter-block bleeding),
    blur mode, composited sampling ordering (later obfuscate samples earlier
    obfuscate), resize recomputation, move recomputation, reactive recomputation on
    underlying annotation change (including N stacked obfuscates), undo/redo
    including bundled compound undo.
14. Update `docs/annotation_tools.md` tool list and the overlay keyboard-shortcuts
    help content.

## Out of Scope for v1

- Ellipse/freehand-shaped obfuscation regions (rectangular only).
- User-adjustable blur radius (fixed named constant).
- Per-annotation opacity control.
