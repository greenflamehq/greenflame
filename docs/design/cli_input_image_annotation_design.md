---
title: CLI Input Image Annotation Design
summary: Current contributor reference for `--input` plus `--annotate`, including probe-first validation, local-only coordinate rules, and crash-safe in-place overwrite behavior.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - cli
  - annotations
  - input
  - images
---

# CLI Input Image Annotation Design

This document describes the current shipped `--input` annotation path.

It replaces the earlier proposal-style version of this file. The implementation,
tests, and higher-priority reference docs are authoritative when this document is
updated.

For the user-facing CLI contract, see [README.md](../README.md). For the shared
annotation format, see [cli_annotations.md](cli_annotations.md). For the broader
CLI annotation pipeline, see [cli_annotation_design.md](cli_annotation_design.md).

## Current Status

`--input` is already shipped as a one-shot CLI path for annotating an existing
PNG, JPEG, or BMP image.

Current invariants:

- `--input` requires `--annotate`
- `--input` requires either `--output` or `--overwrite`
- `--input` is incompatible with live capture modes
- `--input` is incompatible with `--window-capture`
- `--input` is incompatible with `--cursor` and `--no-cursor`
- imported images support only local coordinates
- imported images must decode fully opaque in V1

This is no longer a proposal. The option, parser, probe path, save path, and
manual coverage already exist.

## CLI Contract

### Parser and validation rules

Current parser behavior in `cli_options.cpp`:

- `--input` expects a non-empty path
- `--input` may be specified at most once
- `--input` requires `--annotate`
- `--input` requires either `--output` or `--overwrite`
- `--input` rejects all live capture sources:
  - `--region`
  - `--window`
  - `--window-hwnd`
  - `--monitor`
  - `--desktop`
- `--input` rejects `--window-capture`
- `--padding`, `--padding-color`, `--format`, and `--overwrite` remain valid

CLI misuse still fails with exit code `2`.

### Supported source formats

Current input-format support is:

- `.png`
- `.jpg`
- `.jpeg`
- `.bmp`

The extension is only the first gate. The file must also decode successfully
through the input-image service.

### Coordinate-space rules

`--input` reuses the same annotation document format, but with one important
restriction:

- `coordinate_space: "local"` is valid
- omitted `coordinate_space` still means `local`
- `coordinate_space: "global"` is rejected for `--input` with exit code `14`

Current local-coordinate meaning for `--input`:

- `0,0` is the top-left of the decoded source image before padding
- negative coordinates can render into left/top padding
- coordinates beyond the decoded width or height can render into right/bottom
  padding

This behavior is enforced by passing `CliAnnotationTargetKind::InputImage` into
the shared annotation parser.

## Probe-First Flow

`AppController::Run_cli_input_mode(...)` probes the input image before parsing
annotations against it.

Current high-level order:

1. resolve `--input` to an absolute path
2. probe the image through `IInputImageService`
3. derive `target_rect = RectPx::From_ltrb(0, 0, width, height)`
4. build `CliAnnotationParseContext` with:
   - `capture_rect_screen = target_rect`
   - `virtual_desktop_bounds = target_rect`
   - `target_kind = InputImage`
5. load and prepare annotations
6. resolve output path and format
7. save through `IInputImageService::Save_input_image_to_file(...)`

Probe-first matters because:

- invalid or unreadable source images fail before output-path work
- annotation coordinate translation uses real decoded dimensions
- extensionless explicit outputs can preserve the probed input format

## Input-Image Service Boundary

The input-image path intentionally uses a dedicated service rather than forcing
everything through the screen-capture interface.

Current service contracts in `app_services.h`:

- `IInputImageService::Probe_input_image(...)`
- `IInputImageService::Save_input_image_to_file(...)`

Relevant result types:

- `InputImageProbeResult`
- `InputImageSaveRequest`
- `InputImageSaveResult`

Current `InputImageProbeResult` carries:

- status
- width
- height
- detected format
- error message

Current `InputImageSaveRequest` carries:

- padding insets
- fill color
- prepared annotations

This keeps source-image decode and save mechanics in the Win32 layer while
leaving CLI policy and output-path resolution in core.

## Win32 Implementation Model

### Probe and decode

The current Win32 implementation probes and decodes input images through WIC.

Current behavior in `win32_services.cpp`:

- validate path and supported extension
- decode the image into 32bpp BGRA pixels
- record the detected container format
- reject any image with non-opaque alpha
- reject invalid or unrepresentable dimensions

If probe or decode fails, the path returns `InputImageProbeStatus::SourceReadFailed`
or `InputImageSaveStatus::SourceReadFailed`, which the controller maps to exit
code `16`.

### Save path

Current `Win32InputImageService::Save_input_image_to_file(...)` does this:

1. decode the source image again for save
2. convert it into the same `GdiCaptureResult` bitmap shape used by other save
   paths
3. build a `CaptureSaveRequest` with:
   - source rect = decoded image bounds
   - padding
   - fill color
   - prepared annotations
4. save through `Save_exact_source_capture_to_file(...)`

This reuse is intentional. It keeps padding and annotation composition aligned
with the other output pipelines.

## Padding And Composition Semantics

The input-image path reuses the same high-level final-assembly model as the other
CLI outputs.

Current ordering:

1. start from the decoded source image
2. create synthetic padding if requested
3. render prepared annotations onto the final canvas
4. save the result

Differences from live capture:

- no captured cursor layer exists for `--input`
- no virtual-desktop clipping step exists
- no window-obscuration warnings exist
- no off-desktop capture warning exists

The annotations still render above the final image and above any synthetic
padding area.

## Output Path And Format Rules

### In-place overwrite

If `--output` is omitted, the controller uses the resolved absolute `--input`
path as the output path.

Current format rule in that case:

- output format starts from the probed input-image format
- if `--format` is present, it must match that input-image format
- mismatched explicit `--format` fails with exit code `10`

### Explicit output

If `--output` is present, the controller uses the shared explicit-output
resolution path, but with a different default format:

- if `--output` has a supported extension, that extension wins
- otherwise, if `--format` is provided, `--format` wins
- otherwise, an extensionless explicit `--output` preserves the probed input
  image format

If explicit output exists and `--overwrite` is not set, the controller still
uses reservation semantics and fails with exit code `10`.

### Crash-safe in-place replacement

When input and output paths are the same, the current Win32 save path is
crash-safe by design:

1. create a unique sibling temp path
2. write the full annotated output to that temp file
3. replace the original only after successful write
4. delete the temp file on failure

This behavior is already implemented; it is not a future recommendation.

## Error And Exit-Code Mapping

Current exit-code mapping relevant to `--input`:

- `2`
  - CLI parse or validation failure
- `10`
  - output-path resolution failure
  - explicit output exists without `--overwrite`
  - incompatible overwrite-format request
- `14`
  - invalid `--annotate` input
  - `coordinate_space: "global"` with `--input`
  - explicit font-family validation failure
- `16`
  - source image unreadable
  - unsupported input format
  - decode failure
  - transparency rejection
- `18`
  - obfuscate annotations rejected because risk acknowledgement is still missing
- `11`
  - save or encode failure after a readable source image was accepted

Current wording examples exposed by tests and implementation include:

- `--input: unable to read image file "...": ...`
- `--input: image transparency is not supported with --input in V1.`
- `--annotate: $.coordinate_space "global" is not supported with --input.`

## Cleanup Semantics

Current cleanup rules matter:

- invalid annotation input must not create or reserve output files first
- unreadable input images fail before output-path work
- reserved explicit-output paths are deleted on later save failure
- in-place overwrite keeps the original file untouched unless replacement succeeds
- explicit-output saves with `--overwrite` do not use reservation cleanup, because
  no reservation file was created in that path

## Existing Coverage

Automated coverage already exists in:

- `tests/cli_options_tests.cpp`
- `tests/app_controller_tests.cpp`
- `tests/cli_annotation_import_tests.cpp`

Covered areas include:

- `--input` requiring `--annotate`
- `--input` requiring either `--output` or `--overwrite`
- incompatibility with live capture modes and `--window-capture`
- probe failure returning exit `16`
- transparency rejection returning exit `16`
- `global` coordinate rejection returning exit `14`
- obfuscate risk gating before save
- extensionless explicit output preserving the probed input format
- cleanup behavior on later save failures

Key manual coverage already exists in `docs/manual_test_plan.md`, especially:

- `GF-MAN-CLI-012`
- `GF-MAN-CLI-013`
- `GF-MAN-CLI-014`

Future changes should keep `--input` on the same prepared-annotation pipeline as
live captures instead of introducing a separate JSON or raster path just for
input images.
