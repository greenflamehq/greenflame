---
title: CLI Annotation Design
summary: Proposed `--annotate` JSON schema and render pipeline for applying annotations to CLI capture outputs.
audience: contributors
status: proposed
owners:
  - core-team
last_updated: 2026-03-21
tags:
  - cli
  - annotations
  - json
  - proposal
---

# CLI Annotation Design

This document defines the proposed behavior and implementation shape for a new
CLI annotation feature:

- `--annotate "<JSON object>"`
- `--annotate <path-to-json-file>`

The feature applies annotations directly to one-shot CLI captures before the
image is encoded to disk. It is intentionally written as an implementation
handoff for another agent or contributor.

It assumes the current CLI capture pipeline described in
[README.md](../README.md), the existing annotation architecture described in
[docs/annotation_tools.md](annotation_tools.md), the current text and bubble
models described in [docs/text_annotation_design.md](text_annotation_design.md)
and [docs/bubble_annotation_design.md](bubble_annotation_design.md), and the
padding behavior described in
[docs/capture_padding_design.md](capture_padding_design.md).

## Recommendation Summary

- Recommended option name: `--annotate`
- Supported value forms:
  - inline JSON object
  - path to a JSON file
- Recommended top-level shape:

```json
{
  "coordinate_space": "local",
  "color": "#ff0000",
  "highlighter_opacity_percent": 50,
  "font": { "preset": "sans" },
  "annotations": []
}
```

- `annotations` order defines both paint order and bubble numbering order
- All coordinates are integer physical pixels
- Annotation coordinates may be outside the captured image bounds
- Local coordinates are relative to the nominal capture origin
- Global coordinates use `0,0` at the virtual desktop's top-left corner
- All annotations are composited after captured pixels and after any synthetic
  fill or outer padding has been created
- Per-annotation values override document-level defaults
- Document-level defaults override config defaults
- The strict JSON schema artifact should live at
  `schemas/greenflame.annotations.schema.json`
- Manual-test JSON fixtures should live under
  `schemas/examples/cli_annotations/`
- Strict schema is recommended: unknown keys and invalid combinations are fatal
- Brush and freehand Highlighter smoothing should follow the shared freehand
  smoothing path used by interactive commit
- Recommended new exit code: `14` for annotation-input load/validation failure

## Goals

- Let CLI users annotate one-shot captures without entering the interactive
  overlay flow.
- Support every current annotation family:
  - brush
  - highlighter
  - line
  - arrow
  - rectangle
  - filled rectangle
  - ellipse
  - filled ellipse
  - text
  - bubble
- Preserve Greenflame's core correctness rules:
  - physical-pixel coordinate truth
  - explicit coordinate transforms
  - deterministic output under mixed-DPI and multi-monitor layouts
- Make annotations work correctly with existing CLI padding and off-desktop fill.
- Reuse the current committed-annotation raster/composite path wherever
  practical.
- Keep JSON parsing, schema validation, and default resolution in
  `greenflame_core`.
- Keep capture-bitmap allocation, text/bubble rasterization, and final compositing
  in `src/greenflame/win/`.

## Non-Goals For V1

- Interactive import/export of annotation JSON from the overlay UI
- A second CLI option family such as `--annotate-file`
- Arbitrary JSON comments or JSON5 features
- Fractional coordinates
- Alpha values in color strings
- New smoothing algorithms for freehand strokes
- New annotation types beyond the current tool set
- HTML, Markdown, or nested rich-text markup
- Loading font files from disk or bundling fonts with the annotation payload
- Partial success when part of the annotation document is invalid

## Resolved Gaps And Ambiguities

The user requirements leave several important details open. This design resolves
them up front so implementation can stay deterministic.

### Coordinate numbers are integers

All JSON coordinates and sizes are signed 32-bit integers in physical pixels.
Floating-point coordinates are out of scope for V1.

Reason:

- the current annotation truth is integer physical pixels
- the current raster and hit-test model is pixel-oriented
- accepting floats would force new rounding policy at every geometry boundary

### Default `coordinate_space` is `local`

If the top-level `coordinate_space` field is omitted, it defaults to `local`.

Reason:

- most CLI annotation payloads are expected to describe one capture result, not
  one full desktop layout
- local coordinates are easier to generate for scripts that already know the
  capture dimensions

### `font` is a tagged union

The JSON `font` field should be one object with exactly one of:

- `{ "preset": "sans" }`
- `{ "family": "Consolas" }`

Preset values use the existing Greenflame font slots:

- `sans`
- `serif`
- `mono`
- `art`

Reason:

- `font` is one concept, so it should be one property
- lower-level overrides replace one atomic font choice instead of partially
  overriding sibling fields such as `font_preset` and `font_family`
- the common automation-friendly path still reuses `TextFontChoice`
- power users can also request a raw family explicitly

### Rich text uses `spans`

CLI text annotations should support rich text through an ordered `spans` array.

Each span supplies:

- required `text`
- optional inline style flags: `bold`, `italic`, `underline`, and
  `strikethrough`

Reason:

- `spans` is a clearer external file-format term than internal engine terms such
  as `runs`
- it lets the format support rich text without introducing markup parsing
- it maps naturally onto the existing per-run text architecture

### `size` stays a tool-step field for all annotations that support sizing

The JSON `size` field remains the same logical `1..50` size step used by current
tool config.

That means the actual rendered size depends on annotation type:

- brush, line, arrow, rectangle, ellipse: stroke width in px = `size`
- highlighter: stroke width in px = `size + 10`
- bubble: bubble diameter in px = `size + 20`
- text: point size is resolved through the existing text-size step mapping used
  by the GUI Text tool

Reason:

- this preserves semantic parity with the GUI tool model
- it keeps one consistent sizing concept across CLI and interactive flows

### Text escape handling is standard JSON, not custom escaping

Inline strings and JSON-file strings use normal JSON string semantics:

- `\n`, `\t`, `\\`, `\"`, `\uXXXX`, and other standard JSON escapes are decoded
- invalid escapes are hard errors
- after decode, `CRLF` and `CR` are normalized to `LF`

Reason:

- "ignore unknown escapes" is not valid JSON behavior
- strict JSON parsing is clearer and safer for scripts

### Bubble numbering follows array order among bubble annotations only

The first bubble annotation in the `annotations` array is `"1"`, the second is
`"2"`, and so on. Non-bubble annotations do not affect the counter.

Reason:

- this is deterministic
- it matches the user's requirement that numbering increases "as they go"
- it does not require a separate bubble id field in the schema

### Brush and point-list Highlighter smoothing stays shared

Greenflame routes freehand smoothing through the shared interactive path. CLI
brush and point-list highlighter annotations should keep using that same shared
behavior rather than introducing a CLI-only smoother.

Reason:

- the user explicitly raised consistency between interactive and CLI behavior
- freehand smoothing is now implemented once in shared core code
- changing smoothing should continue to update the shared freehand path, not only
  the CLI path

### Unknown keys are errors

Unknown top-level keys, unknown annotation keys, and keys that are invalid for a
given annotation type should all be fatal validation errors.

Reason:

- this is a correctness-first tool
- typos in automation payloads should fail loudly rather than silently being ignored

## User-Facing Behavior

### Option Availability

`--annotate` is valid only with CLI capture modes:

- `--region`
- `--window`
- `--window-hwnd`
- `--monitor`
- `--desktop`

It is invalid with:

- no capture mode
- `--help`
- `--version`

Additional CLI rules:

- no short alias in V1
- may be specified at most once
- both `--annotate=value` and `--annotate value` forms are supported

### Value Interpretation

The option takes one string value. Interpretation is deterministic:

1. Trim leading and trailing whitespace from the raw value.
2. If the first non-whitespace character is `{`, interpret the value as inline JSON.
3. Otherwise, interpret the value as a file path.

This deliberately avoids "if the file exists, use it, otherwise parse JSON"
heuristics, because those produce bad error behavior for misspelled paths.

Important edge case:

- a file path whose first non-whitespace character is `{` is ambiguous and is
  not supported as-is in V1
- if needed, callers can avoid the ambiguity with a prefix such as `.\`
- relative file paths should be resolved against the process current working
  directory through the file-system service

### File Encoding

Annotation JSON files should be read as UTF-8 with optional UTF-8 BOM.

Reason:

- it matches modern Windows editor defaults
- it avoids supporting multiple file encodings in the first version

If future demand requires UTF-16 file input, that should be a follow-up feature,
not implicit V1 behavior.

### Output Semantics

If `--annotate` is present and valid:

1. Resolve the requested capture mode exactly as today.
2. Build the source canvas exactly as today, including any off-desktop fill when
   `preserve_source_extent` is active.
3. Apply any outer padding exactly as today.
4. Composite the resolved annotations on top of the final canvas.
5. Encode the resulting bitmap.

Important rules:

- annotations are never drawn underneath captured pixels
- annotations are never clipped before padding/fill is created
- any portion that lands inside the final output canvas must appear on top of the
  synthetic fill as well as on top of real captured pixels
- later annotations in the JSON array paint on top of earlier annotations
- an empty `annotations` array is valid and produces an unmodified capture

### Examples

Inline JSON:

```bat
greenflame.exe --desktop --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":20,\"y\":20},\"end\":{\"x\":220,\"y\":120},\"size\":4}]}"
```

JSON file:

```bat
greenflame.exe --window "Notepad" --padding 24 --annotate "D:\shots\annotations.json"
```

Local coordinates with text and bubble:

```json
{
  "coordinate_space": "local",
  "font": { "preset": "mono" },
  "annotations": [
    {
      "type": "text",
      "origin": { "x": 12, "y": 16 },
      "color": "#ffffff",
      "size": 12,
      "spans": [
        { "text": "step 1", "bold": true },
        { "text": "\\ncollect logs" }
      ]
    },
    {
      "type": "bubble",
      "center": { "x": 8, "y": 8 },
      "size": 14
    }
  ]
}
```

Global coordinates with highlight over outer padding:

```json
{
  "coordinate_space": "global",
  "highlighter_opacity_percent": 35,
  "annotations": [
    {
      "type": "highlighter",
      "start": { "x": -10, "y": 15 },
      "end": { "x": 140, "y": 15 },
      "size": 12
    }
  ]
}
```

## JSON Schema

### Schema Artifact Location

When this feature is implemented, the machine-readable schema should live at:

- `schemas/greenflame.annotations.schema.json`

Recommended companion fixture location:

- `schemas/examples/cli_annotations/`

Reason:

- `schemas/` is already the repository home for strict JSON schema artifacts
- keeping the annotation schema beside the config schema makes editor/tooling
  discovery easier
- keeping example payloads under `schemas/examples/` gives manual testing one
  stable location that stays close to the schema it exercises

### Top-Level Object

The annotation payload is always one JSON object.

Required field:

- `annotations`

Optional fields:

- `coordinate_space`
- `color`
- `highlighter_opacity_percent`
- `font`

Recommended top-level schema:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `annotations` | array | yes | Ordered annotation list. Order defines paint order. |
| `coordinate_space` | string | no | `"local"` or `"global"`. Defaults to `"local"`. |
| `color` | string | no | Default annotation color for this document. Uses `#rrggbb`. |
| `highlighter_opacity_percent` | integer | no | Default highlighter opacity for this document. Range `0..100`. |
| `font` | font spec object | no | Default font choice for text and bubble annotations. |

Rules:

- top-level object must not contain unknown keys
- `annotations` must be present even if it is empty
- `annotations` must be an array
- `annotations` may be empty

### Shared Primitive Types

#### Color

Color values use the existing Greenflame hex form:

- `#rrggbb`

Rules:

- exactly 7 characters
- first character is `#`
- hex digits may be upper- or lowercase
- no alpha component

#### Point

Reusable point object:

```json
{ "x": 10, "y": -5 }
```

Rules:

- `x` and `y` are signed 32-bit integers
- negative values are allowed
- values larger than the image or desktop are allowed

#### Size

Reusable non-text `size` rules:

- integer
- range `1..50`
- interpretation depends on annotation type

#### Font Spec

Reusable `font` rules:

```json
{ "preset": "mono" }
```

or

```json
{ "family": "Consolas" }
```

Rules:

- `font` must be an object
- it must contain exactly one of `preset` or `family`
- `preset` must be one of `sans`, `serif`, `mono`, `art`
- `family` must be a non-empty string after trimming surrounding whitespace
- `family` should be capped to 128 UTF-16 code units
- an explicitly requested `family` should be treated as required; if DirectWrite
  cannot resolve it to an installed family, the invocation should fail loudly

### Coordinate Spaces

#### `local`

`local` coordinates are relative to the nominal capture origin.

Definitions:

- local `0,0` is the top-left corner of the nominal captured image before outer
  padding is added
- for `--window`, that origin is the matched window rect's top-left
- for `--monitor`, that origin is the selected monitor bounds' top-left
- for `--desktop`, that origin is the virtual desktop bounds' top-left
- for `--region`, that origin is the requested region rect's top-left

This is intentionally the nominal capture origin, not the clipped-to-desktop
origin and not the padded-canvas origin.

#### `global`

`global` coordinates are relative to the virtual desktop's top-left corner.

Definitions:

- global `0,0` is the virtual desktop bounds' top-left corner
- negative global values are still allowed
- values larger than the desktop width or height are still allowed

Important implementation note:

- these are not raw Win32 screen coordinates when the virtual desktop has a
  negative `left` or `top`
- the implementation must translate them into screen coordinates by adding the
  virtual desktop origin

This is the same conceptual origin users naturally expect when thinking about
"the whole desktop" as one positive canvas.

### Coordinate Transform To Output Pixels

Let:

- `virtual_bounds` = virtual desktop bounds in screen coordinates
- `capture_rect` = nominal source rect in screen coordinates
- `padding` = requested outer padding

Then:

- local point to screen point:
  - `screen_x = capture_rect.left + local_x`
  - `screen_y = capture_rect.top + local_y`
- global point to screen point:
  - `screen_x = virtual_bounds.left + global_x`
  - `screen_y = virtual_bounds.top + global_y`

Final compositing then uses the screen-space target bounds:

- `target_left = capture_rect.left - padding.left`
- `target_top = capture_rect.top - padding.top`
- `target_right = capture_rect.right + padding.right`
- `target_bottom = capture_rect.bottom + padding.bottom`

That rule is what allows annotations outside the capture image to appear over
outer padding if enough padding exists.

### Value Resolution Order

#### Color

For every annotation type:

1. annotation-level `color`
2. document-level `color`
3. config default

Config default by type:

- highlighter: current configured highlighter color
- everything else: current configured annotation color

#### Highlighter opacity

For `highlighter` annotations only:

1. annotation-level `opacity_percent`
2. document-level `highlighter_opacity_percent`
3. config `tools.highlighter.opacity_percent`

#### Font

For `text` and `bubble` annotations only:

1. annotation-level `font`
2. document-level `font`
3. config type-specific current font:
   - `tools.text.current_font` for text
   - `tools.bubble.current_font` for bubble

The actual family names are still read from:

- `tools.font.sans`
- `tools.font.serif`
- `tools.font.mono`
- `tools.font.art`

Preset fonts resolve through config to actual family names. Raw-family fonts use
the requested family name directly.

#### Size

For each annotation type that supports `size`:

1. annotation-level `size`
2. config type-specific size step

There is no document-level default `size` in V1.

### Paint Order

The `annotations` array is ordered from back to front.

That means:

- `annotations[0]` paints first
- `annotations[last]` paints last

This matches the current ordered document model used by committed annotations.

## Annotation Types

Every annotation object must contain:

- `type`

Every annotation object must not contain unknown keys.

### Brush

Recommended schema:

```json
{
  "type": "brush",
  "points": [
    { "x": 10, "y": 10 },
    { "x": 20, "y": 14 },
    { "x": 28, "y": 18 }
  ],
  "size": 4,
  "color": "#ff0000"
}
```

Fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `type` | string | yes | Must be `"brush"`. |
| `points` | array of points | yes | Polyline points. Minimum length `1`. |
| `size` | integer | no | Size step `1..50`; width px = `size`. |
| `color` | color string | no | Per-annotation color override. |

Semantics:

- translates to the current round-tip freehand annotation
- uses the shared freehand smoothing mode configured for Brush
- one-point brush input is valid and produces a point-like brush mark

### Highlighter

The highlighter supports exactly one of these shapes:

- `start` + `end`
- `points`

Recommended segment form:

```json
{
  "type": "highlighter",
  "start": { "x": 10, "y": 10 },
  "end": { "x": 140, "y": 20 },
  "size": 12,
  "opacity_percent": 35
}
```

Recommended polyline form:

```json
{
  "type": "highlighter",
  "points": [
    { "x": 10, "y": 10 },
    { "x": 30, "y": 16 },
    { "x": 60, "y": 18 }
  ],
  "size": 12
}
```

Fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `type` | string | yes | Must be `"highlighter"`. |
| `start` | point | conditional | Required only for the segment form. |
| `end` | point | conditional | Required only for the segment form. |
| `points` | array of points | conditional | Required only for the polyline form. Minimum length `1`. |
| `size` | integer | no | Size step `1..50`; actual width px = `size + 10`. |
| `color` | color string | no | Per-annotation color override. |
| `opacity_percent` | integer | no | Per-annotation opacity override. Range `0..100`. |

Validation rules:

- exactly one geometry form is allowed
- `start` and `end` must either both be present or both be absent
- `points` must not appear together with `start` or `end`

Semantics:

- segment form maps to a two-point square-tip freehand annotation
- polyline form maps to a square-tip freehand annotation with the given points
- no additional smoothing is introduced in V1
- opacity affects live saved output exactly like the current highlighter tool

### Line

Recommended schema:

```json
{
  "type": "line",
  "start": { "x": 20, "y": 20 },
  "end": { "x": 180, "y": 70 },
  "size": 3,
  "color": "#00ff00"
}
```

Fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `type` | string | yes | Must be `"line"`. |
| `start` | point | yes | Start point. |
| `end` | point | yes | End point. |
| `size` | integer | no | Size step `1..50`; width px = `size`. |
| `color` | color string | no | Per-annotation color override. |

Semantics:

- maps to the current line annotation with `arrow_head = false`
- zero-length line is allowed and behaves like the current committed line model

### Arrow

Recommended schema:

```json
{
  "type": "arrow",
  "start": { "x": 20, "y": 20 },
  "end": { "x": 180, "y": 70 },
  "size": 3
}
```

Fields are the same as `line`, except:

- `type` must be `"arrow"`

Semantics:

- maps to the current line annotation with `arrow_head = true`

### Rectangle

Recommended schema:

```json
{
  "type": "rectangle",
  "left": 10,
  "top": 20,
  "width": 140,
  "height": 60,
  "size": 2
}
```

Fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `type` | string | yes | Must be `"rectangle"`. |
| `left` | integer | yes | Rectangle left. |
| `top` | integer | yes | Rectangle top. |
| `width` | integer | yes | Width in px. Must be `> 0`. |
| `height` | integer | yes | Height in px. Must be `> 0`. |
| `size` | integer | no | Size step `1..50`; width px = `size`. |
| `color` | color string | no | Per-annotation color override. |

Semantics:

- maps to the current outlined rectangle annotation with `filled = false`
- outline is drawn inward from the dragged outer edge, matching current behavior

### Filled Rectangle

Recommended schema:

```json
{
  "type": "filled_rectangle",
  "left": 10,
  "top": 20,
  "width": 140,
  "height": 60,
  "color": "#0080ff"
}
```

Fields are the same as `rectangle`, except:

- `type` must be `"filled_rectangle"`
- `size` is not allowed

Semantics:

- maps to the current rectangle annotation with `filled = true`

### Ellipse

Recommended schema:

```json
{
  "type": "ellipse",
  "center": { "x": 140, "y": 90 },
  "width": 100,
  "height": 60,
  "size": 2
}
```

Fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `type` | string | yes | Must be `"ellipse"`. |
| `center` | point | yes | Ellipse center. |
| `width` | integer | yes | Width in px. Must be `> 0`. |
| `height` | integer | yes | Height in px. Must be `> 0`. |
| `size` | integer | no | Size step `1..50`; width px = `size`. |
| `color` | color string | no | Per-annotation color override. |

Conversion rule to the existing `RectPx` bounds:

- `left = center.x - (width / 2)`
- `top = center.y - (height / 2)`
- `right = left + width`
- `bottom = top + height`

This matches the current integer geometry style already used by bubble bounds and
keeps behavior deterministic for even and odd sizes.

### Filled Ellipse

Recommended schema:

```json
{
  "type": "filled_ellipse",
  "center": { "x": 140, "y": 90 },
  "width": 100,
  "height": 60
}
```

Fields are the same as `ellipse`, except:

- `type` must be `"filled_ellipse"`
- `size` is not allowed

Semantics:

- maps to the current ellipse annotation with `filled = true`

### Text

Recommended schema:

```json
{
  "type": "text",
  "origin": { "x": 20, "y": 18 },
  "color": "#ffffff",
  "font": { "preset": "mono" },
  "size": 12,
  "spans": [
    { "text": "hello", "bold": true },
    { "text": "\\nworld", "underline": true }
  ]
}
```

Fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `type` | string | yes | Must be `"text"`. |
| `origin` | point | yes | Top-left text origin. |
| `text` | string | conditional | Plain-text shorthand. Mutually exclusive with `spans`. |
| `spans` | array of span objects | conditional | Rich-text form. Mutually exclusive with `text`. |
| `size` | integer | no | Text size step `1..50`, mapped through the existing GUI text-size table. |
| `font` | font spec object | no | Annotation-level font override. |
| `color` | color string | no | Annotation-level color override. |

Text span fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `text` | string | yes | Span text. Must not be empty. |
| `bold` | boolean | no | Bold override for this span. |
| `italic` | boolean | no | Italic override for this span. |
| `underline` | boolean | no | Underline override for this span. |
| `strikethrough` | boolean | no | Strikethrough override for this span. |

Semantics:

- `text` shorthand is equivalent to one span with inherited annotation-level style
- `spans` must be a non-empty array
- text spans are ordered and paint/layout in array order
- adjacent spans with identical effective style should be merged during
  normalization
- text is left-aligned
- there is no automatic wrapping in V1
- the annotation grows down and right from `origin`
- `\n` creates new lines
- `\t` is preserved as a tab character
- `CRLF` and `CR` are normalized to `LF` after decode
- style flags may vary by span
- annotation-level `font`, `size`, and `color` apply uniformly to the
  whole text annotation

Validation rules:

- exactly one of `text` or `spans` is required
- `text` must be non-empty if present
- `spans` must be non-empty if present
- every span must have non-empty `text`
- span objects must not contain unknown keys
- span objects must not contain `color`, `font`, or `size`

Important difference from the interactive tool:

- the CLI `origin` is defined directly as the committed text top-left origin
- it does not inherit the interactive draft click-offset behavior used by the
  live text tool

### Bubble

Recommended schema:

```json
{
  "type": "bubble",
  "center": { "x": 32, "y": 24 },
  "size": 10,
  "font": { "preset": "sans" },
  "color": "#ff0000"
}
```

Fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `type` | string | yes | Must be `"bubble"`. |
| `center` | point | yes | Bubble center. |
| `size` | integer | no | Size step `1..50`; diameter px = `size + 20`. |
| `font` | font spec object | no | Per-annotation font override. |
| `color` | color string | no | Per-annotation color override. |

Semantics:

- bubble value is assigned by array order among bubble annotations
- first bubble renders `"1"`, second `"2"`, and so on
- bubble fill color uses the resolved annotation color
- bubble text color still uses the existing contrast rule from
  `Bubble_text_color`

## Parsing And Validation Design

### CLI Surface

Add one new option:

- `--annotate <json-or-path>`

Recommended `CliOptions` addition:

```cpp
std::optional<std::wstring> annotate_value = std::nullopt;
```

Recommended CLI parser behavior:

- duplicate `--annotate` is an error
- missing value is an error
- `--annotate` without a capture mode is an error

Recommended help text wording:

- `--annotate <json|path>  Apply JSON-defined annotations to the saved CLI capture.`

### Loading Order

The implementation should load and validate annotation input after CLI option
parse succeeds and after the capture target rect is known, but before output-path
reservation or capture work starts.

Reason:

- invalid annotation input should fail before creating reserved placeholder files
- local/global coordinate resolution needs the resolved capture target
- bubble numbering and type-specific defaults can be resolved before capture

### File-System Service Addition

The current `IFileSystemService` does not expose file reading. Add a text-file
read method rather than reaching into Win32 file IO from controller code.

One acceptable shape is:

```cpp
[[nodiscard]] virtual bool Try_read_text_file_utf8(
    std::wstring_view path,
    std::string &utf8_text,
    std::wstring &error_message) const = 0;
```

Any equivalent shape is fine if:

- it is mockable in tests
- it preserves a useful error message
- it keeps path resolution in the service layer

### Recommended Core Types

Recommended new `greenflame_core` model:

```cpp
enum class CliAnnotationCoordinateSpace : uint8_t {
    Local,
    Global,
};

struct CliFontSpec final {
    std::optional<TextFontChoice> preset = std::nullopt;
    std::wstring family = {};
};

struct CliAnnotationDefaults final {
    std::optional<COLORREF> color = std::nullopt;
    std::optional<int32_t> highlighter_opacity_percent = std::nullopt;
    std::optional<CliFontSpec> font = std::nullopt;
};

struct CliAnnotationDocument final {
    CliAnnotationCoordinateSpace coordinate_space =
        CliAnnotationCoordinateSpace::Local;
    CliAnnotationDefaults defaults = {};
    std::vector<CliAnnotationSpec> annotations = {};
};
```

Use separate spec structs per annotation type rather than one loose property bag.
That keeps validation explicit and keeps the translator honest.

### Recommended Parse Result

Return a typed result with path-oriented error reporting.

Examples of good error strings:

- `--annotate: $.annotations[1].size must be an integer in 1..50.`
- `--annotate: $.annotations[3] of type "filled_ellipse" must not contain "size".`
- `--annotate: $.annotations[0] must contain exactly one of "text" or "spans".`
- `--annotate: $.annotations[0].spans[1] must not contain "font".`
- `--annotate: $.annotations[0].font must contain exactly one of "preset" or "family".`
- `--annotate: unable to read annotation file "D:\shots\a.json": access denied.`

### Schema Validation Rules

The parser should validate all of the following:

- top-level value is an object
- `annotations` exists and is an array
- top-level keys are known
- every annotation value is an object
- every annotation object contains `type`
- `type` is one of the supported strings
- annotation keys are valid for that `type`
- required fields exist for that `type`
- forbidden fields for that `type` are rejected
- all numeric fields are integers
- all coordinates fit signed 32-bit range
- all widths and heights fit signed 32-bit range and are positive where required
- all `size` values are `1..50`
- all highlighter opacity values are `0..100`
- all `font` objects contain exactly one of `preset` or `family`
- all preset font values are valid slot tokens
- all explicit family strings are non-empty after trimming
- all explicit family strings respect the configured length cap
- all color strings are valid `#rrggbb`
- all geometry conversions are overflow-checked
- highlighter uses exactly one geometry form
- text uses exactly one of `text` or `spans`

Installed-font availability for explicit family names may be checked after JSON
schema validation, during translation or rasterization. If an explicitly
requested family is not installed, the invocation should fail with a clear
annotation-input error rather than silently substituting another family.

### Overflow Rules

The parser or translator must fail loudly if any of these overflow 32-bit math:

- converting ellipse center/width/height into bounds
- converting local/global coordinates into screen coordinates
- expanding the final target bounds by padding
- building annotation bounds used for compositing

### Bubble Number Assignment

Bubble numbering should happen only after the full document validates. That keeps
the numbering deterministic and avoids accidental partial state.

Recommended assignment rule:

- scan `annotations` in order
- maintain `next_bubble = 1`
- for each `bubble`, assign the current value and increment

## Rendering And Architecture

### High-Level Pipeline

Recommended pipeline:

1. Parse `--annotate` into a typed `CliAnnotationDocument`.
2. Resolve document-level and config-level defaults.
3. Translate all coordinates into screen-space annotation geometry.
4. Convert the resolved CLI annotations into the existing committed annotation
   model wherever practical.
5. Build the source canvas and outer padding exactly as today.
6. Rasterize text and bubble annotations as needed.
7. Blend the full annotation list onto the final output pixels.
8. Encode the final bitmap.

### Reuse Of Existing Annotation Model

The best reuse path is to translate CLI annotations into the existing committed
annotation variants:

- brush -> `FreehandStrokeAnnotation`
- highlighter -> `FreehandStrokeAnnotation` with square tip and resolved opacity
- line -> `LineAnnotation` with `arrow_head = false`
- arrow -> `LineAnnotation` with `arrow_head = true`
- rectangle -> `RectangleAnnotation` with `filled = false`
- filled rectangle -> `RectangleAnnotation` with `filled = true`
- ellipse -> `EllipseAnnotation` with `filled = false`
- filled ellipse -> `EllipseAnnotation` with `filled = true`
- text -> `TextAnnotation`
- bubble -> `BubbleAnnotation`

This is strongly preferred over building a second independent raster/composite path.

However, text and bubble are not a byte-for-byte fit with the current shared
model once raw family names are allowed. The current shared text types already
 support per-run style flags, which is the right shape for the constrained span
 model in this spec, but they do not yet carry a raw-family-capable font
 descriptor. The implementation should extend the shared text/bubble base-style
 model rather than inventing a CLI-only text representation.

### Capture Request Extension

Extend `CaptureSaveRequest` so the save path can receive annotations:

```cpp
struct CaptureSaveRequest final {
    RectPx source_rect_screen = {};
    InsetsPx padding_px = {};
    COLORREF fill_color = static_cast<COLORREF>(0);
    bool preserve_source_extent = false;
    std::vector<Annotation> annotations = {};
};
```

Any equivalent ownership shape is acceptable if:

- the save path receives the ordered annotation list
- the annotations are already translated into screen/global coordinates
- the save path can mutate a local copy for text/bubble rasterization

### Why Screen-Space Translation Should Happen Before Save

Current committed annotations already assume one physical-pixel coordinate space.
Translating the CLI JSON to screen/global coordinates before save keeps the render
path simple:

- `Blend_annotations_onto_pixels(...)` already expects one target-bounds rect in
  the same coordinate space as the annotations
- the final save canvas can treat outer padding as an expansion of the same
  target rect
- local/global JSON handling stays out of the Win32 bitmap code

### Final Target Bounds For Blending

The compositing pass should blend onto:

- `capture_rect` when there is no outer padding
- `capture_rect` expanded by the requested padding when outer padding exists

That rule is what makes annotations render over outer padding.

It also means the save path should composite after the outer padded bitmap exists,
not before.

### Text And Bubble Rasterization

Bubble rasterization already exists via `ITextLayoutEngine::Rasterize_bubble(...)`.

Text is close but not fully ready for CLI because current committed text
rasterization expects measured `visual_bounds` to already exist, and the current
shared text style model is still oriented around preset font choices rather than
explicit family names.

Recommended approach:

- reuse the existing `D2DTextLayoutEngine`
- add one helper for CLI committed text that can:
  - measure ordered spans that vary only by style flags
  - set `visual_bounds`
  - rasterize the bitmap
- extend the shared text and bubble style model so a resolved explicit family
  name can flow through committed annotations

Do not route CLI text through the interactive draft editor.

Reason:

- CLI annotations are already fully specified
- the draft editor contains input-state concerns that do not apply here

### Brush/Highlighter Smoothing

Do not invent a CLI-only smoothing path.

Current behavior:

- brush point lists use the shared Brush freehand smoothing mode
- point-list highlighters use the shared Highlighter freehand smoothing mode
- highlighter segment input becomes a two-point square-tip stroke
- straightened or explicit segment highlighters bypass freehand smoothing

## Error Handling And Exit Codes

### Recommended New Exit Code

Add:

- `14` = annotation input could not be loaded, parsed, or validated

Recommended enum name:

- `CliAnnotationInputInvalid = 14`

Reason:

- malformed or unreadable annotation input is a distinct class of user error
- it is not the same as generic argument parse failure once the CLI parser itself
  has already accepted the option syntax

### Existing Exit Codes That Should Still Apply

- output path resolution/reservation failures remain `10`
- capture, rasterization, compositing, and encode failures remain `11`

### Failure Policy

Any annotation-input failure must abort the whole invocation.

There is no partial success in V1:

- no partially applied annotation documents
- no "skip bad annotation and continue"
- no output file on validation failure beyond normal reserved-file cleanup

## Documentation Updates Required By The Feature

If and when the feature is implemented, the implementation must update:

- `README.md`
  - command-line options table
  - examples
  - exit code table
  - configuration notes if needed
- `docs/manual_test_plan.md`
  - new manual cases for CLI annotation rendering
- `schemas/greenflame.annotations.schema.json`
  - strict schema for annotation JSON
- `schemas/examples/cli_annotations/`
  - checked-in manual-test fixture payloads referenced by docs and test plans

## Manual-Test Fixtures And Commands

This section defines concrete example payloads and command lines that can be
used during implementation and review. The intent is to give manual testing
stable inputs with clearly described expected output.

Assumptions for the commands below:

- the current working directory is the repository root
- `greenflame.exe` is either on `PATH` or replaced with the full build output
  path
- `%TEMP%` exists and is writable

### Example JSON Fixtures

#### `schemas/examples/cli_annotations/local_mixed_edge_cases.json`

Purpose:

- exercises local coordinates
- exercises negative coordinates that should render into outer padding
- exercises bubble numbering with interleaved non-bubble annotations
- exercises rich text spans, `\n`, and `\t`
- exercises mixed annotation ordering on one capture

Expected result when used with the recommended command below:

- the saved image succeeds with exit code `0`
- a red rectangle outline begins partly above and left of the captured image,
  so its clipped portions remain visible inside the added top and left padding
- a yellow highlighter stroke begins in the left padding, crosses into the
  captured image, and keeps the configured translucent look
- a cyan arrow points back toward the upper-left portion of the capture
- white mono text appears as three lines near the lower-left quadrant:
  `Step 1` is bold, `Collect<TAB>logs` is underlined, and `Confirm repro` is
  italic
- the first bubble renders `1` near the top-left corner and the second bubble
  renders `2` near the bottom-right area, proving that only bubble annotations
  increment the counter
- a thin green line crosses the lower portion of the canvas and remains visible
  in left padding where it extends past the capture bounds

#### `schemas/examples/cli_annotations/global_padding_edge_cases.json`

Purpose:

- exercises global coordinates relative to virtual-desktop top-left
- exercises negative global coordinates that should render into desktop padding
- exercises a raw installed font family for text
- exercises multiple annotation types on a desktop capture

Expected result when used with the recommended command below:

- the saved image succeeds with exit code `0`
- a translucent pink highlighter begins in the left padding area outside the
  captured desktop image and continues across the desktop near the top edge
- a filled yellow rectangle is partially visible inside the left padding,
  proving that global negative coordinates are composited after padding/fill
- a green ellipse outline is visible on the desktop image
- text near the desktop origin uses the explicit `Segoe UI` family and spans two
  lines, with the first line bold and the second italic
- the first bubble renders `1` near the desktop origin and the second bubble
  renders `2` farther to the right

#### `schemas/examples/cli_annotations/invalid_unknown_key.json`

Purpose:

- exercises strict unknown-key rejection on a minimal payload

Expected result when used with the recommended command below:

- the command fails with exit code `14`
- no output image is produced
- the error clearly points at the unexpected `colour` property on the line
  annotation

#### `schemas/examples/cli_annotations/invalid_missing_font_family.json`

Purpose:

- exercises the explicit-family hard-failure rule

Expected result when used with the recommended command below:

- the command fails with exit code `14`
- no output image is produced
- the error clearly states that the requested font family is unavailable rather
  than silently substituting another font

### Manual Command-Line Examples

#### 1. Inline JSON sanity check

```bat
greenflame.exe --region 100,100,220,160 --output "%TEMP%\greenflame-inline-line.png" --overwrite --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":219,\"y\":159},\"size\":4,\"color\":\"#ff0000\"}]}"
```

Expected result:

- exit code `0`
- `%TEMP%\greenflame-inline-line.png` is created
- one red line runs from the top-left corner of the captured region to the
  bottom-right corner

#### 2. Inline JSON padding edge case

```bat
greenflame.exe --region 100,100,220,160 --padding 32 --padding-color "#202020" --output "%TEMP%\greenflame-inline-padding.png" --overwrite --annotate "{\"annotations\":[{\"type\":\"rectangle\",\"left\":-16,\"top\":-12,\"width\":120,\"height\":72,\"size\":3,\"color\":\"#00ff00\"},{\"type\":\"bubble\",\"center\":{\"x\":-4,\"y\":-4},\"size\":12}]}"
```

Expected result:

- exit code `0`
- `%TEMP%\greenflame-inline-padding.png` is created
- the rectangle outline extends into the top and left synthetic padding
- bubble `1` is centered near the padded top-left corner, with part of the
  bubble living outside the original capture area

#### 3. File-based local mixed fixture

```bat
greenflame.exe --region 100,100,240,180 --padding 40 --padding-color "#202020" --output "%TEMP%\greenflame-local-mixed.png" --overwrite --annotate ".\schemas\examples\cli_annotations\local_mixed_edge_cases.json"
```

Expected result:

- exit code `0`
- `%TEMP%\greenflame-local-mixed.png` is created
- output matches the detailed expectations listed for
  `local_mixed_edge_cases.json`

#### 4. File-based global desktop fixture

```bat
greenflame.exe --desktop --padding 64 --padding-color "#101010" --output "%TEMP%\greenflame-global-padding.png" --overwrite --annotate ".\schemas\examples\cli_annotations\global_padding_edge_cases.json"
```

Expected result:

- exit code `0`
- `%TEMP%\greenflame-global-padding.png` is created
- output matches the detailed expectations listed for
  `global_padding_edge_cases.json`

#### 5. Invalid file path

```bat
greenflame.exe --desktop --output "%TEMP%\greenflame-missing-file.png" --overwrite --annotate ".\schemas\examples\cli_annotations\does_not_exist.json"
```

Expected result:

- exit code `14`
- no output image is produced
- the error reports that the annotation file could not be read

#### 6. Malformed inline JSON

```bat
greenflame.exe --desktop --output "%TEMP%\greenflame-malformed-json.png" --overwrite --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":10,\"y\":10}}"
```

Expected result:

- exit code `14`
- no output image is produced
- the error reports malformed JSON rather than a generic capture/save failure

#### 7. File-based unknown-key rejection

```bat
greenflame.exe --region 100,100,220,160 --output "%TEMP%\greenflame-invalid-unknown-key.png" --overwrite --annotate ".\schemas\examples\cli_annotations\invalid_unknown_key.json"
```

Expected result:

- exit code `14`
- no output image is produced
- the error reports the unknown `colour` key

#### 8. File-based unavailable raw-family rejection

```bat
greenflame.exe --region 100,100,220,160 --output "%TEMP%\greenflame-invalid-font.png" --overwrite --annotate ".\schemas\examples\cli_annotations\invalid_missing_font_family.json"
```

Expected result:

- exit code `14`
- no output image is produced
- the error reports the unavailable explicit family instead of silently falling
  back to another font

## Testing Plan

### Unit Tests

Add unit coverage for:

- CLI option parsing:
  - `--annotate` accepted with value
  - duplicate `--annotate` rejected
  - `--annotate` without capture mode rejected
- annotation-source interpretation:
  - inline JSON when the value starts with `{`
  - file path otherwise
- JSON parser and schema validation:
  - unknown keys rejected
  - invalid type names rejected
  - missing required fields rejected
  - forbidden `size` on filled shapes rejected
  - highlighter geometry exclusivity enforced
  - invalid colors rejected
  - invalid `font` object shape rejected
  - invalid preset font tokens rejected
  - empty raw-family names rejected
  - unavailable raw-family names rejected
  - invalid `size` and opacity ranges rejected
  - `text` versus `spans` mutual exclusivity enforced
  - span-level `color`, `font`, and `size` rejected
- default resolution:
  - document-level overrides
  - per-annotation overrides
  - fallback to config defaults
- coordinate transforms:
  - local to screen
  - global to screen using virtual desktop origin
  - clipping behavior with and without outer padding
- bubble numbering:
  - counts only bubbles
  - respects array order
- text normalization:
  - `\n` preserved
  - `\t` preserved
  - `CRLF` and `CR` normalized
  - adjacent equivalent spans merged
- app-controller flow:
  - invalid annotation input fails before output path reservation
  - valid annotation input reaches the save request

### Save-Pipeline Tests

Add Win32 or service-level coverage for:

- annotations composited over real captured pixels
- annotations composited over outer padding
- annotations composited over off-desktop fill when `preserve_source_extent`
  is active
- text and bubble rasterization in the save path

### Manual Test Coverage

Add manual test cases for:

- the checked-in fixture files and command lines listed in
  `cli_annotation_design.md`
- inline JSON input
- file-based JSON input
- local coordinates on each capture mode
- global coordinates on mixed-monitor desktop layouts
- negative annotation coordinates with padding
- annotations extending beyond the image on each side
- point-list highlighter versus segment highlighter
- bubble numbering with interleaved non-bubble annotations
- text with `\n` and `\t`
- rich text with multi-span bold/italic/underline/strikethrough
- text `size` matching the GUI step-to-point mapping
- raw-family text and bubble fonts
- unavailable raw-family failure
- invalid annotation file path
- malformed JSON
- schema typo failure such as `colour` instead of `color`

## Notes For The Implementing Agent

- Reuse the existing JSON parser already present in the repository. Do not add a
  second JSON library.
- Keep coordinate math explicit and centralized. This feature introduces one more
  coordinate boundary, and it should be handled in one place.
- The most delicate part of the feature is not the CLI parser. It is the seam
  between:
  - normalized JSON coordinate space
  - screen/global annotation space
  - final padded output canvas
- The easiest way to get this wrong is to composite before padding or to forget
  the virtual desktop origin when translating `global` coordinates.
- The current `--region` CLI path does not appear to normalize through the
  virtual desktop origin today. This feature should not silently reuse that
  assumption for annotation globals. The annotation spec here is intentionally
  defined relative to virtual desktop top-left.
