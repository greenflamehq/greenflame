# greenflame

[![CI](https://github.com/joce/greenflame/actions/workflows/ci.yml/badge.svg)](https://github.com/joce/greenflame/actions/workflows/ci.yml)

<img src="images/greenflame_256.png" alt="Greenflame" width="128" align="left" />

Yet another Windows screenshot tool, with very opinionated set of behaviors.  `¯\_(ツ)_/¯`

A selection scheme inspired by [Greenshot](https://greenshot.org/)
<img src="https://avatars.githubusercontent.com/u/5614224?s=200&v=4" alt="Greenshot" style="width: 16px; height: 16px;">, an editing scheme inspired by [Flameshot](https://flameshot.org/) <img src="https://flameshot.org/flameshot-icon.svg" alt="Flameshot" style="width: 16px; height: 16px;">, all in one tool.

<br clear="left"/>

---

## Usage

**Run** `greenflame.exe`. The executable sits in the tray.

Press the **Print Screen** key, or **left-click** the tray icon to start an interactive capture. Alternatively, **right-click** the tray icon and choose a capture mode from the context menu.

In interactive mode, the screen is captured and a region is selected:

- **Click and drag** to select a rectangle (hold **Alt** to disable snapping).
- **Ctrl** + click ➜ select the window under the cursor.
- **Shift** + click ➜ select the whole monitor under the cursor.
- **Shift+Ctrl** + click ➜ select the whole desktop.

Once a region is selected:

- **Drag the handles** on the selection to resize (hold **Alt** to disable snapping).
- With **no annotation tool selected** (the default mode), **click and drag inside the selection** to **move** it (hold **Alt** to disable snapping).
- With **no annotation tool selected**, **click and drag an annotation** to **select** and **move** it.
- With **no annotation tool selected**, a **selected line or arrow annotation** shows draggable endpoint handles you can drag to reshape it.
- With **no annotation tool selected**, a **selected rectangle annotation** shows draggable resize handles on the corners and sides.
- Press **B** or use the toolbar to toggle the **Brush tool** on or off.
- Press **H** or use the toolbar to toggle the **Highlighter tool** on or off.
- Press **L** or use the toolbar to toggle the **Line tool** on or off.
- Press **A** or use the toolbar to toggle the **Arrow tool** on or off.
- Press **R** or use the toolbar to toggle the **Rectangle tool** on or off.
- Press **F** or use the toolbar to toggle the **Filled Rectangle tool** on or off.
- Press **T** or use the toolbar to toggle the **Text tool** on or off.
- Press **N** or use the toolbar to toggle the **Bubble tool** on or off.
- With an annotation tool active, **right-click** anywhere to open the active tool's **color wheel** at the cursor. **Left-click** a segment to select that color, or press **Escape** to dismiss the wheel.
- With the **Brush**, **Highlighter**, **Line**, **Arrow**, **Rectangle**, or **Bubble** tool active, use **mouse-wheel up/down** or **Ctrl+= / Ctrl+-** to change stroke width from **1** to **50**.
- With the **Text tool** active and no draft open, **left-click inside the selection** to start a text annotation at the click point.
- With the **Text tool** active and no draft open, **right-click** opens a **12-segment text style wheel**: 8 annotation-color slots on the left half and 4 font choices on the right half.
- With the **Text tool** active and no draft open, use **mouse-wheel up/down** or **Ctrl+= / Ctrl+-** to change text point size. The chosen size is persisted.
- With the **Bubble tool** active, **left-click inside the selection** to place a numbered circle. The number auto-increments with each placement and decrements on undo. **Right-click** opens a **12-segment style wheel**: 8 annotation-color slots on the left half and 4 font choices on the right half. The number color is chosen automatically for contrast (black on light fills, white on dark fills).
- With the **Brush** or **Bubble tool** active, the overlay shows an anti-aliased circular size preview around the cursor hotspot.
- With the **Highlighter tool** active, the overlay shows an anti-aliased axis-aligned square size preview around the cursor hotspot.
- With the **Line** or **Arrow** tool active, the overlay shows an anti-aliased square size preview around the cursor hotspot aligned to the current line direction.
- The **Rectangle** and **Filled Rectangle** tools do not draw a cursor size preview overlay.
- While editing text, **Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V** work on the active draft, and **Ctrl-Z / Ctrl-Shift-Z** affect only that draft.
- While editing text, **Ctrl+B**, **Ctrl+I**, **Ctrl+U**, and **Alt+Shift+5** toggle bold, italic, underline, and strikethrough.
- While editing text, **Insert** toggles insert/overwrite mode, **Ctrl+Enter** inserts a newline, **Enter** commits the draft, and **Escape** cancels the draft while keeping the Text tool armed.
- Clicking outside a text draft commits it if it has text, otherwise discards it. Clicking a toolbar button behaves the same way before applying the button action.
- Committed text annotations can be selected, moved, and deleted, but they are not re-editable as live text.
- **Delete** ➜ remove the selected annotation.
- **Ctrl-Z** ➜ undo the last region or annotation change.
- **Ctrl-Shift-Z** ➜ redo the last undone region or annotation change.
- **Ctrl-S** ➜ save directly (no dialog) to the configured default save folder as the configured format (default PNG), then close.
- **Ctrl-Shift-S** ➜ open **Save As** dialog, then save and close.
- **Ctrl-Alt-S** ➜ save directly (no dialog), copy the saved file to the clipboard, then close.
- **Ctrl-Shift-Alt-S** ➜ open **Save As** dialog, save, copy the saved file to the clipboard, then close.
- **Ctrl-C** ➜ copy the selection to the clipboard, then close.
- **Escape** ➜ cancel or go back.

**Save As** supports **PNG**, **JPEG**, and **BMP**.

---

## Hotkeys

All hotkeys use the **Print Screen** key with modifier combinations. They are also available from the tray icon's right-click context menu.

| Hotkey | Action |
|--------|--------|
| **Prt Scrn** | Start interactive capture (select a region) |
| **Ctrl + Prt Scrn** | Copy the current window to the clipboard |
| **Shift + Prt Scrn** | Copy the current monitor to the clipboard |
| **Ctrl + Shift + Prt Scrn** | Copy the full desktop to the clipboard |
| **Alt + Prt Scrn** | Recapture the last captured region (same screen coordinates) |
| **Ctrl + Alt + Prt Scrn** | Recapture the last captured window (wherever it is now) |

The last two hotkeys require a previous capture in the current session. If no previous capture exists, or if the previously captured window has been closed or minimized, a warning toast is shown.

---

## Command-line mode

With no parameters, Greenflame starts normally in the tray.

You can also run one-shot command-line modes (at most one mode per invocation):

| Option | Meaning |
|---|---|
| `-r, --region <x,y,w,h>` | Capture an explicit physical-pixel region |
| `-w, --window <name>` | Capture a visible top-level window whose title matches `<name>` (case-insensitive contains) |
| `-m, --monitor <id>` | Capture monitor by 1-based id |
| `-d, --desktop` | Capture the full virtual desktop |
| `-h, --help` | Show help and exit |
| `-v, --version` | Show version and exit |

Optional:

| Option | Meaning |
|---|---|
| `-o, --output <path>` | Output file path (valid only with a capture mode) |
| `-t, --format <png\|jpg\|jpeg\|bmp>` | Output format override |
| `-f, --overwrite` | Allow replacing an existing explicit `--output` file |

Both `--option=value` and `--option value` forms are supported.

Examples:

```bat
greenflame.exe --desktop
greenflame.exe --desktop --format jpeg
greenflame.exe --monitor 2 --output "D:\shots\monitor2.png"
greenflame.exe --window "Notepad" --output "D:\shots\note" --format jpg
greenflame.exe --window "Notepad" --output "D:\shots\note.jpg" --overwrite
greenflame.exe --window="Notepad" --output "D:\shots\note"
greenflame.exe --region 1200,100,800,600
```

**A note on output format resolution**

1. If `--output` has a supported extension (`.png`, `.jpg`, `.jpeg`, `.bmp`), that extension defines the format.
2. Otherwise, if `--format` is provided, `--format` defines the format.
3. Otherwise, `default_save_format` (from `[save]` in the config) defines the format.
4. If `--output` extension conflicts with `--format`, the command fails.
5. If `--output` has an unsupported extension (for example `.tiff`), the command fails.
6. If `--output` has no extension, Greenflame appends one based on the resolved format.

### Exit codes

Greenflame uses these process exit codes for command-line invocations. Non-zero
codes are unique and not reused.

| Code | Meaning |
|---|---|
| `0` | Success (includes `--help`, `--version`, and "already running" tray startup) |
| `1` | Failed to register application window classes |
| `2` | CLI argument parse/validation failed |
| `3` | Failed to create tray window |
| `4` | Failed to enforce single-instance tray mode |
| `5` | `--region` capture requested but region data is missing |
| `6` | `--window` matched no visible window |
| `7` | `--window` matched multiple windows (ambiguous) |
| `8` | `--monitor` capture requested but no monitors are available |
| `9` | `--monitor` id is out of range |
| `10` | Output path resolution/reservation failed |
| `11` | Capture/save operation failed |
| `12` | Matched window became unavailable before capture |
| `13` | Matched window is minimized |

---

## Configuration

Greenflame reads `~/.config/greenflame/greenflame.ini` (i.e. `%USERPROFILE%\.config\greenflame\greenflame.ini`).

### All config keys

| Section | Key | Default | Meaning |
|---|---|---|---|
| `[ui]` | `show_balloons` | `true` | Show tray toast notifications after copy/save actions. |
| `[ui]` | `show_selection_size_side_labels` | `true` | Show selection-size labels outside the selection (width on top/bottom and height on left/right). |
| `[ui]` | `show_selection_size_center_label` | `true` | Show centered `W x H` selection-size label inside the selection. |
| `[ui]` | `tool_size_overlay_duration_ms` | `800` | How long the centered tool-size overlay stays visible after a stroke-width change. `0` disables it. |
| `[tools]` | `brush_width` | `2` | Default Brush/Highlighter/Line/Arrow/Rectangle/Bubble stroke width (diameter for Bubble) in physical pixels. Runtime adjustments are clamped to `1..50` and persisted here. Filled rectangles ignore it. |
| `[tools]` | `current_color` | `0` | Current annotation color slot index, clamped to `0..7`. |
| `[tools]` | `color_0` ... `color_7` | `#000000`, `#ff0000`, `#00ff00`, `#0000ff`, `#ffff00`, `#ff00ff`, `#00ffff`, `#ffffff` | Annotation color wheel slots, starting at the top-right segment and moving clockwise. Values use `#rrggbb`. |
| `[tools]` | `highlighter_current_color` | `0` | Current Highlighter color slot index, clamped to `0..5`. |
| `[tools]` | `highlighter_color_0` ... `highlighter_color_5` | `#f7eb62`, `#7fe36a`, `#ffb44d`, `#ff79b9`, `#64c7ff`, `#c38cff` | Highlighter color wheel slots, starting at the top-right segment and moving clockwise. Values use `#rrggbb`. |
| `[tools]` | `highlighter_opacity_percent` | `50` | Default Highlighter opacity for live preview, save output, and clipboard output. Values are clamped to `0..100`. |
| `[tools]` | `text_size_points` | `12` | Default Text tool point size. Values are normalized to the nearest supported preset (`5, 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72, 84, 96, 108, 144, 192, 216, 288`). Runtime adjustments are persisted here. |
| `[tools]` | `text_current_font` | `sans` | Active font slot for the Text tool. Accepted values: `sans`, `serif`, `mono`, `art`. |
| `[tools]` | `bubble_current_font` | `sans` | Active font slot for the Bubble tool. Accepted values: `sans`, `serif`, `mono`, `art`. |
| `[tools]` | `text_font_sans` | `Arial` | Font family used for the `sans` slot. Trimmed; falls back to `Arial` if blank or too long. |
| `[tools]` | `text_font_serif` | `Times New Roman` | Font family used for the `serif` slot. Trimmed; falls back to `Times New Roman` if blank or too long. |
| `[tools]` | `text_font_mono` | `Courier New` | Font family used for the `mono` slot. Trimmed; falls back to `Courier New` if blank or too long. |
| `[tools]` | `text_font_art` | `Comic Sans MS` | Font family used for the `art` slot. Trimmed; falls back to `Comic Sans MS` if blank or too long. |
| `[save]` | `default_save_dir` | `%USERPROFILE%\Pictures\greenflame` (runtime fallback when unset) | Folder used by **Ctrl-S**, **Ctrl-Alt-S**, and CLI captures when `--output` is not provided. |
| `[save]` | `last_save_as_dir` | Falls back to `default_save_dir`, then `%USERPROFILE%\Pictures\greenflame` | Initial folder used by **Ctrl-Shift-S** and **Ctrl-Shift-Alt-S** (Save As). |
| `[save]` | `default_save_format` | `png` | Default image format for **Ctrl-S**, **Ctrl-Alt-S**, and CLI output paths without explicit extension. Accepted values: `png`, `jpg`/`jpeg`, `bmp`. |
| `[save]` | `filename_pattern_region` | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}` | Default filename pattern for region captures. |
| `[save]` | `filename_pattern_desktop` | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}` | Default filename pattern for desktop captures. |
| `[save]` | `filename_pattern_monitor` | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-monitor${monitor}` | Default filename pattern for monitor captures. |
| `[save]` | `filename_pattern_window` | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-${title}` | Default filename pattern for window captures. |

Example:

```ini
[ui]
show_balloons=true
show_selection_size_side_labels=true
show_selection_size_center_label=true
tool_size_overlay_duration_ms=800

[tools]
brush_width=2
current_color=0
color_0=#000000
color_1=#ff0000
color_2=#00ff00
color_3=#0000ff
color_4=#ffff00
color_5=#ff00ff
color_6=#00ffff
color_7=#ffffff
highlighter_current_color=0
highlighter_color_0=#f7eb62
highlighter_color_1=#7fe36a
highlighter_color_2=#ffb44d
highlighter_color_3=#ff79b9
highlighter_color_4=#64c7ff
highlighter_color_5=#c38cff
highlighter_opacity_percent=50
text_size_points=12
bubble_current_font=sans

[save]
default_save_dir=C:\Users\you\Pictures\greenflame
last_save_as_dir=D:\shots\scratch
default_save_format=png
filename_pattern_region=screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}
filename_pattern_desktop=screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}
filename_pattern_monitor=screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-monitor${monitor}
filename_pattern_window=screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-${title}
```

### Save filenames

Saved files use one pattern per capture type and Greenshot-style `${VARIABLE}` placeholders.

#### Filename patterns

##### Supported variables

| Variable | Expansion | Example |
|----------|-----------|---------|
| `${YYYY}` | 4-digit year | `2026` |
| `${YY}` | 2-digit year | `26` |
| `${MM}` | 2-digit month | `02` |
| `${DD}` | 2-digit day | `21` |
| `${hh}` | 2-digit hour (24h) | `14` |
| `${mm}` | 2-digit minute | `30` |
| `${ss}` | 2-digit second | `25` |
| `${title}` | Sanitized window title (spaces and invalid filename chars become `_`; max 50 chars; falls back to `window`) | `My_App` |
| `${monitor}` | 1-based monitor number | `2` |
| `${num}` | Incrementing counter (6-digit, zero-padded, next available by directory scan) | `000042` |

##### Default patterns

| Capture type | Default pattern | Example output |
|---|---|---|
| Region | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}` | `screenshot-2026-02-21_143025` |
| Desktop | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}` | `screenshot-2026-02-21_143025` |
| Monitor | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-monitor${monitor}` | `screenshot-2026-02-21_143025-monitor2` |
| Window | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-${title}` | `screenshot-2026-02-21_143025-My_App` |

---

## Build and test

- Build instructions: [docs/build.md](docs/build.md)
- Test instructions: [docs/testing.md](docs/testing.md)
- Agent-specific repository rules: [AGENTS.md](AGENTS.md)

---

**[MIT](LICENSE) License**
