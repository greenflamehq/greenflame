---
title: Undo/Redo Command System
summary: Current reference for the overlay undo/redo architecture in Greenflame.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - undo
  - redo
  - annotations
---

# Undo/Redo Command System

This document describes the current undo/redo architecture used by the interactive
overlay.

For user-visible behavior, [docs/annotation_tools.md](../annotation_tools.md) is the
authoritative reference. This document focuses on the command model, ownership,
and integration points in the implementation.

## Current Status

Undo/redo is implemented and active for both region operations and committed
annotation operations.

- One chronological session-scoped stack is used for both region and annotation changes.
- The stack lives in `greenflame_core`, not in Win32-only code.
- Region operations restore a full `OverlaySelectionState`, not just a `RectPx`.
- Annotation operations use dedicated command classes in `annotation_commands.*`.
- Tool switching is not part of undo history.
- While a text draft is active, `Ctrl+Z` and `Ctrl+Shift+Z` operate on the draft
  editor first. Once the draft is committed, undo/redo returns to the session stack.

## Keyboard Behavior

The overlay uses:

- `Ctrl+Z` for undo
- `Ctrl+Shift+Z` for redo

There is no `Ctrl+Y` overlay shortcut in the current implementation.

## Core Types

### `ICommand`

`src/greenflame_core/command.h` defines the common interface:

- `Undo()`
- `Redo()`
- `Description()`

Every command type used by the overlay implements this interface.

### `ModificationCommand<T>`

`src/greenflame_core/modification_command.h` provides a small generic before/after
command:

- it stores `before` and `after` values of type `T`
- it stores an `apply` callback
- `Undo()` applies `before`
- `Redo()` applies `after`

This is used for region selection history, where restoring the previous state is
more important than modeling each low-level gesture as a separate command class.

### `UndoStack`

`src/greenflame_core/undo_stack.*` implements a single-list, index-based undo stack.

Key properties:

- `Push()` discards any redo branch above the current index
- `Push()` immediately executes `Redo()` on the pushed command
- `Undo()` moves the index backward, then calls `Undo()`
- `Redo()` calls `Redo()`, then moves the index forward
- `Set_undo_limit(0)` means no limit

This means callers build a command that can move between the `before` and `after`
states, then push it once when the interaction is committed.

### `OverlaySelectionState`

Region undo does not restore only a rectangle. It restores the full
`OverlaySelectionState` value from `overlay_controller.h`, which includes:

- `final_selection`
- `selection_source`
- `selection_window`
- `selection_monitor_index`
- `selection_capture_rect_screen`
- `selection_capture_offset_px`
- `selection_uses_full_window_capture`
- `selection_has_offscreen_capture`

That broader state is necessary because selection history must preserve more than
geometry. Window-based and monitor-based selections carry capture metadata that
must survive undo and redo intact.

## Command Types In Use

### Region commands

Region history is recorded with `ModificationCommand<OverlaySelectionState>`.

Current region command descriptions include:

- `Create selection`
- `Draw selection`
- `Move selection`
- `Resize selection`

These commands are created in `src/greenflame/win/overlay_window.cpp` and restore
state through `OverlayWindow::Restore_selection_state(...)`.

### Annotation commands

Committed annotation history uses named command classes from
`src/greenflame_core/annotation_commands.*`:

- `AddAnnotationCommand`
- `DeleteAnnotationCommand`
- `UpdateAnnotationCommand`
- `AddBubbleAnnotationCommand`
- `CompoundCommand`

These commands are created by `AnnotationController`.

Notable current behavior:

- annotation add, delete, move, resize, and edit operations are undoable
- bubble annotations use a dedicated add command so undo/redo also keeps the bubble
  counter in sync
- grouped edits use `CompoundCommand`
- reactive obfuscate recomputes are appended to the same undo record as the user
  action that triggered them

## Ownership And Flow

### `OverlayController`

`OverlayController` owns the session undo stack:

```cpp
UndoStack undo_stack_;
```

It exposes:

- `Push_command(...)`
- `Undo()`
- `Redo()`
- `Selection_state()`
- `Restore_selection_state(...)`

This keeps undoable policy in core while leaving Win32 message routing in the
overlay window.

### `OverlayWindow`

`src/greenflame/win/overlay_window.cpp` is responsible for:

- detecting committed region interactions
- capturing `before` and `after` `OverlaySelectionState` snapshots
- pushing `ModificationCommand<OverlaySelectionState>` entries
- routing `Ctrl+Z` / `Ctrl+Shift+Z`
- refreshing toolbar state, cursor state, and annotation caches after undo/redo

### `AnnotationController`

`src/greenflame_core/annotation_controller.cpp` is responsible for:

- building annotation commands
- grouping primary and reactive commands
- pushing single commands directly when no grouping is required
- maintaining selected annotation ids across undo and redo

## Text Draft Interaction

Text editing has two layers of undo:

- while a text draft is active, undo/redo applies to the draft editor itself
- after the draft is committed, the committed annotation change is recorded in the
  session undo stack

This is why `OverlayWindow::On_key_down(...)` routes `Ctrl+Z` and
`Ctrl+Shift+Z` to the active text editor before it falls back to
`OverlayController::Undo()` or `Redo()`.

## Testing

Current automated coverage is split across several levels:

- `tests/undo_stack_tests.cpp`
  - `UndoStack` semantics
  - redo-branch clearing
  - undo-limit behavior
  - `ModificationCommand<T>` behavior
- `tests/annotation_controller_tests.cpp`
  - committed annotation add/delete/edit flows
  - grouped annotation history
  - committed text annotation history
  - bubble-specific counter behavior through undo/redo
- `tests/overlay_controller_tests.cpp`
  - overlay-level selection and command integration points

Manual verification for user-visible overlay behavior belongs in
[docs/manual_test_plan.md](../manual_test_plan.md) when automation is not practical.

## Related Documents

- [docs/annotation_tools.md](../annotation_tools.md): authoritative behavior and current
  architecture overview for annotation features
- [README.md](../../README.md): user-visible shortcuts and behavior summary
- [docs/manual_test_plan.md](../manual_test_plan.md): manual coverage for Win32-only flows
