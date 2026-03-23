---
title: CLI Annotations
summary: User-facing guide for the `--annotate` CLI render option.
audience: users
status: authoritative
owners:
  - core-team
last_updated: 2026-03-22
tags:
  - cli
  - annotations
  - json
---

# CLI Annotations

Greenflame can apply annotations directly to one-shot CLI render flows with:

```bat
greenflame.exe --annotate "<JSON object>"
greenflame.exe --annotate <path-to-json-file>
```

This feature uses the same annotation families as the GUI tools, but applies
them directly to the saved output image.

## Quick Start

Inline JSON:

```bat
greenflame.exe --desktop --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":20,\"y\":20},\"end\":{\"x\":220,\"y\":120},\"size\":4,\"color\":\"#ff00c8\"}]}"
```

JSON file:

```bat
greenflame.exe --region 100,100,240,180 --padding 40 --padding-color "#202020" --output "%TEMP%\greenflame-annotated.png" --overwrite --annotate ".\schemas\examples\cli_annotations\local_mixed_edge_cases.json"
```

Imported image:

```bat
greenflame.exe --input "%TEMP%\greenflame-source.png" --overwrite --annotate ".\schemas\examples\cli_annotations\local_mixed_edge_cases.json"
```

## Input Rules

- `--annotate` is valid only with a CLI render source:
  - `--region`
  - `--window`
  - `--window-hwnd`
  - `--monitor`
  - `--desktop`
  - `--input`
- `--input` requires `--annotate`.
- `--input` also requires either `--output` or `--overwrite`.
- `--input` is incompatible with live capture modes and with `--window-capture`.
- If the first non-whitespace character is `{`, the value is treated as inline
  JSON.
- Otherwise, the value is treated as a file path.
- Annotation files must be UTF-8 JSON. UTF-8 BOM is accepted.
- Invalid annotation input fails the command with exit code `14`.

## Schema And Examples

- Tooling schema: `schemas/greenflame.annotations.schema.json`
- Example payloads: `schemas/examples/cli_annotations/`

The checked-in examples include valid local/global fixtures, a padding-focused
brush fixture, and invalid payloads for manual failure testing.

## Top-Level Shape

Annotation input is a JSON object with this general structure:

```json
{
  "coordinate_space": "local",
  "color": "#ff0000",
  "highlighter_opacity_percent": 50,
  "font": { "preset": "sans" },
  "annotations": []
}
```

Top-level fields:

- `annotations`
  - Required array of annotations, in paint order.
- `coordinate_space`
  - Optional.
  - `local` or `global`.
  - Defaults to `local`.
- `color`
  - Optional default color for annotations.
- `highlighter_opacity_percent`
  - Optional default highlighter transparency.
- `font`
  - Optional default font for text and bubble annotations.

## Defaults And Overrides

Resolved values follow this order:

- Annotation-level value
- Document-level value
- Greenflame config default

This applies to:

- `color`
- `highlighter_opacity_percent`
- `font`
- `size` for annotation types that support it

## Coordinate Spaces

All coordinates are integer physical pixels.

- `local`
  - For live capture modes, `0,0` is the top-left of the requested capture.
  - For `--input`, `0,0` is the top-left of the decoded source image before any
    padding is added.
- `global`
  - `0,0` is the top-left of the virtual desktop.
  - `global` is valid only for live capture modes.
  - `global` is invalid with `--input` and fails with exit code `14`.

Coordinates may be outside the capture bounds. Negative coordinates and
coordinates beyond the captured width or height are valid.

When `--padding` is used, annotations that extend outside the captured image are
still rendered over the synthetic padding area. They are not clipped to the
original capture rectangle.

Imported images must decode fully opaque in V1. If an input image contains any
non-opaque alpha, the command fails with exit code `16`.

## Annotation Types

Supported annotation types:

- `brush`
  - `points`, `size`, optional `color`
- `highlighter`
  - Either `start`/`end` or `points`
  - `size`, optional `color`, optional `highlighter_opacity_percent`
- `line`
  - `start`, `end`, `size`, optional `color`
- `arrow`
  - `start`, `end`, `size`, optional `color`
- `rectangle`
  - `left`, `top`, `width`, `height`, `size`, optional `color`
- `filled_rectangle`
  - `left`, `top`, `width`, `height`, optional `color`
- `ellipse`
  - `center`, `width`, `height`, `size`, optional `color`
- `filled_ellipse`
  - `center`, `width`, `height`, optional `color`
- `text`
  - `origin`, `size`, optional `color`, optional `font`, plus either `text` or
    `spans`
- `bubble`
  - `center`, `size`, optional `color`, optional `font`

Rules:

- `size` is valid only for annotations that support sizing.
- Filled shapes do not accept `size`.
- Brush and point-list highlighter rendering uses the current pass-through
  stroke behavior. No extra smoothing is applied.
- Bubble numbering is assigned automatically by bubble order only:
  first bubble is `1`, second is `2`, and so on.

## Colors, Transparency, And Size

- Colors use `#rrggbb`.
- Alpha is not encoded in the color string.
- Highlighter transparency uses `highlighter_opacity_percent` in the range
  `0..100`.
- `size` uses the same `1..50` tool-step concept as the GUI.

Size behavior by annotation family:

- Brush, line, arrow, rectangle, ellipse
  - `size` controls stroke width.
- Highlighter
  - `size` controls the highlighter stroke width using the same GUI mapping.
- Bubble
  - `size` controls bubble size using the same GUI mapping.
- Text
  - `size` controls text size using the same GUI text-size mapping.

## Text And Fonts

Text annotations support either plain text or rich text spans.

Plain text:

```json
{
  "type": "text",
  "origin": { "x": 20, "y": 20 },
  "size": 18,
  "text": "Hello\nworld"
}
```

Rich text:

```json
{
  "type": "text",
  "origin": { "x": 20, "y": 20 },
  "size": 18,
  "spans": [
    { "text": "Step 1", "bold": true },
    { "text": "\nCollect logs", "underline": true },
    { "text": "\nConfirm repro", "italic": true }
  ]
}
```

Text rules:

- `text` and `spans` are mutually exclusive.
- Each span must contain `text`.
- Span-level styling is limited to:
  - `bold`
  - `italic`
  - `underline`
  - `strikethrough`
- Spans cannot override `color`, `font`, or `size`.
- Strings use normal JSON escaping. Invalid JSON escapes are errors.

Fonts use a tagged union:

Preset font:

```json
{ "preset": "mono" }
```

Explicit installed family:

```json
{ "family": "Consolas" }
```

Preset values:

- `sans`
- `serif`
- `mono`
- `art`

If an explicit family is not installed, the command fails with exit code `14`.

## Validation

Validation is strict.

- Unknown properties are errors.
- Invalid property combinations are errors.
- Wrong types are errors.
- Malformed JSON is an error.
- Missing required fields are errors.

Greenflame does not partially apply a document. If any part of the annotation
payload is invalid, the command fails.

## Rendering Order

For CLI render flows, Greenflame builds the output in this order:

1. Source image pixels
2. Synthetic fill for off-desktop regions, when applicable for live captures
3. Outer padding, when applicable
4. Annotations

That means annotations always appear on top of the final saved image, including
any padded area.

## Related Files

- User examples: `schemas/examples/cli_annotations/`
- Manual verification cases: `docs/manual_test_plan.md`
- Implementation design note: `docs/cli_annotation_design.md`
