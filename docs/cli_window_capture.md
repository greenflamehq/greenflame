---
title: CLI Window Capture Backends
summary: Public CLI semantics for `--window-capture auto|gdi|wgc`.
audience:
  - users
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-03-22
tags:
  - cli
  - windows
  - capture
  - wgc
---

# CLI Window Capture Backends

`--window-capture` is a CLI-only option for one-shot window captures.

It applies only to:

- `--window "<title>"`
- `--window-hwnd <hex>`

It does not affect:

- interactive overlay capture
- tray capture actions
- clipboard hotkeys

## Syntax

```bat
greenflame.exe --window "<title>" --window-capture auto --output "<file>"
greenflame.exe --window "<title>" --window-capture gdi --output "<file>"
greenflame.exe --window "<title>" --window-capture wgc --output "<file>"
greenflame.exe --window-hwnd 0x0000000000123456 --window-capture wgc --output "<file>"
```

If `--window-capture` is omitted, Greenflame uses:

```text
auto
```

## Backend Modes

### `auto`

- Tries Windows Graphics Capture (WGC) first.
- If WGC backend setup or capture fails for the matched window, prints one info line to `stderr` and falls back to GDI.
- If WGC succeeds, Greenflame does not emit the usual GDI-only warnings about obscuration or off-screen clipping.

Fallback info line:

```text
Info: WGC window capture failed; falling back to GDI.
```

### `gdi`

- Uses the existing GDI desktop-capture path.
- Captures what is available from the visible virtual desktop, then crops to the target window rect.
- Keeps the existing warning semantics for:
  - fully obscured windows
  - partially obscured windows
  - windows partially outside visible desktop bounds
  - padded captures that require fill outside the virtual desktop

### `wgc`

- Forces WGC for the target window.
- Does not fall back to GDI.
- Suppresses the usual GDI-only obscuration/off-screen warnings.
- If WGC is unavailable or fails for that window, the command exits with code `15`.

## Behavioral Differences

### Obscured windows

- `gdi`: may include covering windows or miss the target window entirely.
- `wgc`: intended to capture the target window itself, even when another window is in front of it.

### Windows partially outside the desktop

- `gdi`: only captures the visible portion of the window from the virtual desktop.
- `wgc`: captures the full target window frame if WGC succeeds.

### Minimized windows

- Still rejected in all modes.
- Exit code remains `13`.
- For title-based `--window "<title>"` in `wgc` mode, and in `auto`, Greenflame
  distinguishes minimized matches from true "not found" cases.
- If every matching title is minimized, the command reports that the matching
  windows are minimized instead of saying no window matched.
- If one capturable title match is used while additional matching windows are
  minimized, capture proceeds and `stderr` includes a warning that those
  minimized matches were skipped.

Greenflame does not use WGC to capture minimized windows in this feature.

## Padding And Annotations

When `--window-capture wgc` or `auto` resolves to WGC:

- the captured source is the full window image returned by WGC
- `--padding` still adds only synthetic outer padding
- annotations still render last, after capture and padding
- local annotation coordinates still treat the window capture as `(0,0)` at the window's top-left
- global annotation coordinates still use the window's resolved screen rect

This means WGC window capture works with:

- `--padding`
- `--padding-color`
- `--annotate`

## Errors And Exit Codes

Relevant window-capture exit codes:

| Code | Meaning |
| --- | --- |
| `11` | General capture/save failure |
| `13` | Matched window is minimized |
| `15` | Forced `wgc` failed |

Exit code `15` is used for forced `wgc` failures such as:

- WGC unsupported on the system
- WGC setup/init failure for the matched window
- failure to receive a frame
- captured WGC frame size not matching the resolved window rect

`auto` does not use exit `15` when GDI fallback succeeds.

## Examples

Visible window, default backend selection:

```bat
greenflame.exe --window "Untitled - Notepad" --output "%TEMP%\note-auto.png" --overwrite
```

Force legacy GDI semantics:

```bat
greenflame.exe --window "Untitled - Notepad" --window-capture gdi --output "%TEMP%\note-gdi.png" --overwrite
```

Force WGC for an obscured or partially off-desktop window:

```bat
greenflame.exe --window "Untitled - Notepad" --window-capture wgc --output "%TEMP%\note-wgc.png" --overwrite
```

Capture by exact `HWND` and annotate the result:

```bat
greenflame.exe --window-hwnd 0x0000000000123456 --window-capture wgc --padding 32 --annotate ".\\schemas\\examples\\cli_annotations\\global_padding_edge_cases.json" --output "%TEMP%\\note-annotated.png" --overwrite
```

## Related Docs

- [README.md](../README.md)
- [docs/cli_annotations.md](cli_annotations.md)
- [docs/manual_test_plan.md](manual_test_plan.md)
