---
title: CLI Input Image Annotation Design
summary: Proposed `--input` design for applying `--annotate` JSON to an existing image file instead of performing a live capture.
audience: contributors
status: proposed
owners:
  - core-team
last_updated: 2026-03-22
tags:
  - cli
  - annotations
  - input
  - images
  - proposal
---

# CLI Input Image Annotation Design

This document defines the proposed behavior and implementation shape for a new
CLI image-source feature:

- `--input <path>`

The feature lets Greenflame load an existing image file, optionally add
synthetic padding, apply `--annotate`, and save the result without taking a new
screenshot.

This is intentionally written as an implementation handoff. It builds on the
existing CLI annotation and padding work described in
[docs/cli_annotation_design.md](cli_annotation_design.md),
[docs/cli_annotations.md](cli_annotations.md), and
[docs/capture_padding_design.md](capture_padding_design.md), plus the current
CLI contract in [README.md](../README.md).

## Recommendation Summary

- Recommended option name: `--input`
- `--input` requires `--annotate`
- `--input` is incompatible with all live capture options:
  - `--region`
  - `--window`
  - `--window-hwnd`
  - `--monitor`
  - `--desktop`
  - `--window-capture`
- Supported decoded input formats:
  - `png`
  - `jpg`
  - `jpeg`
  - `bmp`
- Imported images must decode to fully opaque pixels; non-opaque alpha is
  rejected in V1
- `--padding` and `--padding-color` remain valid with `--input`
- `--input` requires either `--output` or `--overwrite`
- `--input` with `--overwrite` and no `--output` overwrites the input file in
  place
- If explicit `--output` has no extension and `--format` is omitted, the output
  format defaults to the input image format
- Imported-image annotation supports `coordinate_space: "local"` only
- Imported-image annotation local coordinates use the decoded image's
  top-left corner as `0,0`
- Recommended new exit code: `16` for unreadable input image
- In-place overwrite must be implemented as write-to-temp-then-replace, not by
  truncating the source file first
- The Win32 implementation should decode input images through WIC and then
  reuse the existing padding, annotation-composite, and encode pipeline as much
  as possible

## Goals

- Let users annotate an already-existing image in a second CLI invocation.
- Support the main automation scenario:
  - create or obtain an image
  - inspect it externally
  - generate annotation JSON
  - rerun Greenflame with `--input` and `--annotate`
- Allow the same flow for arbitrary valid image files, not only screenshots.
- Preserve the current CLI annotation JSON schema wherever practical.
- Preserve the current padding behavior model for outer synthetic canvas.
- Keep CLI policy and validation in `greenflame_core`.
- Keep image decode, bitmap allocation, raster composition, and encode in
  `src/greenflame/win/`.
- Fail loudly and specifically when the input file cannot be read or decoded.

## Non-Goals For V1

- Interactive "open image and annotate" GUI flow
- Making `--input` a general-purpose image converter without `--annotate`
- New input formats beyond `png`, `jpg`/`jpeg`, and `bmp`
- Preserving imported image transparency
- Recovering original monitor layout or virtual-desktop origin from an imported
  screenshot
- Supporting `coordinate_space: "global"` for imported images
- Importing annotation JSON from EXIF, sidecar metadata, or embedded chunks
- Metadata preservation guarantees for rewritten files
- Multi-frame image formats

## Resolved Gaps And Ambiguities

The user requirements leave a few policy questions open. This design resolves
them up front.

### `--input` requires `--annotate`

`--input` is valid only when `--annotate` is also present in the same
invocation.

Reason:

- the requested feature is "annotate an existing image", not "copy or convert an
  existing image"
- this avoids adding a second image-transform mode with no annotation work

Validation failures for `--input` without `--annotate` should remain normal CLI
parse/validation failures with exit code `2`.

### Imported images use local coordinates only

For `--input`, the annotation document may use only:

```json
{ "coordinate_space": "local" }
```

or omit `coordinate_space`, which still defaults to `local`.

`coordinate_space: "global"` is invalid for imported images and should fail with
exit code `14`.

Reason:

- imported files do not carry a trustworthy virtual-desktop origin in the
  current design
- pretending that `global` still means "virtual desktop" would be wrong for
  arbitrary images
- keeping `global` only for live-capture modes preserves correctness

### Local coordinates are relative to the unpadded image origin

For `--input`, `0,0` is the top-left of the decoded source image before padding.

That means:

- an annotation at `x=0, y=0` lands on the original image corner
- negative coordinates can render into left/top padding
- coordinates beyond the image width or height can render into right/bottom
  padding

This matches the existing CLI capture-plus-padding model, where local
coordinates are relative to the nominal source content, not the final padded
canvas.

### Unreadable input image gets its own exit code

A new process exit code should be introduced:

- `16` = input image unreadable

This code should cover:

- file does not exist
- access denied / sharing violation
- unsupported extension for `--input`
- unsupported or corrupt file contents
- decode failure
- decoded image contains any non-opaque alpha
- decoded dimensions that cannot be represented safely in Greenflame's current
  integer geometry model

Reason:

- the user explicitly requested a new error code for unreadable image input
- this is a different failure class from bad annotation JSON (`14`) and from
  output write failure (`11`)

### Imported images must be fully opaque in V1

Imported images are accepted only if every decoded pixel is fully opaque.

That means:

- opaque PNG files are valid
- PNG files with any transparent or semi-transparent pixel are rejected
- the rule should be format-agnostic if another decoded format ever exposes
  alpha in the future

Reason:

- the current save path is screenshot-oriented and explicitly ignores bitmap
  alpha
- the current annotation compositor writes final pixels as fully opaque
- rejecting transparency is clearer and safer than flattening against an
  arbitrary background color

### `--input` requires an explicit write intent

If `--input` is present, the invocation must also include at least one of:

- `--output`
- `--overwrite`

That gives these supported forms:

- `--input + --output`
  - valid if the output path does not already exist
  - writes to `--output`
- `--input + --output + --overwrite`
  - always valid from an overwrite-policy perspective
  - writes to `--output`
- `--input + --overwrite`
  - valid
  - writes back to `--input`

This form is invalid:

- `--input`
  - fails validation with exit code `2`

Reason:

- this makes the write target explicit and intentional
- it avoids accidental in-place overwrite from a bare `--input` command
- it still supports the convenient "rewrite the same file" workflow when the
  caller opts into it explicitly with `--overwrite`

### In-place overwrite must be crash-safe

When `--output` is omitted, Greenflame must not decode from the input path and
then reopen the same path for truncating write.

Recommended behavior:

1. Decode the input file from its original path.
2. Render the final result to a unique sibling temp file.
3. Replace the original file only after a fully successful write.
4. Delete the temp file on failure.

Reason:

- correctness-first behavior should not corrupt the only source file when encode
  or replace fails
- this is especially important for agent-driven automation

### Format resolution preserves the input format unless the user overrides it

When `--input` is present:

1. If explicit `--output` has a supported extension, that extension defines the
   format.
2. Otherwise, if `--format` is provided, `--format` defines the format.
3. Otherwise, if explicit `--output` has no extension, the input image format
   defines the output format.
4. Otherwise, if `--overwrite` is present and `--output` is omitted, Greenflame
   overwrites the input path and therefore preserves the input format
   automatically.
5. If explicit `--output` extension conflicts with `--format`, the command
   fails.

Reason:

- imported-image annotation is a transform of an existing file, so preserving
  the source format is more intuitive than falling back to
  `save.default_save_format`
- this still leaves a clear override path through `--format` or an explicit
  extension

## User-Facing Behavior

### Option availability

`--input <path>` is valid only when all of these are true:

- `--annotate` is present
- no live capture mode is present
- `--window-capture` is not present

It may be combined with:

- `--output`
- `--format`
- `--padding`
- `--padding-color`
- `--overwrite`

It must also include at least one of:

- `--output`
- `--overwrite`

It is invalid with:

- `--region`
- `--window`
- `--window-hwnd`
- `--monitor`
- `--desktop`
- `--window-capture`
- `--help`
- `--version`

### Examples

Overwrite the input image in place:

```bat
greenflame.exe --input "D:\shots\issue.png" --overwrite --annotate ".\note.json"
```

Write to a different file:

```bat
greenflame.exe --input "D:\shots\issue.png" --annotate ".\note.json" --output "D:\shots\issue-annotated.png" --overwrite
```

Add padding around an existing image:

```bat
greenflame.exe --input "D:\shots\issue.jpg" --padding 32 --padding-color "#202020" --overwrite --annotate ".\note.json"
```

Convert format intentionally:

```bat
greenflame.exe --input "D:\shots\issue.bmp" --annotate ".\note.json" --output "D:\shots\issue-annotated.jpg"
```

### Input image rules

- Input path is resolved to an absolute path before any read attempt.
- Supported extensions for `--input` are:
  - `.png`
  - `.jpg`
  - `.jpeg`
  - `.bmp`
- The extension is only an allowlist gate; the file must still decode
  successfully.
- The decoded image must be fully opaque. Any non-opaque alpha is rejected in
  V1.
- Decode failures are fatal even if the extension looks valid.

### Annotation rules

`--input` continues to use the existing `--annotate` document shape, defaults,
and per-annotation schema.

The only new restriction is coordinate space:

- `local` is valid
- omitted `coordinate_space` still means `local`
- `global` is invalid with `--input`

### Padding semantics

Imported-image padding should follow the same high-level rule as current CLI
capture padding:

1. Start with the nominal source image.
2. Create a larger output canvas when `--padding` is present.
3. Fill the whole padded canvas with the resolved padding color.
4. Blit the original decoded image into the padded canvas at the configured
   offset.
5. Composite annotations after the padded output canvas exists.

Differences from live-capture padding:

- there is no virtual-desktop clipping step
- there is no off-desktop fill warning
- there are no window-obscuration warnings

## CLI Semantics

### Parser and validation rules

Add a new optional CLI field:

- `input_path`

Recommended validation rules:

- `--input` expects a non-empty path
- `--input` can only be specified once
- `--input` requires `--annotate`
- `--input` requires either `--output` or `--overwrite`
- `--input` is incompatible with any live capture option
- `--input` is incompatible with `--window-capture`
- `--padding-color` still requires `--padding`
- `--output` is valid when either:
  - a live capture mode is selected
  - `--input` is present
- `--padding` is valid when either:
  - a live capture mode is selected
  - `--input` is present
- `--annotate` becomes valid when either:
  - a live capture mode is selected
  - `--input` is present

CLI misuse should continue to fail with exit code `2`.

### Help text

Add a new optional CLI help row:

| Option | Meaning |
|---|---|
| `--input <path>` | Load an existing PNG/JPEG/BMP image, apply `--annotate`, and save the result instead of taking a screenshot |

The `--annotate` help text should also be widened slightly so it no longer
implies "saved CLI capture" only.

## Output Path And Overwrite Policy

### No `--output`

If `--output` is omitted and `--overwrite` is present:

- output path = resolved absolute `--input` path
- output format = detected input image format
- write must still be temp-then-replace internally

If `--output` is omitted and `--overwrite` is not present:

- validation fails with exit code `2`

### Explicit `--output`

If `--output` is present:

- use the existing explicit output-path resolution model
- keep existing reservation and `--overwrite` policy
- if the explicit path already exists and `--overwrite` is not present, fail
  with exit code `10`
- if the explicit path matches the input path and `--overwrite` is missing,
  still fail with exit code `10`

### Failure cleanup

If Greenflame reserved or created a new output path and the save later fails:

- delete the incomplete output path, matching current behavior

If Greenflame is doing an in-place overwrite:

- keep the original input file untouched unless the final replace step succeeds
- delete the temporary sibling file on failure

## Error Behavior

### Exit code mapping

Recommended exit code mapping after this feature:

| Code | Meaning |
|---|---|
| `2` | CLI parse or validation failure, including bare `--input` without `--output` or `--overwrite` |
| `10` | Output path resolution or reservation failure |
| `11` | Final render, encode, temp write, or replace failure |
| `14` | Invalid `--annotate` input |
| `16` | Unreadable input image |

### Recommended error wording

Unreadable input image:

```text
--input: unable to read image "C:\path\file.png": <detail>
```

Unsupported coordinate space for imported image:

```text
--annotate: $.coordinate_space "global" is not supported with --input.
```

Unsupported input extension:

```text
--input: unsupported image extension ".gif". Supported extensions are .png, .jpg/.jpeg, and .bmp.
```

Unsupported input transparency:

```text
--input: image transparency is not supported with --input in V1.
```

### Partial-write guarantees

- Invalid annotation input must not modify the source image.
- Unreadable input image must not create an output.
- In-place overwrite must not destroy the original file on save failure.
- Explicit-output failures should continue to delete partially written outputs
  that Greenflame created during that invocation.

## Architecture Proposal

### Core responsibilities

### `src/greenflame_core/cli_options.*`

Add parser support for `--input` and the validation rules above.

This is the main place where incompatible combinations should be rejected before
the controller tries to do any work.

### `src/greenflame_core/app_controller.cpp`

`AppController::Run_cli_capture_mode` should become a "CLI render mode" entry
point in practice, even if the method name is preserved initially for minimal
churn.

Recommended controller shape:

1. Resolve the source kind:
   - live capture path
   - imported-image path
2. For `--input`, probe the image first to get:
   - width
   - height
   - detected format
3. Build annotation parse context from that probed size:
   - `capture_rect_screen = RectPx::From_ltrb(0, 0, width, height)`
   - imported-image mode flag or equivalent validation hint
4. Parse and prepare annotations using the existing annotation pipeline.
5. Resolve output path and format using imported-image rules.
6. Dispatch the final render/save request through a Win32 image service.

The controller should not hold Win32 bitmap objects directly.

### `src/greenflame_core/cli_annotation_import.*`

The annotation parser needs one new concept:

- live-capture parse context
- imported-image parse context

Recommended minimal change:

- add a boolean or enum to `CliAnnotationParseContext` that tells the parser
  whether `global` coordinates are permitted

This keeps the existing JSON parser and default resolution logic intact while
failing early and explicitly for `--input`.

### Win32 responsibilities

### New service surface

Introduce a dedicated service rather than stretching `ICaptureService` further.

Recommended new abstraction in `app_services.h`:

- `IInputImageService`

Recommended responsibilities:

- probe an input image file for size and detected format
- decode the image file into a bitmap
- render padding and annotations onto the decoded bitmap
- save to explicit output or temp-then-replace source file

Suggested result types:

- `InputImageProbeResult`
  - status
  - width/height
  - detected format
  - error message
- `InputImageSaveRequest`
  - input path
  - padding
  - fill color
  - annotations
  - maybe a flag for in-place overwrite

The final save result can reuse the existing `CaptureSaveResult` shape.

### Win32 implementation strategy

Recommended implementation in `src/greenflame/win/`:

1. Use WIC to probe and decode `.png`, `.jpg`/`.jpeg`, and `.bmp`.
2. Scan decoded pixels for alpha and reject the image if any pixel is not fully
   opaque.
3. Convert the decoded frame into the same 32bpp bitmap representation already
   used by the current save/composite path.
4. Reuse the existing helper pattern from `win32_services.cpp`:
   - create padded canvas if needed
   - compute annotation target bounds
   - blend annotations onto pixels
   - save through existing encode helpers
5. For in-place overwrite, save to a unique sibling temp file first, then
   replace the original.

This keeps one shared "final image assembly" path instead of creating a
parallel annotation compositor just for `--input`.

### Why a separate image service is preferable

- `ICaptureService` is screen-capture focused by both name and current request
  model
- imported-image probing needs a read/decode path before output resolution and
  annotation parsing
- a separate service keeps tests clearer:
  - capture orchestration stays separate from file-image orchestration

## Data Model Notes

### Nominal source rect

For imported images, the nominal source rect should be:

```text
left = 0
top = 0
right = decoded width
bottom = decoded height
```

This lets the existing annotation target-bounds math continue to work for
padding.

### Save-selection source naming

Do not reuse screenshot-oriented filename-pattern logic for imported images when
`--output` is omitted.

The overwrite-in-place default is a better fit than inventing a generated save
name for this mode.

### Last-capture state

Imported-image annotation should not update "last captured region/window"
session state.

Reason:

- no screenshot was taken
- recapture hotkeys should continue to mean screen capture only

## Testing Plan

Implementation should add automated coverage in at least these areas.

### Unit tests

- `tests/cli_options_tests.cpp`
  - parse `--input`
  - reject `--input` without `--annotate`
  - reject bare `--input` without `--output` or `--overwrite`
  - reject `--input` with each live capture mode
  - reject `--input` with `--window-capture`
  - allow `--input` with `--padding`, `--output`, and `--overwrite`
- `tests/app_controller_tests.cpp`
  - imported-image happy path with `--overwrite` and no `--output`
  - explicit-output path with `--overwrite`
  - explicit-output path without `--overwrite` when output does not already
    exist
  - unreadable input image returns exit `16`
  - imported image with transparency returns exit `16`
  - invalid imported-image `coordinate_space: "global"` returns exit `14`
  - imported-image mode does not emit live-capture warnings
  - output format falls back to probed input format when explicit `--output`
    lacks an extension and `--format` is omitted
- `tests/cli_annotation_import_tests.cpp`
  - imported-image parse context rejects `global`
  - imported-image local coordinates still map exactly

### Manual tests

Add manual coverage to [docs/manual_test_plan.md](manual_test_plan.md) for:

- bare `--input` without `--output` or `--overwrite` fails
- annotate existing PNG in place with `--overwrite`
- annotate existing JPEG to a different output file
- annotate existing BMP with padding
- unreadable or corrupt input file
- transparent PNG input
- unsupported input extension
- explicit-output overwrite rules when output equals input
- annotation visibility in padding around imported images

## Documentation Updates After Implementation

Implementation should also update:

- [README.md](../README.md)
  - command-line option table
  - examples
  - exit-code table
- [docs/cli_annotations.md](cli_annotations.md)
  - `--input` availability
  - imported-image coordinate-space restriction
- [docs/manual_test_plan.md](manual_test_plan.md)
  - new manual cases

## Resolved Implementation Notes

- Preserve input format when explicit `--output` has no extension and
  `--format` is omitted.
- Reject imported images with any non-opaque alpha in V1.

Current codebase rationale for the transparency rule:

- the existing save path in
  [save_image.cpp](C:/code/greenflame/src/greenflame/win/save_image.cpp)
  creates WIC bitmaps from `HBITMAP` with
  `WICBitmapAlphaChannelOption::WICBitmapIgnoreAlpha`
- the current capture pipeline treats screenshots as opaque by design
- the current pixel compositor writes final pixels as fully opaque in the main
  annotation blend path

That makes explicit rejection safer than accidental flattening or partial
alpha-preservation through a pipeline that was not designed for it.
