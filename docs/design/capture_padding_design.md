---
title: Capture Padding Design
summary: Current contributor reference for CLI synthetic padding on live captures and `--input`, including parser rules, color resolution, and out-of-bounds behavior.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - cli
  - padding
  - capture
  - images
---

# Capture Padding Design

This document describes the current shipped CLI padding behavior.

It replaces the earlier proposal-style version of this file. The implementation,
tests, and higher-priority reference docs are authoritative when this document is
updated.

For the user-facing CLI contract, see [README.md](../../README.md). For the
shared `--input` path, see
[cli_input_image_annotation_design.md](cli_input_image_annotation_design.md).

## Current Status

CLI padding is already shipped.

Current surfaced pieces:

- `--padding`
- `-p`
- `--padding-color`
- `save.padding_color`

Current invariants:

- padding measurements are always in physical pixels
- padding is synthetic fill, never extra captured screen pixels
- the parser supports live capture sources and `--input`
- the controller resolves one padding color for the whole invocation
- the Win32 save path applies padding after source-image acquisition

This is no longer an implementation handoff. The parser, config property,
controller policy, Win32 save path, and tests already exist.

## CLI Contract

### Parser and validation rules

Current parser behavior in `cli_options.cpp`:

- `--padding` and `-p` are aliases
- `--padding` may be specified at most once
- `--padding-color` may be specified at most once
- `--padding` requires exactly one render source:
  - `--region`
  - `--window`
  - `--window-hwnd`
  - `--monitor`
  - `--desktop`
  - `--input`
- `--padding-color` requires `--padding`

Current `--padding` grammar:

- one value: `n`
  - expands to `left=top=right=bottom=n`
  - `n` must be `> 0`
- two values: `h,v`
  - expands to `left=right=h`, `top=bottom=v`
  - each value must be `>= 0`
  - at least one value must be `> 0`
- four values: `l,t,r,b`
  - each value must be `>= 0`
  - at least one value must be `> 0`

Other current parser details:

- surrounding whitespace around comma-separated parts is trimmed
- negative values are rejected
- three-value forms are rejected
- all-zero forms are rejected

Current `--padding-color` grammar:

- exact form: `#rrggbb`
- no alpha component
- uppercase and lowercase hex digits are both accepted

CLI misuse still fails during parse and validation before capture or save work.

## Color Resolution And Config

Padding color resolution is centralized in
`AppController::Resolve_padding_color(...)`.

Current resolution order when `--padding` is present:

1. `--padding-color`, if supplied
2. `save.padding_color` from config
3. the `AppConfig` default, `#000000`

Current config facts:

- `AppConfig` defaults `padding_color` to black
- `app_config_json.cpp` reads and validates `save.padding_color`
- the JSON value must be a `#rrggbb` string
- the property is persisted under the `save` object

The controller resolves this color for both live capture and `--input` mode and
passes it to the concrete save service as `fill_color`.

## Live Capture Semantics

### Source extent and outer padding

For live capture, `AppController::Run_cli_capture_mode(...)` resolves one target
rect from the selected source:

- requested region for `--region`
- matched window rect for `--window` or `--window-hwnd`
- selected monitor bounds for `--monitor`
- virtual desktop bounds for `--desktop`

When `--padding` is present, the controller:

- keeps the original target rect as the source-extent truth
- computes the final padded output size up front
- sets `CaptureSaveRequest::padding_px`
- sets `CaptureSaveRequest::fill_color`
- sets `CaptureSaveRequest::preserve_source_extent = true`

That `preserve_source_extent` flag is important. It tells the Win32 save path to
preserve the requested source extent instead of silently shrinking the source to
only the capturable intersection.

The current Win32 save path then:

1. captures or materializes the source image
2. preserves the requested source extent when padding requires it
3. fills uncovered source holes with the resolved padding color
4. applies outer padding using the same fill color
5. renders later output layers such as annotations onto the final canvas

### Partially outside the virtual desktop

For the GDI-backed live-capture path, padding changes partially out-of-bounds
behavior from clipping to fill-preserving output.

Current behavior:

- if the target rect still intersects the virtual desktop, the command succeeds
- uncapturable areas inside the preserved source extent are filled with the
  resolved padding color
- stderr includes:

```text
Warning: Requested capture area extends outside the virtual desktop.
Uncovered areas were filled with the padding color.
```

This warning is covered by `app_controller_tests.cpp`.

### Completely outside the virtual desktop

Padding does not allow a fully synthetic live capture result.

Current controller behavior for the GDI-prechecked live path:

- if the requested target rect has no intersection with the virtual desktop, the
  command fails
- region, monitor, and desktop captures fail before output-path reservation
- forced GDI window capture follows the same rule

Representative current errors are:

- `Error: Requested capture area is outside the virtual desktop.`
- `Error: Matched window is completely outside the virtual desktop. Nothing to capture.`

Window-backend-specific fallback behavior for `auto` and forced `wgc` is covered
separately by the CLI window-capture design docs.

### Overflow checks

Before output-path work begins, the controller validates that padding can expand
the requested source size without overflowing.

If padded dimensions cannot be represented, the command fails loudly with:

```text
Error: Requested padded output dimensions are invalid or too large.
```

## `--input` Semantics

The old proposal limited padding to live capture. The shipped implementation does
not.

Current `--input` behavior:

- `--padding` is accepted with `--input`
- `--padding-color` and `save.padding_color` apply the same way they do for live
  capture
- the controller probes the source image first, then derives
  `RectPx::From_ltrb(0, 0, width, height)` as the unpadded source extent
- the controller passes `InputImageSaveRequest::padding_px` and
  `InputImageSaveRequest::fill_color` into the input-image save service

Important differences from live capture:

- there is no virtual-desktop clipping step
- there is no off-desktop fill warning
- there is no obscuration warning
- padding is applied around the decoded source image, not around desktop pixels

The Win32 input-image save path reuses the same exact-source save helper used by
the capture path, which keeps padding behavior aligned across both modes.

## Output Resolution And Failure Ordering

Padding-related validation happens before the controller starts reserving or
creating output files.

### Live capture ordering

Current high-level order for live capture is:

1. resolve target rect and source metadata
2. validate padded output dimensions
3. validate required virtual-desktop intersection for the GDI-prechecked path
4. parse and prepare annotations
5. resolve and reserve output path
6. call the save service

This ordering matters because padding overflow and fully-outside-desktop errors do
not leave behind reserved output files.

### `--input` ordering

Current high-level order for `--input` is:

1. resolve absolute input path
2. probe and validate the input image
3. validate padded output dimensions against decoded image size
4. parse and prepare annotations
5. resolve or reuse output path
6. call the input-image save service

This keeps padding validation dependent on real decoded dimensions rather than file
extensions or guessed metadata.

### Cleanup on later save failure

If output-path reservation already happened and the later save step fails, the
controller deletes the reserved output path when it owns that reservation:

- default live-capture outputs reserved through `Reserve_unique_file_path(...)`
- explicit outputs reserved through `Try_reserve_exact_file_path(...)`

For `--input` in-place overwrite, the save service handles crash-safe replacement
internally instead of relying on controller-side reservation.

## Ownership Split

Current implementation split:

- `cli_options.cpp`
  - parses `--padding` and `--padding-color`
  - enforces option-shape and source-availability rules
- `app_controller.cpp`
  - resolves padding color
  - validates padded dimensions
  - decides live-capture out-of-bounds failure vs warning behavior
  - forwards padding metadata into live and input save requests
- `app_config_json.cpp`
  - parses and writes `save.padding_color`
- `win32_services.cpp`
  - materializes preserved source extents
  - fills uncovered areas and outer padding
  - shares the final save helper between capture and input-image flows

This split is consistent with the rest of Greenflame's controller-first CLI
design: policy in core, pixel work in Win32 services.

## Automated Coverage

Current automated coverage includes:

- `cli_options_tests.cpp`
  - one-, two-, and four-value padding parsing
  - whitespace trimming
  - duplicate-option rejection
  - zero-value, negative-value, and malformed-form rejection
  - `--padding` source requirements
  - `--padding-color` dependency and parsing
- `app_controller_tests.cpp`
  - config-default color use
  - CLI override precedence
  - preserved-source-extent save requests
  - partially out-of-bounds fill warning
  - fully outside-desktop failure
  - padded-dimension overflow failure
  - `--input` save-request wiring
- `app_config_tests.cpp`
  - default black padding color
  - config parse and round-trip coverage for `save.padding_color`

At this point, doc drift risk is mostly around parser details and backend-specific
window behavior, not missing feature coverage.
