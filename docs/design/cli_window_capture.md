---
title: CLI Window Capture Backends
summary: Current public semantics for `--window-capture auto|gdi|wgc`, including fallback rules, minimized-match behavior, and warning differences.
audience:
  - users
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - cli
  - windows
  - capture
  - wgc
---

# CLI Window Capture Backends

This document describes the current shipped behavior of
`--window-capture auto|gdi|wgc`.

The option is CLI-only. It applies to one-shot window capture and does not
change overlay capture, tray actions, or other interactive workflows.

For the broader CLI contract, see [README.md](../../README.md). For the
implementation-focused contributor reference, see
[cli_window_capture_wgc_design.md](cli_window_capture_wgc_design.md).

## CLI Contract

### Scope and parsing rules

Current parser behavior:

- `--window-capture` accepts `auto`, `gdi`, or `wgc`
- values are parsed case-insensitively
- the default is `auto`
- the option may be specified at most once
- the option is valid only with:
  - `--window "<title>"`
  - `--window-hwnd <hex>`
- the option is rejected with:
  - `--region`
  - `--monitor`
  - `--desktop`
  - `--input`
  - no window capture source

Examples:

```bat
greenflame.exe --window "Notepad" --window-capture auto --output "%TEMP%\note-auto.png" --overwrite
greenflame.exe --window "Notepad" --window-capture gdi --output "%TEMP%\note-gdi.png" --overwrite
greenflame.exe --window "Notepad" --window-capture wgc --output "%TEMP%\note-wgc.png" --overwrite
greenflame.exe --window-hwnd 0x0000000000123456 --window-capture wgc --output "%TEMP%\note-hwnd.png" --overwrite
```

## Backend Modes

### `auto`

`auto` is the current default.

Current behavior:

- Greenflame tries Windows Graphics Capture (WGC) first
- if WGC succeeds, the command completes without the usual GDI-only obscuration
  or off-desktop warning text
- if WGC returns a backend failure, Greenflame emits this info line and then
  falls back to GDI:

```text
Info: WGC window capture failed; falling back to GDI.
```

- if the GDI fallback succeeds, the command succeeds under normal GDI semantics
- if the window has no capturable intersection with the virtual desktop for the
  GDI path, the command still fails after the info line

Important current limitation:

- only WGC backend failures trigger fallback
- later save/encode failures do not trigger fallback to GDI

### `gdi`

`gdi` forces the visible-desktop capture path.

Current behavior:

- captures from the virtual desktop and crops to the resolved window rect
- preserves the existing warning model for:
  - fully obscured windows
  - partially obscured windows
  - windows partially outside visible desktop bounds
  - padded captures that require synthetic fill outside the virtual desktop

`gdi` therefore reflects what is currently visible and capturable from the
desktop, not the idealized window content.

### `wgc`

`wgc` forces Windows Graphics Capture for the resolved window.

Current behavior:

- does not fall back to GDI
- suppresses the usual GDI-only obscuration and off-desktop warning text when
  WGC succeeds
- returns exit code `15` if the WGC backend is unsupported or fails before the
  image reaches the normal save step

Representative forced-`wgc` failure cases include:

- unsupported WGC platform or support probe failure
- WGC setup failure for the target window
- timeout or failure while waiting for the first frame
- WGC frame-size mismatch versus the resolved window rect

## Window Selection And Minimized Behavior

### Shared pre-backend checks

Before either backend runs, window capture still enforces the same shared
window-availability checks:

- invalid or disappeared windows fail
- minimized windows fail with exit code `13`
- windows marked uncapturable through `WDA_EXCLUDEFROMCAPTURE` fail with exit
  code `17`

These checks happen before backend save work starts.

### Title-based minimized-match behavior

Current title-query behavior differs between `gdi` and the WGC-capable modes.

For `auto` and `wgc`:

- if no visible title matches remain, Greenflame counts minimized matches with
  the same query
- if all matching windows are minimized, the command exits with code `13`
  instead of saying no window matched
- if one visible match is captured while additional matches are minimized,
  capture succeeds and `stderr` includes a warning that those minimized matches
  were skipped

For `gdi`:

- title matching keeps the older visible-window-only behavior
- minimized matches do not change the result from "no visible window matches"

This difference is intentional and is covered by both automated tests and the
manual plan.

## Behavioral Differences

### Obscured windows

- `gdi`: may include covering windows or omit the target window content when it
  is obscured
- `wgc`: targets the window itself rather than visible desktop pixels, so an
  obscured window can still capture correctly when WGC succeeds

### Windows partially outside the desktop

- `gdi`: can clip the target window to the visible virtual desktop
- `gdi` with padding: preserves the nominal window extent and fills uncovered
  source holes with the resolved padding color
- `wgc`: returns the full window image when WGC succeeds, so partial
  off-desktop placement does not create the same missing-source-hole behavior

### Auto fallback on off-screen windows

One current edge case matters for `auto`:

- if WGC fails
- and the resolved window rect has no intersection with the virtual desktop
- Greenflame still emits the fallback info line
- but then returns the normal outside-the-virtual-desktop error instead of
  attempting a doomed GDI save

## Padding, Cursor, And Annotations

All three backends work with the rest of the current CLI window-render pipeline.

Current semantics:

- `--padding` still means synthetic outer padding only
- `--annotate` still renders annotations after capture and padding
- local annotation coordinates remain relative to the window image origin
- global annotation coordinates remain tied to the resolved window rect in
  screen coordinates

Current cursor behavior with successful `wgc` capture:

- the WGC session disables native cursor capture
- Greenflame optionally captures its own cursor snapshot
- that cursor snapshot is composited onto the captured window image before
  annotation rendering

This keeps cursor behavior aligned with the other CLI capture paths and avoids a
double-cursor result from WGC itself.

## Errors And Exit Codes

Relevant exit codes for CLI window capture:

| Code | Meaning |
| --- | --- |
| `6` | `--window` matched no visible window |
| `7` | `--window` remained ambiguous |
| `11` | Capture/save operation failed |
| `12` | Matched window became unavailable before capture |
| `13` | Matched window is minimized |
| `15` | Forced `--window-capture wgc` failed |
| `17` | Matched window is protected from screen capture |

`auto` does not use exit code `15` when the WGC attempt fails but GDI fallback
later succeeds.

## Manual Coverage

Manual case [docs/manual_test_plan.md](../manual_test_plan.md) currently covers:

- visible-window parity between `gdi`, `wgc`, and `auto`
- obscured-window differences
- partially off-screen behavior with padding
- `--window-hwnd` parity
- annotation composition on `wgc`
- minimized-window failure behavior
- minimized title-match warnings and errors
- observation of any yellow capture border during WGC use
