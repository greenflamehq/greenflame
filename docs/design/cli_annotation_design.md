---
title: CLI Annotation Design
summary: Current contributor reference for `--annotate`, including parse flow, preparation boundaries, and save ordering for live captures and `--input`.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - cli
  - annotations
  - json
  - input-image
---

# CLI Annotation Design

This document describes the current shipped `--annotate` implementation.

It replaces the earlier proposal-style version of this file. The implementation,
tests, and higher-priority reference docs are authoritative when this document is
updated.

For the user-facing format guide, examples, and schema entry points, see
[cli_annotations.md](cli_annotations.md). This document focuses on contributor
concerns: parser boundaries, preparation flow, save ordering, and ownership.

## Current Status

`--annotate` is already shipped for both:

- live CLI capture modes
- `--input` image annotation mode

Current supported render sources are:

- `--region`
- `--window`
- `--window-hwnd`
- `--monitor`
- `--desktop`
- `--input`

Three points in particular drifted from the old proposal:

- `--annotate` is no longer live-capture-only; it also powers `--input`
- the repo already contains the checked-in schema and example fixtures
- the feature is integrated into the current CLI save pipeline and covered by both
  automated and manual tests

Current repository artifacts:

- schema: `schemas/greenflame.annotations.schema.json`
- example payloads: `schemas/examples/cli_annotations/`

## CLI Contract

### Option availability

`CliOptions::annotate_value` is populated by the shared CLI parser.

Current parser rules in `cli_options.cpp`:

- `--annotate` may be specified at most once
- the value must be non-empty after trimming
- `--annotate` requires one render source
- `--input` requires `--annotate`
- `--input` also requires either `--output` or `--overwrite`

Related current validation:

- `--input` remains incompatible with live capture modes and `--window-capture`
- `--input` also remains incompatible with `--cursor` and `--no-cursor`

### Inline JSON vs. file-path classification

`Classify_cli_annotation_input(...)` is the current discriminator.

Current behavior:

- trim leading whitespace
- if the first non-whitespace character is `{`, treat the value as inline JSON
- otherwise, treat the value as a file path

Current file behavior:

- file paths are resolved through `IFileSystemService`
- annotation files are read as UTF-8 text
- UTF-8 BOM is accepted

This same classifier is used for both live-capture and `--input` paths.

## Schema And Parse Model

### External format ownership

The external JSON contract is described for users in
[cli_annotations.md](cli_annotations.md).

Contributor-facing responsibility split is:

- `cli_annotations.md`
  - user-facing format guide
  - examples, coordinate rules, and high-level validation behavior
- `cli_annotation_import.cpp`
  - strict parser and translator into core annotation objects
- `cli_annotation_import_tests.cpp`
  - behavioral lock for parser semantics

### Strict validation

The current parser is strict.

Current behavior:

- unknown top-level keys are rejected
- unknown annotation keys are rejected
- keys invalid for a given annotation type are rejected
- invalid JSON syntax fails loudly
- invalid combinations fail loudly

`$schema` is an allowed top-level key, but it is metadata only and does not affect
rendering behavior.

### Parse context

`Parse_cli_annotations_json(...)` receives a `CliAnnotationParseContext` with:

- `capture_rect_screen`
- `virtual_desktop_bounds`
- `config`
- `target_kind`

Current `target_kind` values:

- `Capture`
- `InputImage`

This context is what lets one parser support both:

- live captures in virtual-desktop space
- decoded input images with local-only coordinates

### Coordinate-space rules

Current parse behavior:

- default `coordinate_space` is `local`
- `global` is supported only for live capture targets
- `global` with `--input` is rejected with exit code `14`
- coordinates are integer physical pixels
- coordinates may lie outside the nominal capture bounds

Translation happens during parse, not later. The parser emits core annotations in
the coordinate space expected by later save/preparation stages.

### Defaults and per-annotation resolution

Current precedence is:

1. annotation-level values
2. document-level values
3. app-config defaults

This currently applies to the shared style inputs:

- color
- highlighter opacity
- font
- size, where that annotation family supports sizing

Two implementation-backed mapping details matter:

- highlighter width uses the same step model as the GUI, currently `size + 10`
- bubble diameter uses the same step model as the GUI, currently `size + 20`

### Supported annotation families

The parser currently supports the same families documented in
[cli_annotations.md](cli_annotations.md), including:

- brush
- highlighter
- line
- arrow
- rectangle
- filled rectangle
- ellipse
- filled ellipse
- obfuscate
- text
- bubble

Current parser-specific rules worth preserving:

- bubble numbering is assigned automatically by bubble order only
- text accepts either `text` or `spans`, never both
- explicit font families are trimmed before later validation
- point-list Brush and Highlighter input uses the shared freehand smoothing path
- straight `start`/`end` Highlighter input keeps explicit bar geometry

## Preparation Boundary

Parsing and raster preparation are intentionally separate steps.

### Parse output

`Parse_cli_annotations_json(...)` returns a vector of core `Annotation` objects.

At this stage:

- structural validation is complete
- coordinates and defaults are resolved
- text and bubble annotations are not yet guaranteed raster-ready

### Preparation service

`Load_prepared_annotations(...)` then passes parsed annotations through
`IAnnotationPreparationService`.

Current `AnnotationPreparationRequest` includes:

- parsed annotations
- resolved preset font-family strings from config

Current `Win32AnnotationPreparationService` responsibilities:

- initialize Direct2D and DirectWrite for CLI preparation
- enumerate installed system fonts
- reject missing explicit font families as input-invalid
- rasterize text annotations for CLI output
- rasterize bubble annotations for CLI output

Status mapping is current behavior:

- `Success`
  - prepared annotations continue into save
- `InputInvalid`
  - CLI exits `14`
- `RenderFailed`
  - CLI exits `11`

That split is important: "bad user input" and "failed to render valid input" are
not treated as the same class of failure.

## Save Ordering And Failure Boundaries

### Live capture path

For live captures, `AppController::Run_cli_capture_mode(...)` currently does this
in order:

1. resolve capture target geometry and capture backend intent
2. build `CliAnnotationParseContext`
3. load and prepare annotations
4. reject obfuscate usage if `tools.obfuscate.risk_acknowledged` is still false
5. resolve and reserve the output path
6. save the capture with prepared annotations attached to `CaptureSaveRequest`

Two current ordering guarantees matter:

- parse/preparation failures happen before output-path reservation
- obfuscate-risk rejection also happens before output-path reservation

That keeps invalid annotation input from creating or reserving destination files.

### Input-image path

For `--input`, `AppController::Run_cli_input_mode(...)` currently does this in
order:

1. probe the input image
2. derive local target bounds from the decoded image size
3. build `CliAnnotationParseContext` with `target_kind = InputImage`
4. load and prepare annotations
5. reject obfuscate usage if acknowledgement is still missing
6. resolve output path or overwrite behavior
7. save through `IInputImageService`

Current `--input` constraints that stay outside the annotation parser:

- decoded source image must be fully opaque
- overwrite/format rules are enforced by input-image mode
- extensionless explicit output preserves the probed input-image format

### Final composition ordering

Prepared annotations are passed to the save layer, not rendered directly in
`AppController`.

Current live-capture save ordering in `Win32CaptureService` is:

1. build the source bitmap
2. composite the captured cursor if enabled
3. render prepared annotations into that bitmap
4. encode or write output

Current input-image save ordering in `Win32InputImageService` is analogous:

1. decode source image into a capture bitmap
2. create padding/fill when requested
3. render prepared annotations
4. save output

This means CLI annotations always render above captured pixels and above any
synthetic padding area.

## Exit Codes And Error Surfaces

Current exit-code mapping relevant to `--annotate`:

- `14`
  - annotation file read failure
  - inline JSON UTF-8 encode failure
  - JSON parse/validation failure
  - unsupported `global` coordinates for `--input`
  - explicit font-family validation failure
- `11`
  - annotation preparation render failure
  - later capture/save failure
- `18`
  - obfuscate annotations rejected because risk acknowledgement is still missing
- `16`
  - `--input` image decode/format/transparency failures

Current error-prefix convention is also important:

- parser errors are rooted at `--annotate:`
- structural validation often names a JSON path such as `$.annotations[0]...`

## Existing Coverage

Automated coverage is strong in this area.

Key automated coverage already exists in:

- `tests/cli_options_tests.cpp`
- `tests/cli_annotation_import_tests.cpp`
- `tests/app_controller_tests.cpp`

Covered areas include:

- `--annotate` parser availability and duplication rules
- `--input` requiring `--annotate`
- inline JSON vs. file-path classification
- strict unknown-key rejection
- local and global coordinate translation
- `global` rejection for `--input`
- bubble numbering order
- default-resolution precedence
- explicit font-family trimming and validation
- obfuscate risk gating
- parse/preparation failure ordering before output-path resolution
- successful save flow for both live capture and `--input`

Key manual coverage already exists in `docs/manual_test_plan.md`, especially:

- `GF-MAN-CLI-008`
- `GF-MAN-CLI-009`
- `GF-MAN-CLI-010`
- `GF-MAN-CLI-011`
- `GF-MAN-CLI-012`
- `GF-MAN-CLI-013`
- `GF-MAN-CLI-014`

Future changes should keep `cli_annotations.md` as the primary user-facing format
guide and keep this document focused on contributor-facing architecture and save
ordering.
