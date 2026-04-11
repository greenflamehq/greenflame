# Greenflame Capture Workflows

Use this document to choose capture modes, choose window-capture backends, and recover from CLI window-targeting failures.

## Execution Model

Use the Greenflame CLI only.

- prefer explicit `--output` paths
- use `--overwrite` only when replacing an existing target is intended
- prefer file-based annotation JSON over inline JSON
- use `--window-hwnd` after ambiguity recovery instead of repeating a fuzzy `--window` title match

## Choosing A Capture Source

### `--region`

Use when physical-pixel bounds are already known.

```bat
greenflame.exe --region 1200,100,800,600 --output "D:\shots\region.png" --overwrite
```

`--region` captures the desktop pixels currently visible inside that rectangle. It is not a drop-in replacement for `--window` or `--window-hwnd`, even if the rectangle happens to match a window's bounds.

Do not substitute `--region` for window capture when the task is about a specific window. If the target window is covered, off-desktop, hidden by another window, or otherwise different from the current visible desktop pixels in that area, `--region` will not capture the same thing that a window-targeted capture is intended to capture.

### `--window`

Use when the user identifies a window by title but no exact `HWND` is known yet.

```bat
greenflame.exe --window "Notepad" --output "D:\shots\note.png" --overwrite
```

Greenflame already prefers a unique case-insensitive exact title match over broader substring matches.

Choose `--window` when the user means "that window" rather than "whatever pixels are currently visible in this rectangle."

### `--window-hwnd`

Use after ambiguity recovery or when the exact handle is already known.

```bat
greenflame.exe --window-hwnd 0x0000000000123456 --output "D:\shots\exact-window.png" --overwrite
```

Use `--window-hwnd` instead of `--region` when the handle is known and the task is to capture that exact window, not the current desktop pixels in a matching rectangle.

### `--monitor`

Use when the user wants a specific monitor by id.

```bat
greenflame.exe --monitor 2 --output "D:\shots\monitor2.png" --overwrite
```

### `--desktop`

Use for the full virtual desktop, especially multi-monitor context.

```bat
greenflame.exe --desktop --output "D:\shots\desktop.png" --overwrite
```

### `--input`

Use to annotate or re-render an existing PNG, JPEG, or BMP without recapturing the screen.

```bat
greenflame.exe --input "D:\shots\issue.png" --output "D:\shots\issue-annotated.png" --annotate ".\note.json" --overwrite
```

Rules:

- `--input` requires `--annotate`
- `--input` requires either `--output` or `--overwrite`
- `--input` supports only local coordinates
- `--input` rejects images with non-opaque alpha in v1
- `--input` cannot be combined with live capture modes, `--window-capture`, `--cursor`, or `--no-cursor`

## Capture, Analyze, Then Annotate

Use this as the default agent workflow for non-trivial markup:

1. capture to a file
2. inspect the image
3. create an annotation JSON file
4. re-run Greenflame with `--input <the original captured file> --annotate ...`

The annotation pass should use the exact screenshot that was analyzed. Do not take a fresh capture for the final annotation pass unless the user explicitly asks for a new screenshot. Otherwise the source may have changed and the markup/report can drift from the analyzed image.

If the image is crowded, add padding during the second pass:

```bat
greenflame.exe --input "D:\shots\issue.png" --output "D:\shots\issue-annotated.png" --padding 96 --padding-color "#202020" --annotate ".\note.json" --overwrite
```

Local coordinates for `--input` still use the original decoded image origin before padding. To place a label in the new margin, use coordinates outside the original image bounds.

## Window-Capture Backends

### `auto`

Use by default.

- tries WGC first
- falls back to GDI if WGC setup or capture fails
- prints an info line on fallback
- suppresses GDI-only obscuration warnings if WGC succeeds

### `gdi`

Use only when the user explicitly wants visible-desktop semantics or is debugging WGC behavior.

- captures from the visible virtual desktop and crops to the target window rect
- may include covering windows
- warns about obscuration and off-desktop clipping

### `wgc`

Use when the user specifically wants the target window itself even if it is obscured or partly off-desktop, and when failure should be surfaced instead of falling back.

- no fallback to GDI
- suppresses GDI-only obscuration and off-screen warnings
- exits `15` if WGC is unsupported or fails
- still rejects minimized windows

## Ambiguous `--window` Recovery

When `--window` exits `7`, `stderr` includes a candidate list. Candidate lines look like:

```text
  [1] hwnd=0x1111 class="Chrome_WidgetWin_1" title="Codex" (x=10, y=20, w=200, h=200)
  [2] hwnd=0x2222 class="Notepad" title="Codex" (x=30, y=40, w=300, h=200)
```

Candidates may also end with:

```text
[uncapturable]
```

Smart retry policy:

- rerun with `--window-hwnd` only when one candidate clearly matches the user's intent
- acceptable disambiguators: exact app name, exact title fragment, expected class, or expected geometry
- never auto-pick an `[uncapturable]` candidate
- if more than one candidate remains plausible, ask the user which `hwnd` to use

Example recovery:

```bat
greenflame.exe --window-hwnd 0x1111 --output "D:\shots\codex.png" --overwrite
```

## Uncapturable And Minimized Windows

### Exit `17`: uncapturable window

Greenflame uses exit `17` when the chosen window has `WDA_EXCLUDEFROMCAPTURE` display affinity.

- do not silently switch to a different app window
- surface the issue to the user
- if ambiguity output included both capturable and uncapturable candidates, rerun only when one capturable candidate clearly matches the request

### Exit `13`: minimized window

Treat this as a target-state issue, not a title-matching issue.

- ask the user to restore the window if needed
- do not silently choose a different window unless the original request already made another candidate clearly correct

### Minimized matches with `auto` or `wgc`

With WGC-style title handling, Greenflame may emit a warning that additional matching windows were minimized and skipped.

If all matching title hits are minimized, Greenflame reports minimized-window failure instead of "not found". Do not reinterpret that as a bad query.

## Output And Annotation Tips

- prefer PNG for general agent work
- use `--padding` when you need label space
- use `--cursor` or `--no-cursor` only for live capture modes
- after image analysis, prefer `--input` on the original screenshot for the final annotation pass so the output matches the analyzed source exactly
- do not switch from `--window` or `--window-hwnd` to `--region` just because you learned a rect; those modes are not interchangeable

## Relevant Exit Codes

- `6`: no visible window matched
- `7`: window match ambiguous
- `10`: output path resolution or reservation failed
- `11`: capture or save failed
- `12`: matched window became unavailable before capture
- `13`: matched window is minimized
- `15`: forced `wgc` failed
- `17`: matched window is excluded from capture
