# Greenflame Annotation Language

Use this document as the primary reference for `--annotate` payloads. Read the bundled schema only when you need the exact contract or need to debug a validation failure.

## Top-Level Shape

Annotation input is a JSON object. `annotations` is required.

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

- `annotations`: required array, painted in order
- `coordinate_space`: optional, `local` or `global`, defaults to `local`
- `color`: optional default annotation color
- `highlighter_opacity_percent`: optional default highlighter transparency, `0..100`
- `font`: optional default font for text and bubble annotations

If the `--annotate` value begins with `{` after trimming whitespace, Greenflame treats it as inline JSON. Otherwise it treats the value as a file path.

## Defaults And Overrides

Resolved values use this order:

1. annotation-level value
2. document-level value
3. Greenflame config default

This applies to:

- `color`
- `highlighter_opacity_percent`
- `font`
- `size` for annotation types that support it

## Coordinate Spaces

All coordinates are integer physical pixels.

- `local`
  - live capture modes: `0,0` is the top-left of the requested capture
  - `--input`: `0,0` is the top-left of the decoded source image before any padding is added
- `global`
  - `0,0` is the top-left of the virtual desktop
  - valid only for live capture modes
  - invalid with `--input` and fails with exit `14`

Coordinates may be outside the capture bounds. Negative coordinates and coordinates beyond the captured width or height are valid.

When `--padding` is used, annotations can render over the synthetic padding area. This is the preferred way to place short labels in empty space without covering source UI text.

## Supported Annotation Types

| Type | Required fields | Optional fields |
|---|---|---|
| `brush` | `points` | `size`, `color` |
| `highlighter` | `start` and `end`, or `points` | `size`, `color`, `opacity_percent` |
| `line` | `start`, `end` | `size`, `color` |
| `arrow` | `start`, `end` | `size`, `color` |
| `rectangle` | `left`, `top`, `width`, `height` | `size`, `color` |
| `filled_rectangle` | `left`, `top`, `width`, `height` | `color` |
| `ellipse` | `center`, `width`, `height` | `size`, `color` |
| `filled_ellipse` | `center`, `width`, `height` | `color` |
| `obfuscate` | `left`, `top`, `width`, `height` | `size` |
| `text` | `origin`, plus `text` or `spans` | `size`, `color`, `font` |
| `bubble` | `center` | `size`, `color`, `font` |

Rules:

- `width` and `height` must be positive integers.
- Filled shapes do not accept `size`.
- Obfuscate does not accept `color`, `font`, or highlighter opacity settings.
- `text` and `spans` are mutually exclusive.

## Size Mappings

`size` is always a step in the range `1..50`, but each annotation family interprets that step differently.

- brush, line, arrow, rectangle, ellipse: `size` maps directly to stroke width in pixels
- highlighter: physical stroke width is `size + 10` pixels
- bubble: physical diameter is `size + 20` pixels
- obfuscate: `1` means blur, `2..50` means block pixelation
- text: `size` maps to a point-size table

Text point-size table:

```text
1:5   2:6   3:7   4:8   5:9   6:10  7:11  8:12  9:13  10:14
11:15 12:16 13:17 14:18 15:19 16:20 17:21 18:22 19:23 20:24
21:26 22:28 23:31 24:34 25:36 26:40 27:43 28:47 29:51 30:55
31:60 32:65 33:71 34:77 35:83 36:90 37:98 38:107 39:116 40:126
41:137 42:149 43:161 44:175 45:190 46:207 47:225 48:244 49:265 50:288
```

## Colors, Transparency, And Fonts

- Colors use `#rrggbb`.
- Alpha is not encoded in the color string.
- Highlighter transparency uses `opacity_percent` or document-level `highlighter_opacity_percent`, both `0..100`.

Fonts use a tagged union:

```json
{ "preset": "mono" }
```

or

```json
{ "family": "Consolas" }
```

Preset values:

- `sans`
- `serif`
- `mono`
- `art`

Explicit font families must be installed on the system. Missing explicit families fail with exit `14`.

## Text And Spans

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
    { "text": "\nCollect logs", "underline": true }
  ]
}
```

Span rules:

- each span must contain `text`
- only `bold`, `italic`, `underline`, and `strikethrough` are allowed
- spans cannot override `color`, `font`, or `size`

Bubble numbering is automatic. The first bubble becomes `1`, the second `2`, and so on.

## Validation Rules

Validation is strict.

- unknown properties are errors
- malformed JSON is an error
- wrong types are errors
- invalid field combinations are errors
- missing required fields are errors

Greenflame does not partially apply a document. If any part of the payload is invalid, the whole command fails.

Common failures:

- `coordinate_space: "global"` with `--input`
- using `size` on `filled_rectangle` or `filled_ellipse`
- adding `color` or `font` to `obfuscate`
- providing both `text` and `spans`
- using an explicit font family that is not installed
- using unsupported properties not listed in the schema

## Practical Guidance

- Default to `coordinate_space: "local"`.
- Use callouts first. A rectangle plus arrow or bubble is usually clearer than long text.
- Keep labels short and specific.
- If labels would sit on top of source text, add `--padding` and place labels in the margin instead.
- Prefer annotation files for anything non-trivial; inline JSON is harder to quote correctly.
- Use obfuscate only when the user explicitly asks for it.

## Related Files

- schema: [greenflame.annotations.schema.json](greenflame.annotations.schema.json)
- examples: [examples/simple-callouts-local.json](examples/simple-callouts-local.json), [examples/short-label-local.json](examples/short-label-local.json), [examples/capture-analyze-annotate-local.json](examples/capture-analyze-annotate-local.json), [examples/obfuscate-by-request-local.json](examples/obfuscate-by-request-local.json)
