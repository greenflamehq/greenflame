# Wheel Keyboard Navigation

## Overview

The selection wheel currently only responds to mouse hover and click. This document specifies
keyboard and scroll-wheel navigation: moving the hover with the mouse wheel or the Up/Down
arrow keys, confirming with `Enter`, and showing the wheel with `Tab`.

---

## New Inputs

| Input | Action |
|---|---|
| Scroll wheel up | Navigate hover counter-clockwise |
| Scroll wheel down | Navigate hover clockwise |
| `↑` (Up arrow) | Navigate hover counter-clockwise |
| `↓` (Down arrow) | Navigate hover clockwise |
| `Enter` | Select the currently hovered segment |
| `Tab` | Show wheel at cursor (first press); cycle views on subsequent presses |

All of the above only apply while the wheel is visible, except `Tab` (which can show it).

---

## Hover State Model

Two hover sources exist simultaneously:

- **Mouse hover** — whichever segment the cursor is physically over (or none).
- **Nav hover** — the segment most recently set by keyboard/scroll-wheel navigation.

### Effective hover

```
effective_hover =
    nav_hover            if nav_hover is set
    mouse_hover          if mouse is over a segment
    currently_selected   otherwise
```

The effective hover is what is displayed and what `Enter` acts on.

### Nav hover lifetime

- **Set** by any keyboard or scroll-wheel navigation event.
- **Cleared** when the mouse cursor enters a segment.
  After clearing, the effective hover reverts to the mouse-over segment.
- Mouse movement that does not cross into any segment leaves nav hover unaffected.

Nav hover is also cleared when the wheel is dismissed.

---

## Navigation Baseline

When a navigation event fires, the starting point for the step is:

1. `nav_hover` — if already set, continue from there.
2. Mouse-over segment — if the cursor is currently on a segment.
3. Currently selected segment — if the tool has a selection.
4. *(No selection)* — counter-clockwise lands on the last segment; clockwise on the first.

Navigation wraps around (segment `0` ↔ segment `N−1`).

---

## Tab Behaviour

- **First `Tab`** while wheel is hidden: show the wheel at the current cursor position
  (same as right-click). No segment is pre-hovered by keyboard; effective hover is the
  currently selected segment (or none if the cursor is over a segment).
- **Subsequent `Tab`** presses while wheel is visible:
  - *Multi-view wheels* — cycle to the next view.
    Nav hover is cleared on view switch (segment indices change meaning between views).
  - *Single-view wheels* — ignored.

---

## Enter Behaviour

`Enter` selects the effective hover segment (same effect as clicking that segment) and
dismisses the wheel. If there is no effective hover (wheel shown but no selection and cursor
not over a segment), `Enter` is ignored.

---

## Interaction with Mouse

Mouse hover never blocks keyboard navigation. Keyboard navigation immediately overrides the
displayed hover. The moment the mouse moves, `nav_hover` is cleared and the mouse position
takes over. This means:

- Mouse over segment 1 → hover displays segment 1.
- `↓` twice → nav hover set to segment 3; display shows segment 3 (mouse still over 1).
- `Enter` → segment 3 selected.
- Mouse moves one pixel (cursor stays over segment 1) → nav hover cleared; display reverts to
  segment 1 (where the mouse is).
- Alternatively: mouse moves into empty space (no segment) → nav hover is **not** cleared;
  display remains on segment 3.

---

## Scope

- Navigation applies to both ring segments and font-mode segments.
- Hub buttons (Color / Font toggle in the text wheel center) are not keyboard-navigable;
  they remain pointer-only.
- Scroll-wheel input is consumed by the wheel only while the wheel is visible; default
  window scroll behaviour is unaffected when the wheel is hidden.
- `Tab` view-cycling generalises to any wheel with more than one view; no hardcoded
  per-tool check.
