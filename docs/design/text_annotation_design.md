---
title: Text Annotation Design
summary: Current reference for Greenflame's interactive text annotation workflow, re-edit behavior, and implementation boundaries.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - annotations
  - text
  - clipboard
  - spell-check
---

# Text Annotation Design

This document describes the current shipped Text tool implementation in the
interactive overlay.

It replaces the earlier proposal-style version of this file. The implementation,
tests, and higher-priority reference docs are authoritative when this document is
updated.

For shared annotation architecture, see [docs/annotation_tools.md](../annotation_tools.md).
For the Text/Bubble hub-and-ring selection wheel, see
[text_style_wheel_redesign.md](text_style_wheel_redesign.md).

## Current Status

Text annotations are shipped and support both:

- creating a new text annotation from the Text tool
- re-editing a committed text annotation in place

Two areas in particular drifted from the original proposal and are now part of
the current implementation:

- committed text is re-editable
- draft clipboard operations round-trip rich text style flags through RTF and
  HTML clipboard formats, not just plain text

The current implementation keeps one uniform base style per annotation for:

- color
- font slot / resolved font family
- point size

Per-range variation is limited to the four text style flags:

- bold
- italic
- underline
- strikethrough

## User Model

### Armed Text tool

With the Text tool active and no draft open:

- the overlay shows an `I-beam` cursor plus the existing text placement preview
- left-click inside the capture selection starts a new draft
- right-click or bare `Tab` opens the Text style wheel
- mouse-wheel and `Ctrl+=` / `Ctrl+-` change the persisted text size step

Each new draft inherits its base style from the current tool defaults:

- color from the shared annotation color selection
- font slot from `tools.text.current_font`
- point size from `tools.text.size`, mapped through
  `kTextSizePtTable` (`1..50` -> `5..288 pt`)

Current default behavior is:

- `tools.text.size = 10`
- step `10` maps to `14 pt`

Draft placement uses the clicked cursor position plus a small implementation
offset so the preview glyph and the actual text baseline align visually.

### Live draft editing

While a draft is active, `TextEditController` owns text editing behavior.

Current editing behavior includes:

- text insertion through `WM_CHAR`
- caret movement by character, word, line, and document
- mouse selection using layout-engine hit testing
- backspace/delete, including word variants
- insert vs. overwrite mode
- `Ctrl+B`, `Ctrl+I`, `Ctrl+U`, and `Alt+Shift+5`
- `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V`
- draft-local `Ctrl+Z` and `Ctrl+Shift+Z`
- `Ctrl+Enter` newline insertion
- `Enter` commit
- `Esc` cancel

Current draft rules:

- right-click does not open the style wheel while editing
- mouse-wheel and `Ctrl+=` / `Ctrl+-` do not change text size while editing
- overlay-global shortcuts such as screenshot save/copy and session undo/redo are
  suppressed while the text editor has focus
- explicit newlines are supported and layout remains `no-wrap`

Pointer behavior while a draft is active:

- clicking inside the draft moves the caret or starts a selection drag
- clicking outside the draft commits a non-empty draft or discards an empty one
- when the Text tool remains active and the outside click lands inside the
  capture selection, a fresh draft starts at that click point
- clicking a toolbar button first commits or discards the current draft, then
  applies the toolbar action without starting a replacement draft

### Spell-check behavior

Live spell-check is optional and applies only while editing.

Current behavior:

- language tags come from `tools.text.spell_check_languages`
- `OverlayWindow` rebuilds the spell-check service when that config changes
- `TextEditController` requests spell results on layout rebuild
- squiggles are visible only in the live draft view
- committed annotations, saved output, and copied images never include squiggles
- when multiple languages are configured, a word is flagged only if every active
  checker reports it as misspelled

### Clipboard behavior

Clipboard behavior is richer than the original proposal.

Current copy/cut behavior:

- `Ctrl+C` and `Ctrl+X` export the selected text as:
  - `CF_UNICODETEXT`
  - `CF_RTF`
  - Windows `"HTML Format"`
- when there is no selection, Greenflame leaves the clipboard unchanged

Current paste behavior:

- `Ctrl+V` tries `CF_RTF` first
- if no usable RTF is present, it tries HTML clipboard data
- if no usable rich format is present, it falls back to plain text

Current fidelity rules:

- bold, italic, underline, and strikethrough are preserved through rich-text
  copy/paste
- font face, point size, and color are intentionally discarded on rich-text
  import
- plain-text paste uses the current typing style
- incoming `CRLF` and bare `CR` are normalized to internal `LF`

## Re-editing Committed Text

Committed text annotations are re-editable in the current implementation.

Re-edit entry rules:

- no annotation tool may be active
- no text draft may already be active
- exactly one annotation must be selected
- that selected annotation must be a `TextAnnotation`
- double-click enters text editing at the clicked hit-tested position

This works for both already-selected and newly double-clicked text annotations,
because the normal default-mode click path can select the annotation before the
double-click path begins re-editing.

Implementation model:

- `AnnotationController` records the target id in `editing_annotation_id_`
- the draft is pre-populated from the committed annotation's origin, base style,
  and logical runs
- the same `TextEditController` used for new drafts is reused for re-edit

Current re-edit outcomes:

- commit updates the existing annotation through `UpdateAnnotationCommand`
- cancel leaves the original annotation unchanged
- committing an empty result leaves the original annotation unchanged and does
  not push a new undo entry
- undo after a committed re-edit restores the previous committed text

While re-editing:

- selected-annotation resize handles stay hidden
- the live draft bounds temporarily replace the stored committed bounds for the
  marquee, so the selection frame tracks the edited text correctly

## Data Model And Ownership

### Core data types

Committed text is represented by `TextAnnotation`, which stores:

- `origin`
- `base_style`
- `runs`
- `visual_bounds`
- committed premultiplied BGRA bitmap data

The live editor is built around:

- `TextDraftBuffer`
- `TextDraftSnapshot`
- `TextDraftView`
- `TextEditController`

`TextDraftBuffer` owns the transient editing state:

- logical runs
- typing style
- selection
- overwrite mode
- preferred x-position for vertical navigation

### Controller split

Current ownership is intentionally split:

- `AnnotationController`
  - owns the committed annotation document
  - owns persisted Text tool defaults such as size step and current font slot
  - owns the optional active `TextEditController`
  - owns the optional `editing_annotation_id_` used during re-edit
- `TextEditController`
  - owns one live draft buffer
  - owns private snapshot-based undo/redo history for that draft
  - collaborates with the text layout engine and spell-check service
- `OverlayController`
  - routes overlay pointer and cancel/commit behavior
  - decides whether a text commit is a new annotation or an edit of an existing
    one
- `OverlayWindow`
  - owns the Win32 keyboard routing, caret blink timer, and clipboard helpers

Draft-local undo history is intentionally separate from the overlay session undo
stack. Typing inside a draft does not create session-level undo entries.

## Layout, Rendering, And Output

### Layout engine boundary

Text layout stays behind `ITextLayoutEngine`.

The layout engine is responsible for:

- draft layout construction
- point hit testing
- selection rectangle generation
- caret geometry
- line ascent lookup
- final rasterization of committed text

The current Win32 implementation uses DirectWrite with `DWRITE_WORD_WRAPPING_NO_WRAP`.

### Draft rendering

The live draft is not treated as a committed annotation bitmap.

Current draft paint data includes:

- draft annotation runs and bounds
- selection rectangles
- insert or overwrite caret geometry
- spell-check squiggles

The draft is rendered in the live overlay layer and is excluded from saved or
copied image output until it is committed.

### Committed rendering

On commit:

1. core finalizes a `TextAnnotation` value from the live draft
2. the layout engine rasterizes it into a tight premultiplied BGRA bitmap
3. core records it as either:
   - a new annotation via `AddAnnotationCommand`
   - an edit of an existing annotation via `UpdateAnnotationCommand`

After commit, the same committed bitmap is reused for:

- live overlay paint
- topmost hit testing
- save output
- clipboard image output

This keeps committed text consistent with the repository-wide rule that paint,
selection, and exported output must agree on committed pixel coverage.

## Configuration

The Text tool currently depends on:

- `tools.text.size`
- `tools.text.current_font`
- `tools.text.spell_check_languages`
- `tools.font.sans`
- `tools.font.serif`
- `tools.font.mono`
- `tools.font.art`
- the shared annotation color selection in `tools.current_color`

Current normalization and behavior rules are implemented in `app_config.*` and
the Win32 font-resolution path:

- text size persists as a size step, not directly as a point size
- the active font setting is a four-slot token (`sans`, `serif`, `mono`, `art`)
- configured family names fill those four slots and fall back to built-in defaults
- unsupported spell-check language tags trigger a warning and are skipped

## Existing Coverage

Key automated coverage already exists in:

- `tests/text_draft_buffer_tests.cpp`
- `tests/text_annotation_redit_tests.cpp`
- `tests/text_html_tests.cpp`
- `tests/text_rtf_tests.cpp`
- `tests/spell_check_tests.cpp`
- `tests/overlay_controller_tests.cpp`
- `tests/app_config_tests.cpp`

Key manual coverage already exists in `docs/manual_test_plan.md`, especially:

- `GF-MAN-ANN-011` through `GF-MAN-ANN-015`
- `GF-MAN-ANN-014A`
- `GF-MAN-ANN-022`
- `GF-MAN-TXT-RTF-001` through `GF-MAN-TXT-RTF-006`
- `GF-MAN-TXT-HTML-001`

Future edits to the Text tool should update this document only when the shipped
behavior changes. Wheel-specific details should stay in
`text_style_wheel_redesign.md` instead of being duplicated here.
