---
title: Obfuscate Tool Design
summary: Current reference for Greenflame's interactive Obfuscate tool, committed raster model, and reactive recomputation behavior.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - annotations
  - obfuscate
  - blur
  - pixelation
---

# Obfuscate Tool Design

This document describes the current shipped Obfuscate tool implementation in the
interactive overlay.

For shared annotation architecture, preview ordering, and selection behavior, see
[docs/annotation_tools.md](../annotation_tools.md). For the first-use warning and
CLI acknowledgment gate, see [obfuscate_risk_warning.md](obfuscate_risk_warning.md).

## Current Status

Obfuscate is a shipped rectangular annotation tool that hides content by storing a
committed bitmap stamp built from the composited image underneath the obfuscate
bounds.

Current mode model:

- `block_size == 1` -> blur
- `block_size == 2..50` -> block pixelation

The earlier draft of this document drifted significantly. In particular, current
implementation differs from that draft in these ways:

- live preview does not use a separate GPU-only/raw-capture effect-chain model
- both preview and commit use the same core `Rasterize_obfuscate(...)` function
- preview sampling uses the current composited source, including lower annotations
- reactive recomputation is implemented through ordinary command bundling, not a
  separate speculative design

## Tool Interaction

### Armed behavior

Current armed-state behavior:

- `O` toggles the Obfuscate tool
- right-click does not open a selection wheel
- the cursor shows the same axis-aligned square preview family used by Rectangle
  and Ellipse
- mouse-wheel and `Ctrl+=` / `Ctrl+-` adjust `tools.obfuscate.block_size`

Current configuration:

- config key: `tools.obfuscate.block_size`
- default value: `10`
- allowed range: `1..50`

### Drag and commit flow

Obfuscate creation is a drag gesture.

Current behavior:

- primary press starts the drag
- pointer move updates the live rectangle
- primary release commits a non-empty obfuscate
- click without drag does not commit anything
- `Esc` during the gesture cancels the draft
- the tool remains active after a successful commit

Bounds use the same rectangle convention as the outline rectangle tools:

- drag endpoints are converted to an exclusive right/bottom `RectPx`
- the committed bounds are normalized before rasterization

### Selection and editing

Committed obfuscates behave like rectangular annotations in default mode.

Current behavior:

- obfuscates can be selected, moved, and resized after commit
- when exactly one obfuscate is selected, it shows the standard rectangular
  corner and side resize handles
- the selection frame sits `1 px` outside the committed bounds
- point hit testing uses the full normalized bounds rectangle

## Rasterization Model

### Data model

Committed obfuscates use `ObfuscateAnnotation`, which currently stores:

- `bounds`
- `block_size`
- committed premultiplied BGRA bitmap fields

There is no stored source snapshot. The committed bitmap is always derived from
the current composited image below that obfuscate.

### Core raster function

All committed obfuscate pixels are produced by:

- `Rasterize_obfuscate(BgraBitmap const &source, int32_t block_size)`

This function is the current implementation baseline.

### Pixelation mode

When `block_size >= 2`, the rasterizer performs explicit per-cell averaging.

Current behavior:

- the source region is divided into `block_size x block_size` cells
- each cell averages only the pixels inside that cell
- partial edge cells are handled independently
- the averaged color is written back to every pixel in the cell footprint

This preserves strict block isolation. Existing raster tests specifically cover the
"no inter-block bleeding" behavior.

### Blur mode

When `block_size == 1`, the rasterizer runs a fixed-radius blur.

Current implementation details:

- blur radius constant: `kObfuscateBlurRadiusPx = 10`
- pipeline: horizontal box blur, vertical box blur, then the same pair again
- output dimensions match the source dimensions
- alpha remains opaque in the current raster path

## Composited Sampling And Preview

### Commit-time source

Commit-time rasterization samples from the composited image, not the raw capture.

Current commit flow in `AnnotationController`:

1. build an `ObfuscateAnnotation` with normalized bounds and the current block size
2. ask `IObfuscateSourceProvider::Build_composited_source(...)` for the pixels under
   those bounds, using lower annotations only
3. call `Rasterize_obfuscate(...)`
4. store the resulting BGRA stamp on the committed annotation

This is the same rule used by CLI-rendered obfuscates through the shared raster path.

### Live preview

The current live preview path also uses composited-source sampling plus the same core
rasterizer.

Current preview flow in `OverlayWindow`:

- `Build_preview_obfuscate_annotation(...)` normalizes the draft bounds
- it requests a composited source image for those bounds
- it calls `Rasterize_obfuscate(...)`
- it attaches the resulting bitmap to a temporary preview annotation for paint

This means the preview shown during drag, move, or resize already includes lower
committed annotations underneath the obfuscate region.

### Stacked obfuscate preview

Live preview also supports stacked obfuscates.

Current behavior:

- while moving or resizing an obfuscate, or editing a lower annotation beneath
  existing obfuscates, the overlay identifies affected higher obfuscates
- the preview path rebuilds those obfuscates in ascending z-order
- each higher preview therefore sees the current lower preview state rather than
  stale committed pixels

This is the behavior reflected in the current manual plan and shared annotation docs.

## Reactive Recomputation And Undo

### Recompute model

Reactive recomputation is already implemented in `AnnotationController`.

Current implementation approach is simpler than the older proposal:

- after a committed annotation mutation, the controller walks the post-change
  annotation list in order
- each obfuscate is rebuilt against the annotations beneath it
- if the rebuilt value differs from the prior committed value, the controller adds
  an `UpdateAnnotationCommand` for that obfuscate

This means recomputation is driven by "rebuild and compare" rather than a narrowly
filtered overlap-only trigger table in the current code.

### Undo bundling

Reactive obfuscate recomputes are folded into the same undo record as the triggering
change.

Current command model:

- new obfuscates use the normal `AddAnnotationCommand`
- obfuscate delete uses `DeleteAnnotationCommand`
- obfuscate move/resize commits use `UpdateAnnotationCommand`
- reactive recomputes are appended as additional `UpdateAnnotationCommand` entries
- grouped changes are wrapped in `CompoundCommand`

Undoing the primary change therefore restores both:

- the triggering annotation mutation
- every obfuscate stamp recomputed because of that mutation

## Configuration

The interactive Obfuscate tool currently depends on:

- `tools.obfuscate.block_size`
- `tools.obfuscate.risk_acknowledged`

Current behavior notes:

- `tools.obfuscate.block_size` controls both blur-vs-pixelate mode and pixelation
  strength
- the tool has no color palette or style wheel
- the first-use warning and persisted acknowledgment are documented separately in
  `obfuscate_risk_warning.md`

## Existing Coverage

Key automated coverage already exists in:

- `tests/obfuscate_raster_tests.cpp`
- `tests/annotation_tool_tests.cpp`
- `tests/annotation_controller_tests.cpp`
- `tests/annotation_edit_interaction_tests.cpp`
- `tests/app_config_tests.cpp`

Key manual coverage already exists in `docs/manual_test_plan.md`, especially:

- `GF-MAN-ANN-004A`
- `GF-MAN-ANN-004B`
- `GF-MAN-UI-003`

Future changes to Obfuscate should keep the warning/acknowledgment flow in
`obfuscate_risk_warning.md` and keep shared overlay behavior in
`docs/annotation_tools.md` instead of reintroducing speculative duplicate design text
here.
