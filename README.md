# greenflame

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
- Hold **Tab** and drag inside the selection to **move** it (hold **Alt** to disable snapping).
- **Ctrl-S** ➜ save directly (no dialog) to the last-used folder as the configured format (default PNG), then close.
- **Ctrl-Shift-S** ➜ open **Save As** dialog, then save and close.
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

You can also run one-shot capture commands (exactly one capture mode per invocation):

| Option | Meaning |
|---|---|
| `-r, --region <x,y,w,h>` | Capture an explicit physical-pixel region |
| `-w, --window <name>` | Capture a visible top-level window whose title matches `<name>` (case-insensitive contains) |
| `-m, --monitor <id>` | Capture monitor by 1-based id |
| `-d, --desktop` | Capture the full virtual desktop |
| `-h, --help` | Show help and exit |

Optional:

| Option | Meaning |
|---|---|
| `-o, --output <path>` | Output file path (valid only with a capture mode) |

Both `--option=value` and `--option value` forms are supported.

Examples:

```bat
greenflame.exe --desktop
greenflame.exe --monitor 2 --output "D:\shots\monitor2.png"
greenflame.exe --window="Notepad" --output "D:\shots\note"
greenflame.exe --region -1200,100,800,600
```

---

## Filename patterns

Saved files are named using configurable patterns with Greenshot-style `${VARIABLE}` placeholders. Each capture type has its own pattern, editable in `%APPDATA%\greenflame\greenflame.json` under the `"save"` section.

### Supported variables

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

### Default patterns

| Capture type | Default pattern | Example output |
|---|---|---|
| Region | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}` | `screenshot-2026-02-21_143025` |
| Desktop | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}` | `screenshot-2026-02-21_143025` |
| Monitor | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-monitor${monitor}` | `screenshot-2026-02-21_143025-monitor2` |
| Window | `screenshot-${YYYY}-${MM}-${DD}_${hh}${mm}${ss}-${title}` | `screenshot-2026-02-21_143025-My_App` |

To customize, add the corresponding key to the config file:

```json
{
  "save": {
    "filename_pattern_region": "${YYYY}${MM}${DD}_${hh}${mm}${ss}",
    "filename_pattern_window": "Greenflame-${title}-${YYYY}${MM}${DD}"
  }
}
```

Only patterns explicitly set in the config file are saved; omitted keys use the built-in defaults.

### Direct save format

The `default_save_format` setting controls which format **Ctrl-S** uses. Accepted values: `"png"` (default), `"jpg"`, `"bmp"`.

```json
{
  "save": {
    "default_save_format": "jpg"
  }
}
```

### Save directories

Save paths are configured independently:

- `default_save_dir`: used by **Ctrl-S** and CLI captures that do not pass `--output`.
- `last_save_as_dir`: used as the initial folder for **Ctrl-Shift-S** (Save As).

```json
{
  "save": {
    "default_save_dir": "C:\\Users\\you\\Pictures\\greenflame",
    "last_save_as_dir": "D:\\shots\\scratch"
  }
}
```

---

## Build and test

- Build instructions: [docs/build.md](docs/build.md)
- Test instructions: [docs/testing.md](docs/testing.md)
- Agent-specific repository rules: [AGENTS.md](AGENTS.md)

---

**License** — [MIT](LICENSE)
