---
title: Capture Padding Design
summary: Proposed CLI and config design for synthetic color padding around CLI capture outputs.
audience: contributors
status: proposed
owners:
  - core-team
last_updated: 2026-03-20
tags:
  - cli
  - padding
  - proposal
---

# Capture Padding Design

This document defines the proposed behavior and implementation shape for a new
CLI padding feature that adds synthetic color around a captured image.

The final user-facing feature is:

- `--padding`
- `--padding-color`
- `save.padding_color`

The feature applies to CLI capture modes and is intentionally written as an
implementation handoff.

## Recommendation Summary

- Recommended option name: `--padding`
- Recommended override option: `--padding-color`
- Recommended config key: `save.padding_color`
- Default padding color: `#000000`
- Supported capture modes: `--region`, `--window`, `--monitor`, `--desktop`
- Padding values are in **physical pixels**
- Padding is always synthetic color and **never** sampled from the screen
- Four-value order: `l,t,r,b`
- Recommended short alias: `-p` for `--padding`
- No short alias for `--padding-color`

`padding` is the correct term because the feature adds generated canvas around the
captured pixels. It does not expand what Greenflame captures from the screen.

## Goals

- Let CLI users add stable colored space around any one-shot captured image.
- Keep all padding measurements in **physical pixels**, matching the rest of the
  CLI capture model.
- Make padding behavior identical across `region`, `window`, `monitor`, and
  `desktop` capture modes.
- Keep color behavior deterministic across `png`, `jpg/jpeg`, and `bmp`.
- Keep parser, padding policy, and source-rect math testable in `greenflame_core`.
- Keep bitmap allocation, fill, and blit logic in `src/greenflame/win/`.

## Non-Goals For V1

- Interactive-overlay support
- Per-side padding colors
- Alpha or transparency in padding color
- Pattern fills such as checkerboard
- Persisting `--padding-color`

## User-Facing Behavior

### Option availability

`--padding` is available with every CLI capture mode:

- `--region`
- `--window`
- `--monitor`
- `--desktop`

It is invalid with:

- no capture mode
- `--help`
- `--version`

`--padding-color` is valid only when `--padding` is also present in the same
invocation.

### `--padding` grammar

- Long option: `--padding`
- Short option: `-p`

The option accepts exactly **1**, **2**, or **4** comma-separated integers.

1. One value: `<n>`
   - Applies the same padding to all four sides.
   - `n` must be `> 0`.

2. Two values: `<h,v>`
   - `h` applies to both left and right.
   - `v` applies to both top and bottom.
   - `h >= 0`, `v >= 0`, and at least one must be `> 0`.

3. Four values: `<l,t,r,b>`
   - `l` applies to the left side.
   - `t` applies to the top side.
   - `r` applies to the right side.
   - `b` applies to the bottom side.
   - Every value must be `>= 0`, and at least one must be `> 0`.

Whitespace around comma-separated parts should be accepted and trimmed.

### `--padding-color` grammar

`--padding-color` accepts one value in the existing Greenflame config color form:

- `#rrggbb`

Rules:

- exactly 7 characters
- first character must be `#`
- hex digits may be upper- or lowercase
- no alpha component

Examples:

- `#000000`
- `#ffffff`
- `#1a2B3c`

Because `#` has shell-specific meaning in some environments, examples should quote
the color value in user-facing docs unless the surrounding shell is explicitly
`cmd.exe`.

### Config key

Add:

- `save.padding_color`

Rules:

- stored as `#rrggbb`
- default value is `#000000`
- used for CLI padding whenever `--padding` is present and `--padding-color` is not
- normalized the same way as existing color config values

### Examples

```bat
greenflame.exe --desktop --padding 12
greenflame.exe --monitor 2 --padding 24,12 --padding-color "#ffffff"
greenflame.exe --window "Notepad" --padding 8,16,24,32
greenflame.exe --region 1200,100,800,600 --padding 16 --output "D:\shots\crop.png"
```

## Semantics

### High-level model

Padding is not extra captured screen area. Padding is generated output canvas.

The output image is produced in two stages:

1. Build the **source canvas** for the selected capture mode.
2. Add synthetic outer padding around that source canvas using the resolved padding
   color.

### Source canvas model

When `--padding` is present, Greenflame should preserve the **nominal geometry**
of the selected capture request and fill any uncapturable pixels with the resolved
padding color.

Nominal source geometry by mode:

- `--region`: the requested region width and height
- `--window`: the matched window rect width and height
- `--monitor`: the selected monitor bounds width and height
- `--desktop`: the virtual desktop bounds width and height

This means:

- Any real desktop pixels that exist inside the nominal source rect are copied into
  the source canvas.
- Any part of the nominal source rect that lies outside the virtual desktop is
  filled with the resolved padding color.
- For `--window`, if the matched window is partially outside the virtual desktop,
  the output still preserves the full nominal window size; the uncapturable portion
  is filled with the resolved padding color.

This gives the feature one consistent rule across all capture modes.

### Outer padding model

After the source canvas exists, Greenflame allocates the final output canvas:

- `output_width = source_width + left_padding + right_padding`
- `output_height = source_height + top_padding + bottom_padding`

The entire output canvas is first filled with the resolved padding color, then the
source canvas is blitted into it at offset:

- `x = left_padding`
- `y = top_padding`

### Color resolution order

When `--padding` is present, resolve the padding color in this order:

1. `--padding-color`, if present
2. `save.padding_color` from config
3. runtime default `#000000`

`--padding-color` affects only the current invocation and is not persisted.

## Warnings And Error Behavior

### Obscuration warnings

Existing `--window` obscuration warnings remain valid and should stay:

- fully obscured window
- partially obscured window

Padding does not change those capture limitations.

### Off-desktop fill warning

When `--padding` is present and any part of the nominal source rect lies outside
the virtual desktop, the command should succeed if at least one real desktop pixel
is capturable, and should emit a warning that uncovered areas were filled.

Recommended wording:

```text
Warning: Requested capture area extends outside the virtual desktop.
Uncovered areas were filled with the configured padding color.
```

This warning should apply consistently to padded `region`, `window`, `monitor`, or
`desktop` captures if their nominal source geometry extends beyond capturable
bounds.

### Completely outside desktop

If the nominal source rect has no intersection with the virtual desktop at all,
the command should still fail. A fully synthetic image with no captured pixels is
not a useful screenshot result.

### Overflow

The implementation must detect arithmetic overflow when:

- expanding the output canvas with padding
- computing the source-canvas copy offsets
- translating between virtual-desktop and destination coordinates

If any of those calculations overflow 32-bit coordinate math, fail loudly with a
clear CLI error message. The implementation may add a new unique exit code if that
failure does not fit an existing category cleanly.

## Parsing And Validation Design

### Core types

Add an explicit physical-pixel padding type in `greenflame_core`, for example:

```cpp
struct InsetsPx final {
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};
};
```

And store the resolved CLI override color in an explicit field, for example:

```cpp
std::optional<COLORREF> padding_color_override = std::nullopt;
```

### CLI parser changes

- Add a new option spec for `--padding-color`.
- Extend `CliOptions` with:
  - `std::optional<InsetsPx> padding_px`
  - `std::optional<COLORREF> padding_color_override`
- Reject duplicate `--padding`.
- Reject duplicate `--padding-color`.
- Allow `--padding` with any capture mode.
- Reject `--padding-color` unless `--padding` is present.

### Validation rules

Parser-level checks for `--padding`:

- token count must be 1, 2, or 4
- every token must parse as `int32_t`
- no token may be negative
- 1-token form requires `n > 0`
- 2-token and 4-token forms require at least one positive value

Parser-level checks for `--padding-color`:

- value must be present
- value must parse as `#rrggbb`

Cross-option validation:

- `--padding` requires a capture mode
- `--padding-color` requires both a capture mode and `--padding`

## Capture Pipeline Design

### Current limitation

The current capture service is rect-oriented and clips requested screen rects to
the virtual desktop before cropping the captured virtual-desktop bitmap.

That is not enough for padding mode because padded captures now need:

- preserved nominal source geometry
- fill for uncapturable source pixels
- synthetic outer padding

### Recommended capture request model

Instead of passing only a plain screen rect, introduce an explicit capture-render
request, for example:

```cpp
struct CaptureRenderRequest final {
    core::RectPx source_rect_screen = {};
    core::InsetsPx outer_padding_px = {};
    COLORREF fill_color = RGB(0, 0, 0);
    bool preserve_source_extent = false;
};
```

The exact type shape is flexible. The important requirement is that the Win32
capture boundary knows:

- the nominal source rect in screen coordinates
- the amount of synthetic outer padding
- the fill color
- whether off-desktop portions of the source rect must be preserved and filled

### Recommended Win32 algorithm

For a padded capture:

1. Capture the full virtual desktop once, as it does today.
2. Allocate a source canvas sized to the nominal source rect.
3. Fill the source canvas with the resolved padding color.
4. Intersect the nominal source rect with the virtual desktop.
5. Copy any intersecting real desktop pixels from the captured virtual-desktop
   bitmap into the correct offset inside the source canvas.
6. Allocate the final output canvas sized to `source + outer padding`.
7. Fill the final output canvas with the same resolved padding color.
8. Blit the source canvas into the final output canvas at `(left_padding, top_padding)`.
9. Save the final output canvas through the existing file-format encode path.

This keeps all Win32 bitmap work in `src/greenflame/win/` and all policy in
`greenflame_core`.

### Color consistency

Use the same resolved padding color for both:

- uncapturable portions of the nominal source rect
- the synthetic outer padding area

Using one color for both keeps the result visually coherent and avoids inventing
an unnecessary second concept.

## README / Help / Config Impact

When implemented, the following user-facing docs must be updated:

- `README.md`
  - command-line option table
  - examples
  - output behavior notes
  - config table under `save.*`
  - exit-code table if a new exit code is added
- `Build_cli_help_text(...)`
  - add `--padding <n|h,v|l,t,r,b>`
  - add `--padding-color <#rrggbb>`

Recommended help descriptions:

```text
Add synthetic padding around the captured image in physical pixels.
Accepts n, h,v, or l,t,r,b. Valid with any capture mode.
```

```text
Override the padding color for this invocation only.
Accepts #rrggbb. Valid only with --padding.
```

Recommended README config entry:

- `save.padding_color`
  - default `#000000`
  - padding color used by CLI captures when `--padding` is present and
    `--padding-color` is not supplied

## Testing Plan For Implementation

This document does not add code, but the implementation should include both
automated and manual coverage.

### Automated tests

Add parser tests for:

- `--padding` one-value form
- `--padding` two-value form
- `--padding` four-value form with `l,t,r,b`
- trimming whitespace around comma-separated tokens
- rejecting zero-only padding
- rejecting negative values
- rejecting 3-value padding
- accepting `--padding` with `region`, `window`, `monitor`, and `desktop`
- rejecting `--padding` without a capture mode
- accepting valid `--padding-color`
- rejecting invalid `--padding-color`
- rejecting `--padding-color` without `--padding`
- rejecting duplicates of either option

Add controller tests for:

- color resolution order: CLI override, config value, default black
- padded region capture
- padded window capture
- padded monitor capture
- padded desktop capture
- preserving nominal source size when part of the source rect is off-desktop
- adding outer padding around the source canvas
- keeping existing window obscuration warnings
- emitting the new off-desktop fill warning
- failing cleanly when the nominal source rect has no capturable pixels
- failing cleanly on arithmetic overflow

Add Win32 capture tests if a test seam is practical for:

- source-canvas fill
- final-canvas fill
- intersection copy offsets
- source-to-output blit offsets

If some of that bitmap behavior is not practical to cover automatically, add
explicit manual coverage instead.

### Manual coverage to add

When implemented, add manual CLI coverage for:

- `--region` with `--padding`
- `--window` with `--padding`
- `--monitor` with `--padding`
- `--desktop` with `--padding`
- symmetric and asymmetric padding
- default config color
- CLI color override
- partially off-desktop padded captures
- mixed-DPI, multi-monitor validation
- invalid `--padding-color`
- `--padding-color` without `--padding`

## Settled CLI Shape

The user-facing feature is:

- `--padding`
- `--padding-color`
- `save.padding_color`

The semantics are:

- padding is available on every CLI capture mode
- padding is synthetic color only
- the same resolved color fills both uncapturable source pixels and outer padding
- four-value order is `l,t,r,b`
