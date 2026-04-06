# greenflame

[!\[CI](https://github.com/joce/greenflame/actions/workflows/ci.yml/badge.svg)](https://github.com/joce/greenflame/actions/workflows/ci.yml)

<img src="images/greenflame\_256.png" alt="Greenflame" width="128" align="left" />

Yet another Windows screenshot tool, with very opinionated set of behaviors.  `¯\\\_(ツ)\_/¯`

A selection scheme inspired by [Greenshot](https://greenshot.org/)
<img src="https://avatars.githubusercontent.com/u/5614224?s=200\&v=4" alt="Greenshot" style="width: 16px; height: 16px;">, an editing scheme inspired by [Flameshot](https://flameshot.org/) <img src="https://flameshot.org/flameshot-icon.svg" alt="Flameshot" style="width: 16px; height: 16px;">, all in one tool.

<br clear="left"/>

\---

## Usage

### Starting a capture

**Run** `greenflame.exe`. The executable sits in the tray.

Press the **Print Screen** key, or **left-click** the tray icon to start an interactive capture. Alternatively, **right-click** the tray icon and choose a capture mode from the context menu.

The tray menu also includes a persisted `Include captured cursor` toggle for the
default live-capture behavior, next to the other app-level settings.

\---

### Selecting a region

In interactive mode, the screen is captured and a region is selected:

* **Click and drag** to select a rectangle (hold **Alt** to disable snapping).
* **Ctrl** + click ➜ select the window under the cursor. While `Ctrl` is held,
Greenflame lifts the pending window preview above occluding windows. If that
window extends off-screen, the full window is still captured, but only the
visible on-desktop portion is editable; while the interactive size label is
shown, a smaller note under the center `width x height` label indicates that
off-screen pixels are included, and the size labels continue to reflect the
full captured window size. If you manually move or resize that selection, it
immediately becomes a normal region selection.
* **Shift** + click ➜ select the whole monitor under the cursor.
* **Shift+Ctrl** + click ➜ select the whole desktop.

Once a region is selected:

* **Drag the handles** on the selection to resize (hold **Alt** to disable snapping).
* Use the **captured cursor** button on the toolbar, or press **Ctrl+K**, to show or
hide the cursor that was sampled from the captured screen image. This affects the
frozen screenshot only, not the live editing pointer.
* Use the **pin** button on the toolbar, or press **Ctrl+P**, to create an
always-on-top pinned image from the current rendered selection. On success, the
overlay closes and the pin stays on screen. Pins created from the interactive
`Ctrl` window path use the full captured window bitmap at 100% scale, so they may
initially appear partly off-screen when the source window extended beyond the
desktop.

With **no annotation tool selected** (the default mode):

* **Click and drag inside the selection** to **move** it (hold **Alt** to disable snapping).
* **Click and drag an annotation** to **select** it; once selected, drag anywhere inside its selection box to move it.
* **Ctrl+click** an annotation to add it to or remove it from the current annotation selection.
* **Ctrl+drag** a marquee to add every touched annotation to the current selection.
* When **multiple annotations** are selected, drag anywhere inside the group selection box to move the whole group.
* A **selected line or arrow annotation** shows draggable endpoint handles you can drag to reshape it.
* When **exactly one** rectangle, ellipse, or obfuscate annotation is selected, it shows draggable resize handles on the corners and sides.

\---

### Annotation tools

Press a hotkey or use the toolbar to toggle a tool on or off.

|Hotkey|Tool|
|-|-|
|**B**|Brush|
|**H**|Highlighter|
|**L**|Line|
|**A**|Arrow|
|**R**|Rectangle|
|**Shift+R**|Filled Rectangle|
|**E**|Ellipse|
|**Shift+E**|Filled Ellipse|
|**O**|Obfuscate|
|**T**|Text|
|**N**|Bubble|

**Bubble:** Left-click inside the selection to place a numbered circle. The number auto-increments with each placement and decrements on undo. Right-click opens a style wheel; a central hub switches it between 8 color slots and 4 font choices. The number color is chosen automatically for contrast (black on light fills, white on dark fills).

**Highlighter:** While drawing, holding the mouse still for 800 ms snaps the stroke to a straight bar from the start point to the cursor. After snapping, the end of the bar tracks the mouse live until release — the snap is one-way and cannot be reverted to freehand. The wait time is configurable via `tools.highlighter.pause\_straighten\_ms` (see [Configuration](#configuration)); setting it to `0` makes every stroke start as a straight bar immediately.

**Obfuscate:** Drag out a rectangle to blur or pixelate the content underneath it. `tools.obfuscate.block\_size = 1` uses blur mode; `2..50` uses block pixelation. Obfuscates can be moved and resized later, and they recompute when lower overlapping annotations change.

### Tool options

**Color:** With an annotation tool other than **Obfuscate** active, **right-click** anywhere (or press **Tab**) to open that tool's **selection wheel** at the cursor. Left-click a segment or press **Enter** to select it. Use **mouse-wheel up/down** or **↑ / ↓** to navigate between segments without the mouse. Press **Escape** to dismiss without selecting. For Text and Bubble, the wheel has a central hub that switches between 8 color slots and 4 font choices; pressing **Tab** while the wheel is open cycles between the color and font views.

**Size:** With **Brush**, **Highlighter**, **Line**, **Arrow**, **Rectangle**, **Ellipse**, **Bubble**, **Obfuscate**, or **Text** active, use **mouse-wheel up/down** or **Ctrl+= / Ctrl+-** to change that tool's size step (1–50). Each tool has its own independent persisted setting. For Obfuscate, the setting is `block\_size`: `1` means blur and `2..50` means block pixelation.

**Cursor previews:**

* **Brush** and **Bubble** — anti-aliased circular preview around the cursor hotspot.
* **Highlighter**, **Line**, **Rectangle**, **Ellipse**, and **Obfuscate** — anti-aliased axis-aligned square preview around the cursor hotspot.
* **Arrow** — anti-aliased square preview aligned to the current arrow direction.
* **Text** — the letter "A" in the current font and size, shown beside the cursor hotspot.
* **Filled Rectangle** and **Filled Ellipse** — no cursor preview.

### Text tool

With the Text tool active and no draft open:

* **Left-click inside the selection** to start a text annotation at the click point.
* **Right-click** (or **Tab**) to open the selection wheel. A central hub switches between 8 annotation-color slots and 4 font choices.
* **Mouse-wheel up/down** or **Ctrl+= / Ctrl+-** to change text size step (1–50, mapped to 5–288 pt). The chosen step is persisted.

While editing a draft:

|Shortcut|Action|
|-|-|
|**Ctrl+A / C / X / V**|Clipboard operations on the draft|
|**Ctrl-Z / Ctrl-Shift-Z**|Undo / redo within the draft|
|**Ctrl+B / I / U**|Bold / italic / underline|
|**Alt+Shift+5**|Strikethrough|
|**Insert**|Toggle insert / overwrite mode|
|**Ctrl+Enter**|Insert a newline|
|**Enter**|Commit the draft|
|**Escape**|Cancel the draft, keep Text tool armed|

Clicking outside a draft commits it if it has text, otherwise discards it. Clicking a toolbar button does the same before applying the button action.

Committed text annotations can be selected, moved, and deleted, but are not re-editable as live text.

### Saving, copying, and pinning

|Shortcut|Action|
|-|-|
|**Ctrl-S**|Save directly to the configured default save folder in the configured format, then close|
|**Ctrl-Shift-S**|Open **Save As** dialog, then save and close|
|**Ctrl-Alt-S**|Save directly in the configured format, copy the saved file to the clipboard, then close|
|**Ctrl-Shift-Alt-S**|Open **Save As** dialog, save, copy the saved file to the clipboard, then close|
|**Ctrl-C**|Copy the selection to the clipboard, then close|
|**Ctrl-P**|Pin the rendered selection to the desktop, then close|

**Save As** supports **PNG**, **JPEG**, and **BMP**.

When the captured cursor is shown, it is composited into the screenshot below all
annotations. It is never selectable or movable.

### Pinned images

Pinned images are frameless, always-on-top reference windows with an always-visible
green halo. The halo gets stronger when the pin is active. Multiple pins can stay
open at once.

* **Drag anywhere on the pin** to move it.
* Use the **mouse wheel** or **Ctrl+= / Ctrl+-** to zoom in or out.
* Pin zoom is clamped to **25% .. 800%**.
* **Right-click** the pin to open its context menu.
* Pins resize by zoom only; there are no border or corner resize handles.
* Copy/save actions include the current **rotation**, but on-screen **opacity** and
the green halo are display-only and are not baked into the exported image.

Pin-window shortcuts:

|Shortcut|Action|
|-|-|
|**Ctrl-C**|Copy the active pinned image to the clipboard|
|**Ctrl-S**|Save the active pinned image to a file|
|**Ctrl-Right**|Rotate the active pin right|
|**Ctrl-Left**|Rotate the active pin left|
|**Ctrl+= / Ctrl+-**|Zoom the active pin in or out|
|**Ctrl-Up / Ctrl-Down**|Increase or decrease the active pin opacity; holding the keys repeats|
|**Escape**|Close the active pin|

### Other shortcuts

|Shortcut|Action|
|-|-|
|**Ctrl-K**|Show or hide the captured cursor in this screenshot|
|**Delete**|Remove the selected annotation or annotation group|
|**Ctrl-Z**|Undo the last region or annotation change|
|**Ctrl-Shift-Z**|Redo the last undone region or annotation change|
|**Escape**|Cancel or go back|

\---

## Hotkeys

All hotkeys use the **Print Screen** key with modifier combinations. They are also available from the tray icon's right-click context menu.

|Hotkey|Action|
|-|-|
|**Prt Scrn**|Start interactive capture (select a region)|
|**Ctrl + Prt Scrn**|Copy the current window to the clipboard|
|**Shift + Prt Scrn**|Copy the current monitor to the clipboard|
|**Ctrl + Shift + Prt Scrn**|Copy the full desktop to the clipboard|
|**Alt + Prt Scrn**|Recapture the last captured region (same screen coordinates)|
|**Ctrl + Alt + Prt Scrn**|Recapture the last captured window (wherever it is now)|

The last two hotkeys require a previous capture in the current session. If no previous capture exists, or if the previously captured window has been closed or minimized, a warning toast is shown.

These direct clipboard captures honor the persisted `capture.include\_cursor` setting.
They do not open the overlay, so there is no post-capture cursor toggle in those flows.

\---

## Command-line mode

With no parameters, Greenflame starts normally in the tray.

You can also run one-shot command-line modes (at most one mode per invocation):

|Option|Meaning|
|-|-|
|`-r, --region <x,y,w,h>`|Capture an explicit physical-pixel region|
|`-w, --window <name>`|Capture a visible top-level window by title text; a unique exact-title match wins over broader substring matches|
|`--window-hwnd <hex>`|Capture a visible top-level window by exact hex HWND|
|`-m, --monitor <id>`|Capture monitor by 1-based id|
|`-d, --desktop`|Capture the full virtual desktop|
|`--input <path>`|Load an existing PNG/JPEG/BMP image, apply `--annotate`, and save the result|
|`-h, --help`|Show help and exit|
|`-v, --version`|Show version and exit|

Optional:

|Option|Meaning|
|-|-|
|`-o, --output <path>`|Output file path (valid only with a render source)|
|`-t, --format <png\|jpg\|jpeg\|bmp>`|Output format override|
|`-p, --padding <n\|h,v\|l,t,r,b>`|Add synthetic padding around the rendered image in physical pixels|
|`--padding-color <#rrggbb>`|Override the padding color for this invocation only (valid only with `--padding`)|
|`--annotate <json\|path>`|Apply JSON-defined annotations to the saved CLI render result|
|`--window-capture <auto\|gdi\|wgc>`|CLI-only window-capture backend for `--window` / `--window-hwnd`; defaults to `auto`|
|`--cursor`|Include the captured cursor for this live-capture invocation only|
|`--no-cursor`|Exclude the captured cursor for this live-capture invocation only|
|`-f, --overwrite`|Allow replacing an existing explicit `--output` file|

Both `--option=value` and `--option value` forms are supported.

Examples:

```bat
greenflame.exe --desktop
greenflame.exe --desktop --format jpeg
greenflame.exe --desktop --padding 12
greenflame.exe --monitor 2 --output "D:\\shots\\monitor2.png"
greenflame.exe --monitor 2 --padding 24,12 --padding-color "#ffffff"
greenflame.exe --window "Notepad" --output "D:\\shots\\note" --format jpg
greenflame.exe --window "Notepad" --window-capture wgc --output "D:\\shots\\note-wgc.png"
greenflame.exe --window "Notepad" --window-capture wgc --cursor --output "D:\\shots\\note-wgc-cursor.png"
greenflame.exe --window-hwnd 0x0000000000123456 --output "D:\\shots\\exact-window.png"
greenflame.exe --window "Notepad" --output "D:\\shots\\note.jpg" --overwrite
greenflame.exe --window="Notepad" --output "D:\\shots\\note"
greenflame.exe --region 1200,100,800,600
greenflame.exe --region 1200,100,800,600 --padding 8,16,24,32
greenflame.exe --desktop --annotate "{\\"annotations\\":\[{\\"type\\":\\"line\\",\\"start\\":{\\"x\\":20,\\"y\\":20},\\"end\\":{\\"x\\":220,\\"y\\":120},\\"size\\":4}]}"
greenflame.exe --desktop --padding 64 --annotate ".\\\\schemas\\\\examples\\\\cli\_annotations\\\\global\_padding\_edge\_cases.json"
greenflame.exe --input "D:\\shots\\issue.png" --overwrite --annotate ".\\\\note.json"
greenflame.exe --input "D:\\shots\\issue.jpg" --output "D:\\shots\\issue-annotated" --annotate ".\\\\note.json"
``` 

**Padding**

* `--padding` accepts one value (`n`), two values (`h,v`), or four values
(`l,t,r,b`).
* Padding is always synthetic color; it never captures extra screen pixels.
* When `--padding` is present, any part of the requested capture area that lies
outside the virtual desktop is filled with the resolved padding color instead
of being clipped away.
* Padding color resolution order is:

  1. `--padding-color`, if provided
  2. `save.padding\_color` from config
  3. default black (`#000000`)

**Annotations**

* `--annotate` applies JSON-defined annotations to the saved CLI render result, using either inline JSON or a UTF-8 JSON file.
* `--input` is valid only with `--annotate`.
* `--input` requires either `--output` or `--overwrite`.
* `--input --overwrite` without `--output` writes back to the input path.
* `--input` is incompatible with live capture modes and with `--window-capture`.
* `--input` is also incompatible with `--cursor` and `--no-cursor`.
* Imported images support only local coordinates. `coordinate\_space: "global"` fails with exit code `14`.
* Imported images must decode fully opaque in V1. Any non-opaque alpha fails with exit code `16`.
* See [docs/cli\_annotations.md](docs/cli_annotations.md) for the full format, schema/examples, coordinate rules, and validation behavior.

**Captured cursor**

* Live CLI captures use `capture.include\_cursor` from config by default.
* `--cursor` and `--no-cursor` override that setting for one invocation only.
* These overrides do not modify the saved config file.

**CLI window capture backends**

* `--window-capture` is CLI-only and applies only to `--window` and `--window-hwnd`.
* `auto` prefers Windows Graphics Capture (WGC) and falls back to GDI if WGC backend setup fails for that window.
* Forced `wgc` does not fall back; backend failures exit with code `15`.
* See [docs/cli\_window\_capture.md](docs/cli_window_capture.md) for backend semantics, warning behavior, and examples.

**Window matching**

* `--window <name>` still starts with a case-insensitive substring search.
* If that search finds exactly one case-insensitive exact-title match, Greenflame
captures that exact-title window automatically.
* If multiple windows still remain ambiguous, the error output lists each candidate
with its `hwnd`, window class, and rect so you can rerun with `--window-hwnd`.

**Output format**

1. If `--output` has a supported extension (`.png`, `.jpg`, `.jpeg`, `.bmp`), that extension defines the format.
2. Otherwise, if `--format` is provided, `--format` defines the format.
3. Otherwise, with `--input`, an extensionless explicit `--output` preserves the probed input-image format.
4. Otherwise, `save.default\_save\_format` (from the config) defines the format.
5. If `--output` extension conflicts with `--format`, the command fails.
6. If `--output` has an unsupported extension (for example `.tiff`), the command fails.
7. If `--output` has no extension, Greenflame appends one based on the resolved format.
8. If `--input --overwrite` writes back to the input path, any explicit `--format` must match the input image format.

### Exit codes

Greenflame uses these process exit codes for command-line invocations. Non-zero
codes are unique and not reused.

|Code|Meaning|
|-|-|
|`0`|Success (includes `--help`, `--version`, and "already running" tray startup)|
|`1`|Failed to register application window classes|
|`2`|CLI argument parse/validation failed|
|`3`|Failed to create tray window|
|`4`|Failed to enforce single-instance tray mode|
|`5`|`--region` capture requested but region data is missing|
|`6`|`--window` matched no visible window|
|`7`|`--window` matched multiple windows (ambiguous)|
|`8`|`--monitor` capture requested but no monitors are available|
|`9`|`--monitor` id is out of range|
|`10`|Output path resolution/reservation failed|
|`11`|Capture/save operation failed|
|`12`|Matched window became unavailable before capture|
|`13`|Matched window is minimized|
|`14`|`--annotate` input is invalid (file read, JSON, validation, or missing explicit font family)|
|`15`|Forced `--window-capture wgc` failed (unsupported, setup/frame failure, or WGC/window size mismatch)|
|`16`|`--input` image is unreadable or unsupported (decode failure, unsupported image format, or transparency rejection)|
|`17`|`--window` or `--window-hwnd` matched a window with `WDA\_EXCLUDEFROMCAPTURE` display affinity; it cannot be captured|
|`18`|CLI obfuscate usage was rejected because `tools.obfuscate.risk\_acknowledged` is not yet `true`|

\---

## Configuration

Greenflame reads `\~/.config/greenflame/greenflame.json` (i.e. `%USERPROFILE%\\.config\\greenflame\\greenflame.json`).

### Capture settings (`capture.\*`)

|Key|Default|Meaning|
|-|-|-|
|`capture.include\_cursor`|`false`|Include the captured cursor by default for live captures. Interactive overlay captures can still toggle it per capture with **Ctrl+K** or the toolbar button.|

### UI settings (`ui.\*`)

|Key|Default|Meaning|
|-|-|-|
|`ui.show\_balloons`|`true`|Show tray toast notifications after copy/save actions.|
|`ui.show\_selection\_size\_side\_labels`|`true`|Show selection-size labels outside the selection (width on top/bottom and height on left/right).|
|`ui.show\_selection\_size\_center\_label`|`true`|Show centered `W x H` selection-size label inside the selection.|
|`ui.tool\_size\_overlay\_duration\_ms`|`800`|How long the centered tool-size overlay stays visible after a stroke-width change. `0` disables it.|

### Tool settings (`tools.\*`)

|Key|Default|Meaning|
|-|-|-|
|`tools.brush.size`|`2`|Brush tool size step (1–50).|
|`tools.brush.smoothing\_mode`|`smooth`|Freehand Brush smoothing mode. Accepted values: `off`, `smooth`. `smooth` affects both committed strokes and the live split-tail preview.|
|`tools.line.size`|`2`|Line tool size step (1–50).|
|`tools.arrow.size`|`2`|Arrow tool size step (1–50).|
|`tools.rect.size`|`2`|Rectangle tool size step (1–50).|
|`tools.ellipse.size`|`2`|Ellipse tool size step (1–50).|
|`tools.colors`|Object with slot index keys (e.g. `{"4": "#ff00ff"}`)|Annotation selection wheel slots (indices 0–7). Only non-default slots are written. Values use `#rrggbb`.|
|`tools.current\_color`|`0`|Current annotation color slot index, clamped to `0..7`.|
|`tools.font.sans`|`Arial`|Font family for the `sans` slot (shared by Text and Bubble tools).|
|`tools.font.serif`|`Times New Roman`|Font family for the `serif` slot.|
|`tools.font.mono`|`Courier New`|Font family for the `mono` slot.|
|`tools.font.art`|`Comic Sans MS`|Font family for the `art` slot.|
|`tools.highlighter.size`|`10`|Highlighter size step (1–50).|
|`tools.highlighter.colors`|Object with slot index keys (e.g. `{"2": "#ffb44d"}`)|Highlighter selection wheel slots (indices 0–5). Only non-default slots are written. Values use `#rrggbb`.|
|`tools.highlighter.current\_color`|`0`|Current Highlighter color slot index, clamped to `0..5`.|
|`tools.highlighter.opacity\_percent`|`35`|Default Highlighter opacity for live preview, save output, and clipboard output. Values are clamped to `0..100`.|
|`tools.highlighter.smoothing\_mode`|`smooth`|Freehand Highlighter smoothing mode. Accepted values: `off`, `smooth`. Straightened highlighter bars bypass this setting and stay explicit `start`/`end` segments.|
|`tools.highlighter.pause\_straighten\_ms`|`800`|After the mouse is still for this many milliseconds during a highlighter stroke, the stroke snaps to a straight bar (start to cursor). `0` means always straight.|
|`tools.highlighter.pause\_straighten\_deadzone\_px`|`0`|Mouse must move more than this many physical pixels from the last timer-reset position before the pause timer resets. `0` means any movement resets the timer.|
|`tools.text.size`|`10`|Text tool size step (1–50).|
|`tools.text.current\_font`|`sans`|Active font slot for the Text tool. Accepted values: `sans`, `serif`, `mono`, `art`.|
|`tools.bubble.size`|`10`|Bubble size step (1–50).|
|`tools.bubble.current\_font`|`sans`|Active font slot for the Bubble tool. Accepted values: `sans`, `serif`, `mono`, `art`.|
|`tools.obfuscate.block\_size`|`10`|Obfuscate tool block size (1–50). `1` uses blur mode; `2..50` uses block pixelation.|
|`tools.obfuscate.risk\_acknowledged`|`false`|Whether the Obfuscate warning has been explicitly accepted. CLI obfuscate usage requires this to be `true`.|

### Save settings (`save.\*`)

|Key|Default|Meaning|
|-|-|-|
|`save.default\_save\_dir`|`%USERPROFILE%\\Pictures\\greenflame` (runtime fallback when unset)|Folder used by **Ctrl-S**, **Ctrl-Alt-S**, and CLI captures when `--output` is not provided.|
|`save.last\_save\_as\_dir`|Falls back to `default\_save\_dir`, then `%USERPROFILE%\\Pictures\\greenflame`|Initial folder used by **Ctrl-Shift-S** and **Ctrl-Shift-Alt-S** (Save As).|
|`save.default\_save\_format`|`png`|Default image format for **Ctrl-S**, **Ctrl-Alt-S**, and CLI output paths without explicit extension. Accepted values: `png`, `jpg`, `bmp`.|
|`save.padding\_color`|`#000000`|Padding color used by CLI captures when `--padding` is present and `--padding-color` is not supplied. Values use `#rrggbb`.|
|`save.filename\_pattern\_region`|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}`|Default filename pattern for region captures.|
|`save.filename\_pattern\_desktop`|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}`|Default filename pattern for desktop captures.|
|`save.filename\_pattern\_monitor`|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}-monitor${monitor}`|Default filename pattern for monitor captures.|
|`save.filename\_pattern\_window`|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}-${title}`|Default filename pattern for window captures.|

### Example

```json
{
  "capture": {
    "include\_cursor": true
  },
  "ui": {
    "show\_balloons": true,
    "show\_selection\_size\_side\_labels": true,
    "show\_selection\_size\_center\_label": true,
    "tool\_size\_overlay\_duration\_ms": 800
  },
  "tools": {
    "font": {
      "sans": "Arial",
      "serif": "Times New Roman",
      "mono": "Courier New",
      "art": "Comic Sans MS"
    },
    "colors": { "4": "#ff00ff" },
    "current\_color": 0,
    "brush": {
      "size": 2,
      "smoothing\_mode": "off"
    },
    "highlighter": {
      "colors": { "2": "#ffb44d" },
      "current\_color": 0,
      "opacity\_percent": 35,
      "smoothing\_mode": "off",
      "pause\_straighten\_ms": 800,
      "pause\_straighten\_deadzone\_px": 0
    },
    "obfuscate": {
      "block\_size": 10,
      "risk\_acknowledged": true
    },
    "text": {
      "size": 14,
      "current\_font": "sans"
    },
    "bubble": {
      "current\_font": "sans"
    }
  },
  "save": {
    "default\_save\_dir": "C:\\\\Users\\\\you\\\\Pictures\\\\greenflame",
    "last\_save\_as\_dir": "D:\\\\shots\\\\scratch",
    "default\_save\_format": "png",
    "padding\_color": "#000000",
    "filename\_pattern\_region": "screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}",
    "filename\_pattern\_desktop": "screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}",
    "filename\_pattern\_monitor": "screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}-monitor${monitor}",
    "filename\_pattern\_window": "screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}-${title}"
  }
}
```

### Save filenames

Saved files use one pattern per capture type and Greenshot-style `${VARIABLE}` placeholders.

#### Supported variables

|Variable|Expansion|Example|
|-|-|-|
|`${YYYY}`|4-digit year|`2026`|
|`${YY}`|2-digit year|`26`|
|`${MM}`|2-digit month|`02`|
|`${DD}`|2-digit day|`21`|
|`${hh}`|2-digit hour (24h)|`14`|
|`${mm}`|2-digit minute|`30`|
|`${ss}`|2-digit second|`25`|
|`${title}`|Sanitized window title (spaces and invalid filename chars become `\_`; max 50 chars; falls back to `window`)|`My\_App`|
|`${monitor}`|1-based monitor number|`2`|
|`${num}`|Incrementing counter (6-digit, zero-padded, next available by directory scan)|`000042`|

#### Default patterns

|Capture type|Default pattern|Example output|
|-|-|-|
|Region|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}`|`screenshot-2026-02-21\_143025`|
|Desktop|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}`|`screenshot-2026-02-21\_143025`|
|Monitor|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}-monitor${monitor}`|`screenshot-2026-02-21\_143025-monitor2`|
|Window|`screenshot-${YYYY}-${MM}-${DD}\_${hh}${mm}${ss}-${title}`|`screenshot-2026-02-21\_143025-My\_App`|

\---

## Build and test

* Build instructions: [docs/build.md](docs/build.md)
* Test instructions: [docs/testing.md](docs/testing.md)

\---

[**MIT**](LICENSE) **License**
