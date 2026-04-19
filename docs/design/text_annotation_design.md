---
title: Text Annotation Design
summary: Proposed UX and architecture for a new text annotation tool in the interactive overlay.
audience: contributors
status: proposed
owners:
  - core-team
last_updated: 2026-03-13
tags:
  - overlay
  - annotations
  - text
  - proposal
---

# Text Annotation Design

This document defines the proposed behavior and implementation shape for a new
interactive **Text annotation** tool.

It is intentionally written as an implementation handoff for another agent or
contributor. It assumes the current annotation architecture described in
[docs/annotation_tools.md](annotation_tools.md).

## Goals

- Add a new `Text tool` to the existing annotation toolbar and hotkey system.
- Keep the existing ownership model:
  - Win32/D2D/DWrite remain in `src/greenflame/win/`
  - behavior and edit policy remain in `greenflame_core`
- Preserve the current correctness rules:
  - overlay-space storage stays in **physical pixels**
  - save/copy output uses the same committed representation as selection/hit-test
- Keep the tool usable enough for day-one screenshot annotation:
  - multi-line text
  - caret movement and selection
  - style-flag formatting during draft editing
  - move/delete after commit

## Non-goals For V1

- Re-editing a committed text annotation after pressing `Enter`
- IME-specific composition UI beyond normal `WM_CHAR` text input
- Arbitrary text box resize or automatic word-wrap
- Rich-text clipboard interop beyond plain `CF_UNICODETEXT`
- Per-range font, color, or point-size changes inside a live draft

Those can be added later, but they materially increase scope and do not appear in
the current requirement set.

## UX Specification

### Tool activation

- New toolbar button: `Text tool`
- New hotkey: `T`
- Registry order:
  - Brush
  - Highlighter
  - Line
  - Arrow
  - Rectangle
  - Filled Rectangle
  - Text
  - Help
- When the active tool is `Text`, the overlay cursor is `IDC_IBEAM`.
- Border resize handles still take priority over the text tool exactly as they do
  for the other tools.

### Text tool states

The tool has two live interaction states:

1. `Armed`
   - `Text` is the active tool, but no draft annotation exists yet.
   - Cursor is I-beam.
   - Left-click starts a new draft edit session at the clicked point.
   - Right-click opens the text style wheel.
   - Mouse-wheel and `Ctrl+=` / `Ctrl+-` step the persisted text size.

2. `Editing`
   - A draft text annotation exists.
   - Keyboard input edits the draft.
   - Mouse input inside the draft moves the caret or creates/extends a selection.
   - Left-click in the canvas outside the current draft finalizes or discards the
     current draft and immediately starts a new draft at the clicked point.
   - Right-click is ignored.
   - Mouse-wheel and `Ctrl+=` / `Ctrl+-` are ignored.
   - Pressing `Enter` commits the draft as a normal annotation and returns to the
     `Armed` state with the `Text` tool still active.
   - Pressing `Esc` cancels the draft and returns to the `Armed` state.

### New draft defaults

Every new draft starts with:

- empty text
- plain style flags: bold off, italic off, underline off, strikethrough off
- current annotation color slot from the shared 8-color palette
- current text font choice from config
- current text size from config, default `12 pt`
- insert mode enabled
- a fresh draft-local undo/redo history seeded with the empty snapshot

`Edition always start plain` applies only to the four style flags. Color, font,
and size still come from the current persisted tool defaults.

### Text layout model

- A draft starts at the clicked point and is **top-left anchored** at that point.
- Text is **left-aligned**.
- There is **no automatic wrapping** in V1.
- Multi-line text is created only by `Ctrl+Enter`.
- Color, font choice, and point size are captured at draft creation and are uniform
  for the whole draft and committed annotation in V1.
- Only the four style flags (`Bold`, `Italic`, `Underline`, `Strikethrough`) may
  vary by text range within a draft.
- The visual bounds grow as text is inserted.
- As with existing annotations, text is stored in overlay coordinates and may
  extend outside the capture selection; save/copy include only the intersecting
  pixels.

### Keyboard behavior while editing

While a text draft is active, the text editor owns these inputs:

- text insertion via `WM_CHAR`
- caret motion:
  - `Left`, `Right`, `Up`, `Down`
  - `Home`, `End`
  - `PgUp`, `PgDn`
  - `Ctrl+Left`, `Ctrl+Right`
  - `Ctrl+Home`, `Ctrl+End`
  - all shift-extended variants for selection
- deletion:
  - `Backspace`
  - `Delete`
  - `Ctrl+Backspace`
  - `Ctrl+Delete`
  - selected-range delete via either key
- mode toggle:
  - `Insert` toggles insert vs overwrite
- formatting toggles:
  - `Ctrl+B`
  - `Ctrl+I`
  - `Ctrl+U`
  - `Alt+Shift+5`
- clipboard editing:
  - `Ctrl+A` selects the whole draft
  - `Ctrl+C` copies the selected text to the Windows clipboard as plain
    `CF_UNICODETEXT`
  - `Ctrl+X` cuts the selected text to the clipboard
  - `Ctrl+V` pastes plain Unicode text, replacing the selection if present
- draft-local undo/redo:
  - `Ctrl+Z` undoes within the current draft only
  - `Ctrl+Shift+Z` redoes within the current draft only, matching the current
    overlay redo binding
- finalize/cancel:
  - `Enter` commits
  - `Ctrl+Enter` inserts newline
  - `Esc` cancels the draft

Additional V1 rules:

- `Ctrl+S` does **not** save the screenshot while editing; global save shortcuts
  remain suppressed while a draft is active.
- `Alt+Shift+5` is the strikethrough shortcut for text editing in V1.
- `Ctrl+=`, `Ctrl+-`, and the mouse wheel do **not** change text size while a
  draft is live.
- `PgUp` and `PgDn` move to document start/end in V1 because the editor has no
  scroll viewport.
- Clipboard paste accepts plain Unicode text only; incoming `CRLF` or `CR`
  line endings are normalized to internal `LF`.
- Save/copy screenshot shortcuts and overlay-level annotation undo/redo are
  suppressed while editing and resume after the draft is committed or canceled.

### Formatting semantics

Only the four style flags are editable during a live draft.

- If the selection is empty:
  - style toggles update the current typing style
- If the selection is non-empty:
  - style toggles apply to the selected range

Recommended toggle rule for selected text:

- if the entire selection already has the flag, toggle it off for the whole range
- otherwise toggle it on for the whole range

Color, font, and point size do **not** change during editing:

- the text style wheel is unavailable while a draft exists
- size stepping is unavailable while a draft exists
- those values are selected before the draft starts and apply to the whole
  annotation

### Mouse behavior while editing

- Left-click inside the draft:
  - place caret if no drag follows
  - start mouse selection if drag follows
- Left-click and drag:
  - extend selection to the hit-tested text position
- Left-click in the canvas outside the draft:
  - if the current draft is empty, discard it
  - if the current draft is non-empty, commit it as one annotation
  - after either case, start a new draft at the clicked point because the tool
    remains armed
- Right-click:
  - ignored while editing
- Mouse-wheel:
  - ignored while editing

### Insert vs overwrite mode

- Default is insert mode.
- `Insert` toggles overwrite mode.
- Overwrite mode replaces the next character on the current line when possible.
- If the caret is at line end or immediately before a newline, insertion behaves
  like normal insert mode.
- Visual cue:
  - insert mode: thin caret
  - overwrite mode: block caret spanning the next glyph advance when available

### Mouse-wheel size stepping

- Text size is independent from `brush_width`.
- Config default: `12 pt`
- Allowed sizes:
  - `5, 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72, 84, 96, 108, 144, 192, 216, 288`
- While the `Text` tool is armed and no draft is active:
  - mouse-wheel up/down moves to the previous/next entry in that list
  - `Ctrl+=` / `Ctrl+-` do the same
- While editing, those inputs do nothing.
- Size persists to config immediately when changed.
- Reuse the existing centered transient overlay, but show values as `12 pt`
  instead of `12 px`.

### Right-click wheel

The existing selection wheel becomes a text-specific **style wheel** while the active
tool is `Text` and no draft is active.

- Left half:
  - the existing 8 annotation colors
- Right half:
  - 4 font choices:
    - Sans
    - Serif
    - Mono
    - Art

Recommended presentation:

- left-half color wedges remain solid-color wedges
- right-half wedges use a neutral fill and display a large lowercase `m`
  rendered in the target font
- `m` is preferred here because it makes `Sans`, `Serif`, `Mono`, and `Art`
  visually distinct with the default families while remaining readable at wheel
  size

Wheel behavior:

- hover highlights the hovered segment
- left-click applies the hovered color or font choice for the **next** draft and
  closes the wheel
- `Esc` closes the wheel without changes
- starting a draft closes the wheel if it is visible
- while editing, right-click does not open the wheel

## Config

Add the following `[tools]` keys:

- `text_size_points=12`
- `text_current_font=sans`
- `text_font_sans=Arial`
- `text_font_serif=Times New Roman`
- `text_font_mono=Courier New`
- `text_font_art=Comic Sans MS`

Normalization rules:

- `text_size_points`
  - if not in the allowed list, clamp to the nearest allowed value
- `text_current_font`
  - valid values: `sans`, `serif`, `mono`, `art`
  - if invalid or empty, restore `sans`
- font family strings
  - trim surrounding whitespace
  - if empty after trimming, restore the choice default
  - cap length to `128` UTF-16 code units

The `Text` tool uses the existing shared annotation palette keys:

- `current_color`
- `color_0` through `color_7`

## Core Model

### New enums and structs

Add:

- `AnnotationToolId::Text`
- `AnnotationToolbarGlyph::Text`
- `AnnotationKind::Text`
- `TextAnnotationBaseStyle`
  - `COLORREF color`
  - `TextFontChoice font_choice`
  - `int32_t point_size`
- `TextFontChoice`
  - `Sans`
  - `Serif`
  - `Mono`
  - `Art`
- `TextStyleFlags`
  - `bold`
  - `italic`
  - `underline`
  - `strikethrough`

New draft-only core types:

- `TextTypingStyle`
  - `TextStyleFlags flags`
- `TextRun`
  - `std::wstring text`
  - `TextStyleFlags flags`
- `TextSelection`
  - `int32_t anchor_utf16`
  - `int32_t active_utf16`
- `TextDraftBuffer`
  - `TextAnnotationBaseStyle base_style`
  - ordered runs
  - current typing style
  - current selection
  - overwrite mode
  - preferred x-position for vertical movement
- `TextDraftSnapshot`
  - `TextDraftBuffer buffer`
- `TextDraftHistory`
  - private undo stack
  - private redo stack
  - lifetime limited to one live draft

New committed annotation payload:

- `TextAnnotation`
  - `PointPx origin`
  - `TextAnnotationBaseStyle base_style`
  - `std::vector<TextRun> runs`
  - `RectPx visual_bounds`
  - `int32_t bitmap_width_px`
  - `int32_t bitmap_height_px`
  - `int32_t bitmap_row_bytes`
  - `std::vector<uint8_t> premultiplied_bgra`

All text positions in the draft model are insertion offsets in the flattened
`std::wstring`, measured in UTF-16 code units.

Keeping the logical runs in the committed payload is intentional even though V1
does not re-edit committed text:

- it keeps undo values self-describing
- it leaves a clean path for future re-edit support
- hit-test and output still use the cached bitmap so committed behavior stays
  deterministic and shared

### Variant updates

Extend `AnnotationData` in `annotation_types.h` to include `TextAnnotation`.

The normal compile-time enforcement via `std::visit` remains useful and should
catch every required call site update:

- `Annotation::Kind()`
- `Annotation_bounds(...)`
- `Annotation_visual_bounds(...)`
- `Annotation_hits_point(...)`
- `Translate_annotation(...)`
- `Blend_annotations_onto_pixels(...)`
- live D2D paint dispatch

## Layout And Rasterization Service

Text layout should not be hand-coded in core.

Add a core interface, owned by the controller and implemented in `win/`, for all
text measurement, hit-test, caret, selection-rect, and rasterization work.

Suggested name:

- `ITextLayoutEngine`

Suggested responsibilities:

- build a `DraftTextLayoutResult` from a `TextDraftBuffer`
- hit-test a point to a text insertion position
- move vertically while preserving preferred x-position
- produce selection highlight rectangles
- produce caret rectangle geometry
- report the overwrite caret advance width at the current insertion position
- rasterize a finalized `TextAnnotation` bitmap

This keeps:

- edit policy in core
- DWrite-specific shaping and rasterization in the Win32 layer
- unit tests independent from DWrite by using a fake engine

Word-boundary navigation and deletion should stay in core on the flattened text
buffer so behavior is testable without DWrite.

## Draft-local History And Clipboard

Each live draft owns a private, temporary history stack.

- The stack is created when the draft starts.
- The stack is destroyed on commit or cancel.
- The overlay session `UndoStack` must not observe any draft-local edits.
- Committing a draft pushes exactly one `AddAnnotationCommand` onto the overlay
  undo stack.
- Undoing annotations after commit removes the whole committed text annotation in
  one step, regardless of any draft-local undo/redo that happened earlier.

Recommended V1 implementation:

- store full `TextDraftSnapshot` values rather than edit deltas
- create history entries only for mutating operations:
  - insert
  - paste
  - cut
  - backspace/delete
  - style toggle
- do not create history entries for pure caret motion or pure selection changes

Clipboard behavior should be plain-text only:

- copy and cut export the selected text as `CF_UNICODETEXT`
- outbound newlines use Windows `CRLF`
- paste prefers `CF_UNICODETEXT`
- incoming `CRLF` and bare `CR` normalize to internal `LF`
- if the clipboard does not contain plain Unicode text, paste is a no-op

## Controller Integration

### Recommended split

Do not add a wide no-op text API to `OverlayController` or `AnnotationController`.

Recommended design:

- keep `TextAnnotationTool` as the registered tool for armed-state pointer entry
- add a dedicated `TextEditController` in `greenflame_core` that exists only
  while a live text draft exists
- `AnnotationController` keeps ownership of:
  - the committed annotation document
  - tool registry and selection/edit interactions
  - persisted text defaults: shared annotation color, current font choice, point
    size
- `TextEditController` owns:
  - the live `TextDraftBuffer`
  - the private `TextDraftHistory`
  - collaboration with `ITextLayoutEngine`
  - all draft-only keyboard and pointer edit behavior
- `OverlayController` routes events to the active subcontroller and should expose
  only a small bridge to the active `TextEditController`

Suggested `TextEditController` surface:

- `Build_view() const -> TextDraftView`
- `On_text_input(std::wstring_view text)`
- `On_select_all()`
- `On_navigation(TextNavigationAction action, bool extend_selection)`
- `On_backspace(bool by_word)`
- `On_delete(bool by_word)`
- `Toggle_style(TextStyleToggle which)`
- `Toggle_insert_mode()`
- `Copy_selected_text()`
- `Cut_selected_text()`
- `Paste_text(std::wstring_view plain_text)`
- `Undo()`
- `Redo()`
- `On_pointer_press(PointPx cursor)`
- `On_pointer_move(PointPx cursor, bool primary_down)`
- `On_pointer_release(PointPx cursor)`
- `Commit() -> TextAnnotation`
- `Cancel()`

Suggested minimal bridge outside the text editor:

- `Has_active_text_edit() const`
- `Active_text_edit()`
- `Begin_text_draft(PointPx origin)`
- `Commit_text_annotation(TextAnnotation annotation, UndoStack &undo_stack)`
- `Text_point_size() / Step_text_size(int delta_steps)`
- `Text_current_font() / Set_text_current_font(TextFontChoice choice)`

Controller ownership recommendation:

- `AnnotationController` owns:
  - the committed annotation document
  - the pending text defaults: color, font choice, point size
  - the optional active `TextEditController`
- `OverlayController` continues to arbitrate between region behavior, annotation
  behavior, and text-draft precedence

### Cancel priority

`OverlayController::On_cancel()` should change for text only as follows:

1. active selection move
2. active selection resize
3. active selection drag
4. active text draft: cancel draft, keep text tool armed
5. active non-text tool gesture: cancel gesture and deselect tool
6. active tool without gesture: deselect tool
7. selected committed annotation: deselect annotation
8. clear final selection
9. close overlay

This preserves the current mental model while making `Esc` match the text
requirement.

## Overlay Window Integration

### New input handling

`OverlayWindow` must start handling:

- `WM_CHAR`
- optionally `WM_UNICHAR` as a fallback convenience path

`WM_KEYDOWN` should gain a text-edit precedence block before save/copy/undo/redo.

When `Active_text_edit()` is non-null, `OverlayWindow` should route text-edit
keyboard and pointer events to that controller and bypass unrelated overlay
shortcuts.

`OverlayWindow` should also gain small plain-text clipboard helpers for
`CF_UNICODETEXT` read/write. Keep the Win32 clipboard API in the window layer and
pass plain strings into core controller methods.

### Cursor rules

- text tool armed or editing: `IDC_IBEAM`
- active text mouse-selection drag: still `IDC_IBEAM`
- committed text selected in default mode: normal arrow, or move cursor during drag
- existing handle, toolbar, help, and wheel rules stay unchanged

While editing, overlay chrome remains visible but non-interactive. Draft text owns
the pointer on the canvas, and outside-canvas chrome clicks should not steal focus
or trigger unrelated overlay actions.

### Paint input

Extend `D2DPaintInput` with text-draft fields:

- `draft_text_annotation`
- `draft_text_selection_rects`
- `draft_text_caret_rect`
- `draft_text_insert_mode`
- `draft_text_blink_visible`

Draft text should be rendered in the live layer, after committed annotations and
before toolbar/color-wheel chrome.

### Timers

Reuse the existing overlay timer infrastructure for caret blink:

- one new timer id for caret blink
- blink rate from `GetCaretBlinkTime()`
- reset blink visibility on any edit, caret move, or mouse selection change

## Rendering And Output

### Draft rendering

Draft text is not part of the committed annotation cache.

It should be rendered like the current freehand draft path:

- draw current draft runs with DWrite
- draw translucent selection highlights behind glyphs
- draw the caret on top
- do not draw the wheel or size overlay while a draft is active

### Committed rendering

When the draft is committed:

1. core finalizes the logical `TextAnnotation`
2. the layout engine rasterizes it into a tight premultiplied BGRA bitmap
3. the annotation is pushed through `AddAnnotationCommand`

After commit:

- live overlay paint uses the cached committed bitmap
- hit-test uses the cached alpha coverage
- save/copy blits the same cached bitmap into the cropped output buffer

This keeps committed text aligned with the existing non-negotiable rule: paint,
selection, and output all use the same committed coverage.

### Selection affordance for committed text

Committed text should behave like a freehand annotation in default mode:

- topmost covered pixel wins selection
- selected text shows the shared clockwise marquee around `visual_bounds`
- no resize handles in V1
- body drag moves the entire annotation

## Resources

- Add `Text` glyph ids beside the existing toolbar glyph ids.
- There is already a `resources/text.png` source asset in the repo; derive a
  matching alpha-mask asset and wire it through the existing resource pipeline.

## Testing Plan

### New automated unit tests

- `tests/text_draft_buffer_tests.cpp`
  - insert text
  - replace selected text
  - backspace/delete
  - `Ctrl+Backspace` and `Ctrl+Delete`
  - caret move by char, word, line, doc
  - `PgUp` / `PgDn` map to doc start/end
  - shift-extended selection
  - select all
  - copy/cut/paste plain text normalization
  - style toggle with empty selection
  - style toggle over a non-empty selection
  - local undo/redo
  - insert/overwrite mode
- `tests/annotation_controller_tests.cpp`
  - `T` toggles the tool
  - new draft starts plain and captures current color/font/size
  - `Enter` commits
  - `Ctrl+Enter` inserts newline
  - `Esc` cancels
  - click outside empty draft discards and starts a new draft
  - click outside non-empty draft commits and starts a new draft
  - committed text stays movable/deletable
- `tests/overlay_controller_tests.cpp`
  - cancel priority with active text draft
  - toolbar visibility and non-interactivity while editing
  - text size stepping uses the discrete list only while armed
  - draft-local undo/redo does not touch the overlay undo stack
- `tests/app_config_tests.cpp`
  - text size normalization
  - `text_current_font` token normalization
  - empty-font fallback
- `tests/annotation_hit_test_tests.cpp`
  - text hit-test against cached bitmap alpha
  - text translation updates bounds only
- `tests/color_wheel_tests.cpp`
  - composite text style wheel segment geometry and hit-test

### Manual coverage additions

Add manual cases for:

- tool toggle and I-beam cursor
- right-click style wheel while armed
- no style wheel while editing
- starting a draft by click
- caret blink and overwrite caret
- mouse selection
- movement keys including `Ctrl+Arrow`, `Home`, `End`, `PgUp`, `PgDn`
- `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V`
- draft-local `Ctrl+Z` / `Ctrl+Shift+Z`
- style toggles
- font wheel half and selection wheel half before drafting
- size wheel stepping and persistence before drafting
- no size changes while editing from mouse wheel or `Ctrl+=` / `Ctrl+-`
- multi-line via `Ctrl+Enter`
- commit via `Enter`
- click outside current draft creating the next draft
- cancel via `Esc`
- move/delete committed text
- verify committed text is not re-editable
- save/copy output containing committed text but not draft chrome
