# Undo/Redo Command System Design

## Overview

Greenflame's overlay allows users to capture and annotate screenshots. Every user action —
snap-region draw, resize, move, and future annotation operations (lines, arrows, shapes, text,
property changes) — must be undoable and redoable via **Ctrl+Z** / **Ctrl+Y**.

This document specifies the design of that system: the core abstractions, how concrete commands
are written, where state lives, how keyboard integration works, and how to test it all.

---

## Guiding Principles

| Principle | How it is upheld |
|-----------|-----------------|
| **Simple core, minimal boilerplate** | A single generic `ModificationCommand<T>` covers the majority of property-change commands. Named classes are only needed for structurally complex operations (add/remove annotation). |
| **Fully testable** | All abstractions (`ICommand`, `UndoStack`, `ModificationCommand<T>`) live in `greenflame_core` — the static library with no Win32 dependencies. Tests use Google Test. |
| **Easy to extend** | Adding a new command is one of two things: (a) construct a `ModificationCommand<T>` inline, or (b) write a small named class. No registration, no factory, no macros. |
| **Session-scoped** | The undo/redo history belongs to one overlay session. It is created when the overlay opens and is discarded when it closes. |

---

## Core Abstractions (in `greenflame_core`)

### `ICommand`

```cpp
// src/greenflame_core/command.h
namespace greenflame::core {

class ICommand {
  public:
    virtual ~ICommand() = default;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
    virtual std::string_view Description() const { return ""; }
};

} // namespace greenflame::core
```

Rules:
- A command is **created in its already-executed state**. The overlay already applied the change
  imperatively via normal mouse/keyboard handling; the command just records what happened so it
  can be reversed or replayed.
- `Undo()` reverts the state to what it was **before** the action.
- `Redo()` re-applies the state to what it was **after** the action.

---

### `UndoStack` (Qt-inspired, index-based)

The stack uses a single list of commands and an **index** (the next command to redo), rather than
two separate stacks. This matches Qt's `QUndoStack` model.

```
Commands:  [ C0 ][ C1 ][ C2 ]     index = 3 (all done, nothing to redo)
                Undo
Commands:  [ C0 ][ C1 ][ C2 ]     index = 2 (C2 undone)
                Undo
Commands:  [ C0 ][ C1 ][ C2 ]     index = 1 (C1, C2 undone)

Push (new command): commands above index are discarded, new cmd pushed, index = count
```

```cpp
// src/greenflame_core/undo_stack.h
namespace greenflame::core {

class UndoStack {
  public:
    void Push(std::unique_ptr<ICommand> cmd);

    bool CanUndo() const;
    bool CanRedo() const;

    void Undo();
    void Redo();

    void Clear();

    std::size_t Count() const;
    int Index() const;

    void SetUndoLimit(int limit);
    int UndoLimit() const;
};

} // namespace greenflame::core
```

- `Index()`: current position (next command to redo). `Index() == Count()` when nothing to redo.
- When a **new command is pushed** after a partial undo, commands above the index are discarded.
  This clears the redo branch and matches the standard behavior of every major editor.
- `SetUndoLimit(0)` = no limit (Qt default).

---

### `ModificationCommand<T>` — Generic Before/After Command

Covers the large class of operations that change a single value: move selection, resize
selection, change line width, change color, toggle a flag, etc.

```cpp
// src/greenflame_core/modification_command.h   (header-only template)
namespace greenflame::core {

template <typename T>
class ModificationCommand final : public ICommand {
  public:
    ModificationCommand(std::string_view description,
                        std::function<void(T const&)> apply,
                        T before,
                        T after);

    void Undo() override;
    void Redo() override;
    std::string_view Description() const override;

  private:
    std::string description_;
    std::function<void(T const&)> apply_;
    T before_;
    T after_;
};

} // namespace greenflame::core
```

The **apply lambda** is where Win32-specific side effects (like `InvalidateRect`) belong. The
`ModificationCommand` itself remains pure and testable.

---

## Named Command Classes

For operations that are not a simple property swap — principally add/remove annotation — write
a dedicated class. This keeps intent explicit and enables type-specific behaviour.

### Example: `AddAnnotationCommand`

```cpp
// (future, when annotation data structures exist)
class AddAnnotationCommand final : public greenflame::core::ICommand {
  public:
    AddAnnotationCommand(AnnotationList* list, Annotation annotation)
        : list_(list), annotation_(std::move(annotation)) {}

    void Undo() override { list_->pop_back(); }
    void Redo() override { list_->push_back(annotation_); }
    std::string_view Description() const override { return "Add annotation"; }

  private:
    AnnotationList* list_;
    Annotation annotation_;
};
```

This works correctly because undo is always applied in strict reverse order — the command
added at `push_back` is always the last element when `Undo()` runs.

---

## State Changes to `OverlayState`

`OverlayState` is a private struct defined in `overlay_window.cpp`. Three additions:

### 1. `UndoStack`

```cpp
greenflame::core::UndoStack undo_stack;
```

Cleared automatically when the overlay is destroyed or reset.

### 2. `final_selection` becomes `std::optional<greenflame::core::RectPx>`

```cpp
// Before:
greenflame::core::RectPx final_selection;

// After:
std::optional<greenflame::core::RectPx> final_selection;
```

`std::nullopt` means **no selection has been drawn yet**. This allows the undo of an initial
selection draw to cleanly return to the "no selection" state, without relying on a sentinel
zero-area rect.

All existing uses of `final_selection` in `overlay_window.cpp` are guarded with
`.has_value()` / `.value()` / `.value_or()` as appropriate.

### 3. `pre_drag_selection` — Snapshot for Command Creation

```cpp
std::optional<greenflame::core::RectPx> pre_drag_selection;
```

Captured at `WM_LBUTTONDOWN` whenever a resize or move drag begins. Used as the `before`
value when the command is pushed at `WM_LBUTTONUP`.

---

## Command Creation Pattern (in `overlay_window.cpp`)

### Resize / Move

```cpp
// --- On_l_button_down() ---
// When a handle or move drag starts, snapshot the current selection:
state_->pre_drag_selection = state_->final_selection;

// --- On_l_button_up() ---
// After snapping and committing final_selection:
if (state_->pre_drag_selection != state_->final_selection) {
    auto before = state_->pre_drag_selection;
    auto after  = state_->final_selection;
    state_->undo_stack.Push(
        std::make_unique<greenflame::core::ModificationCommand<std::optional<greenflame::core::RectPx>>>(
            "Resize selection",        // or "Move selection"
            [this](std::optional<greenflame::core::RectPx> const& r) {
                state_->final_selection = r;
                InvalidateRect(hwnd_, nullptr, TRUE);
            },
            before, after));
}
```

### Initial Selection Draw

Same pattern — `before` is `std::nullopt` (no selection), `after` is the drawn rect.

```cpp
// On_l_button_up() after the initial drag:
state_->undo_stack.Push(
    std::make_unique<greenflame::core::ModificationCommand<std::optional<greenflame::core::RectPx>>>(
        "Draw selection",
        [this](std::optional<greenflame::core::RectPx> const& r) {
            state_->final_selection = r;
            InvalidateRect(hwnd_, nullptr, TRUE);
        },
        std::nullopt,              // before: no selection
        state_->final_selection)); // after: the new rect
```

---

## Keyboard Integration

In `OverlayWindow::On_key_down()`, add two cases alongside existing Ctrl+S, Ctrl+C:

```cpp
case 'Z':
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        state_->undo_stack.Undo();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    break;

case 'Y':
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        state_->undo_stack.Redo();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    break;
```

`InvalidateRect` is called unconditionally — `Undo()`/`Redo()` are no-ops when the stack is
empty, and a spurious repaint is harmless.

---

## File Structure

```
src/greenflame_core/
    command.h                   ICommand interface
    undo_stack.h                UndoStack declaration
    undo_stack.cpp              UndoStack implementation
    modification_command.h      ModificationCommand<T> template (header-only)

src/greenflame/win/
    overlay_window.cpp          MOD  OverlayState changes; command push; Ctrl+Z/Y (future)

CMakeLists.txt                  MOD  Add undo_stack.h/.cpp to greenflame_core sources

tests/
    undo_stack_tests.cpp        UndoStack + ModificationCommand tests
    CMakeLists.txt              MOD  Add undo_stack_tests.cpp

docs/
    undo_command_system.md      THIS FILE
```

Note: `overlay_window.h` does **not** change — `UndoStack` is hidden inside the private
`OverlayState` struct defined in the `.cpp` file, so no new public include is needed.

---

## Testing

All tests use Google Test.

### `undo_stack_tests.cpp` — `UndoStack` behaviour

- Empty stack: `CanUndo`/`CanRedo` false, `Count` 0
- Push one: `CanUndo` true, `CanRedo` false
- Undo: calls setter with `before`, moves to redo branch
- Redo: calls setter with `after`, moves back
- Push after undo: clears redo branch
- Multiple push/undo/redo: correct state transitions
- Clear: empties stack
- Undo/Redo on empty: no-op
- `ModificationCommand`: Undo/Redo apply correct values
- `UndoLimit`: old commands dropped from bottom when limit exceeded

### Manual / Integration Verification (when overlay integration is done)

1. Open overlay → draw selection → resize → move.
2. Ctrl+Z three times: move reverts → resize reverts → selection disappears.
3. Ctrl+Y three times: all actions replay in order.
4. Draw new selection after partial undo: redo branch is cleared, Ctrl+Y does nothing.

---

## Future Commands Reference

This table shows how new operations map to command types once annotation tools are built:

| User action                  | Command class                               | T / data                         |
|------------------------------|---------------------------------------------|----------------------------------|
| Draw initial selection       | `ModificationCommand<optional<RectPx>>`     | `nullopt` → rect                 |
| Resize selection             | `ModificationCommand<optional<RectPx>>`     | old rect → new rect              |
| Move selection               | `ModificationCommand<optional<RectPx>>`     | old rect → new rect              |
| Change line width            | `ModificationCommand<int>`                  | old width → new width            |
| Change tool color            | `ModificationCommand<COLORREF>`             | old color → new color            |
| Add freehand stroke          | `AddAnnotationCommand`                      | `AnnotationList*` + `Annotation` |
| Add line / arrow / shape     | `AddAnnotationCommand`                      | same                             |
| Add text annotation          | `AddAnnotationCommand`                      | same                             |
| Delete annotation            | `RemoveAnnotationCommand`                   | index + `Annotation`             |
| Edit annotation property     | `ModificationCommand<T>` on annotation field | old/new property value           |

The pattern is intentionally uniform. New annotation types inherit the same undo behaviour
for free as long as they are represented as entries in an `AnnotationList`.
