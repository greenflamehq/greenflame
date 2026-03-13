---
title: Human Test Plan
summary: Manual validation plan for Win32, rendering, DPI, tray, and other end-to-end behaviors that automated tests cannot cover reliably.
audience:
  - contributors
  - qa
status: reference
owners:
  - core-team
last_updated: 2026-03-10
tags:
  - testing
  - manual
  - windows
  - dpi
---

# Human Test Plan

This plan covers behaviors that require real Windows UI, monitor, clipboard, filesystem,
registry, or shell integration. It is intentionally focused on manual-only coverage.

These areas are already covered by automated tests and should not be re-expanded here
unless a real end-to-end bug escapes into the Win32 shell:

- geometry and rect math
- DPI conversion math and monitor rules
- selection-handle calculations and snap logic policy
- annotation controller behavior and undo-stack rules
- output-path resolution and filename-pattern logic
- CLI parsing and most validation logic
- config normalization and string utility behavior

## How to use this plan

- Run the required build/test gates first: [build.md](build.md) and [testing.md](testing.md).
- Record failures by case ID only, for example `GF-MAN-OUT-003`.
- For routine changes, run all `P0` cases plus the `P1` cases in touched areas.
- For release validation, run every applicable case.

## Priority Levels

- `P0`: always run
- `P1`: standard functional coverage
- `P2`: edge case, environment-specific, or expensive

## Test Environments

| Label | Setup |
| --- | --- |
| `ENV-A` | Single monitor at 100% scale on Windows 11. |
| `ENV-B` | At least two monitors with mixed DPI, for example 100% + 150%, with one monitor placed left of or above the primary so the virtual desktop includes negative coordinates. |
| `ENV-C` | Disposable login session, VM snapshot, or other safe environment for startup-at-login and hotkey-conflict checks. |

## Common Setup

- Use a scratch output folder, for example `C:\Temp\greenflame-manual`.
- Keep Paint or another simple image editor open to verify clipboard image output.
- Keep File Explorer open to verify saved files and file-drop clipboard behavior.
- Use one visible Notepad window for single-window capture tests.
- Use two visible Notepad windows at once for ambiguous `--window` CLI tests.
- Start from a known config file at `%USERPROFILE%\.config\greenflame\greenflame.ini`.

## Smoke And Tray

### GF-MAN-SMOKE-001 - Launch To Tray

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Launch `greenflame.exe`.
  2. Open the hidden-icons tray area if needed.
- Expected:
  - Exactly one Greenflame tray icon appears.
  - No main window is left open on the desktop.
  - The process stays resident and idle.

### GF-MAN-SMOKE-002 - Single-Instance Tray Startup

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Launch `greenflame.exe`.
  2. Launch `greenflame.exe` again from a terminal.
- Expected:
  - A second tray icon is not created.
  - The second process exits quickly with exit code `0`.
  - The original tray instance remains responsive.

### GF-MAN-TRAY-001 - Tray Click Paths And Menu Contents

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Left-click the tray icon.
  2. Cancel the overlay.
  3. Right-click the tray icon.
  4. Dismiss the menu without choosing an action.
- Expected:
  - Left-click starts interactive capture.
  - The right-click menu includes region, monitor, window, desktop, last-region, last-window, start-with-Windows, About, and Exit actions.
  - Dismissing the menu does not trigger a capture.

### GF-MAN-TRAY-002 - Tray Current-Window Capture Does Not Capture Tray UI

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Bring a Notepad window to the foreground.
  2. Right-click the tray icon.
  3. Choose `Capture current window`.
  4. Paste into Paint.
- Expected:
  - The pasted image shows the target window.
  - The tray menu or notification overflow UI is not captured.
  - A success toast appears if `show_balloons=true`.

### GF-MAN-TRAY-003 - About Dialog Placement And Close Paths

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Move the cursor to the monitor where the dialog should appear.
  2. Right-click the tray icon and choose `About Greenflame...`.
  3. Close it once with `OK` and once with `Esc`.
- Expected:
  - The dialog opens on the monitor containing the cursor.
  - The dialog shows the app icon.
  - Both close paths work cleanly.

### GF-MAN-TRAY-004 - Start With Windows Toggle

- Priority: `P2`
- Run on: `ENV-C`
- Steps:
  1. Enable `Start with Windows` from the tray menu.
  2. Verify `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Greenflame` exists.
  3. Exit and relaunch Greenflame.
  4. Disable `Start with Windows` and verify the value is removed.
  5. For a release pass, sign out and back in with the option enabled.
- Expected:
  - The registry value contains a quoted executable path.
  - The menu checkmark matches the registry state.
  - On sign-in, Greenflame starts once and only once.

### GF-MAN-TRAY-005 - Exit From Tray

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Right-click the tray icon.
  2. Choose `Exit`.
- Expected:
  - The tray icon disappears.
  - The process exits.
  - No overlay window or toast is left behind.

## Global Hotkeys

### GF-MAN-HK-001 - Print Screen Starts Interactive Capture

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Press `Prt Scrn`.
  2. Cancel the overlay with `Esc`.
- Expected:
  - The full-screen capture overlay opens immediately.
  - Cancel returns to the idle tray state.

### GF-MAN-HK-002 - One-Shot Clipboard Hotkeys

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Bring a known window to the foreground and press `Ctrl + Prt Scrn`.
  2. Paste into Paint.
  3. Press `Shift + Prt Scrn` and paste again.
  4. Press `Ctrl + Shift + Prt Scrn` and paste again.
- Expected:
  - `Ctrl + Prt Scrn` copies the current foreground window.
  - `Shift + Prt Scrn` copies the current monitor.
  - `Ctrl + Shift + Prt Scrn` copies the full virtual desktop.
  - Each action works without showing the interactive overlay.

### GF-MAN-HK-003 - Last Region Recapture

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Press `Alt + Prt Scrn` before any prior capture exists.
  2. Perform an interactive region capture and copy or save it.
  3. Change the pixels inside that same on-screen area.
  4. Press `Alt + Prt Scrn` again and paste the result into Paint.
- Expected:
  - Step 1 shows a warning toast that no previous region exists.
  - The recapture uses the same screen coordinates as the previous region.
  - The recapture contains the latest pixels from those coordinates.

### GF-MAN-HK-004 - Last Window Recapture

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Press `Ctrl + Alt + Prt Scrn` before any previous window capture exists.
  2. Capture the current window once.
  3. Move that window to a new position and press `Ctrl + Alt + Prt Scrn`.
  4. Minimize or close the same window and press `Ctrl + Alt + Prt Scrn` again.
- Expected:
  - Step 1 shows a warning toast that no previous window exists.
  - After a successful window capture, the recapture follows the window to its new location.
  - When the window is minimized or closed, a warning toast is shown instead of a bad capture.

### GF-MAN-HK-005 - Hotkey Registration Conflict Handling

- Priority: `P2`
- Run on: `ENV-C`
- Steps:
  1. Reserve `Prt Scrn` in another application if possible.
  2. Launch Greenflame.
  3. If available, also create a conflict for one modified `Prt Scrn` shortcut.
- Expected:
  - Greenflame shows a warning dialog when registration fails.
  - Tray-menu capture actions still work.
  - Conflicted hotkeys fail gracefully without breaking unrelated actions.

## Selection And Overlay UI

### GF-MAN-SEL-001 - Basic Region Selection

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Start interactive capture.
  2. Drag out a region.
- Expected:
  - The desktop is dimmed outside the selection.
  - A border and resize handles appear.
  - Selection-size labels appear if enabled in config.

### GF-MAN-SEL-002 - Resize And Move Selection

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Create a region.
  2. Drag a corner handle.
  3. Drag a side handle.
  4. With no annotation tool active, drag inside the selection.
- Expected:
  - Corner and side drags resize in the expected directions.
  - Dragging inside the selection moves the selection.
  - Resize drags show the centered in-selection size label when that label is enabled.
  - Move drags do not show the centered in-selection size label.
  - Cursor shapes match the hovered handle direction.

### GF-MAN-SEL-003 - Quick-Select Gestures

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Start interactive capture with no selection yet.
  2. Hold `Ctrl` over a window before clicking, then `Ctrl`-click it.
  3. Start again, hold `Shift` over a monitor before clicking, then `Shift`-click it.
  4. Start again, hold `Ctrl + Shift` before clicking, then click anywhere.
- Expected:
  - While the modifier is held before click, the pending window, monitor, or desktop target is shown as a clear undimmed preview region inside the gray overlay.
  - `Ctrl`-click selects the window under the cursor.
  - `Shift`-click selects the monitor under the cursor.
  - `Ctrl + Shift` selects the full virtual desktop.

### GF-MAN-SEL-004 - Snapping And Alt Override

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Create or move a selection near obvious monitor or window edges.
  2. Repeat while holding `Alt`.
- Expected:
  - Without `Alt`, the selection snaps when it gets close enough to eligible edges.
  - With `Alt`, the same drag does not snap.

### GF-MAN-SEL-005 - Escape Behavior Priority

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Open the overlay and press `Esc` before making a selection.
  2. Create a selection and open the color wheel, then press `Esc`.
  3. Open the help overlay and press `Esc`.
  4. With a normal stable selection and no sub-UI open, press `Esc`.
- Expected:
  - `Esc` closes the innermost active UI first.
  - Color wheel closes before the overlay cancels.
  - Help closes before the overlay cancels.
  - With no nested UI open, the overlay cancels or backs out cleanly.

### GF-MAN-UI-001 - Overlay Help

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a stable selection.
  2. Press `Ctrl + H`.
  3. Try clicking, dragging, typing tool hotkeys, and using the mouse wheel.
  4. Close the help overlay with `Ctrl + H`, then reopen and close with `Esc`.
- Expected:
  - A help overlay opens near the cursor.
  - It shows the current shortcut reference.
  - While visible, other overlay interactions are blocked.
  - Both close paths work.

### GF-MAN-UI-002 - Toolbar Placement And Tooltips

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Create selections near each screen edge and corner.
  2. Hover each toolbar button.
  3. Toggle a few tools on and off.
- Expected:
  - The toolbar stays attached to the selection and remains visible on-screen.
  - Hovering shows the full tool name.
  - Button state tracks the active tool correctly.

## Annotations

### GF-MAN-ANN-001 - Tool Toggling

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection.
  2. Toggle `B`, `H`, `L`, `A`, `R`, and `F` from the keyboard.
  3. Toggle the same tools from the toolbar.
  4. Activate the same tool twice in a row.
- Expected:
  - Each trigger selects the expected tool.
  - Activating the same tool a second time returns to default mode.
  - Keyboard and toolbar stay in sync.

### GF-MAN-ANN-002 - Brush And Highlighter Annotation

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate the Brush tool and move the cursor without drawing.
  2. Draw a freehand brush stroke.
  3. Activate the Highlighter tool and move the cursor without drawing.
  4. Draw a highlighter stroke across visible text or another detailed background.
  5. Return to default mode and click each stroke.
  6. Drag each selected stroke.
- Expected:
  - The Brush tool shows a circular size preview.
  - The Highlighter tool shows an axis-aligned square size preview.
  - Both committed strokes render cleanly.
  - The Highlighter stroke remains semi-transparent and the underlying content stays visible.
  - Selecting either stroke shows L-bracket corner markers hugging the tight bounding box of the stroke geometry.
  - Both strokes can be moved in default mode.

### GF-MAN-ANN-003 - Line And Arrow Editing

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Draw one line and one arrow.
  2. Return to default mode and select each one.
  3. Drag the body of each shape.
  4. Drag each endpoint handle.
- Expected:
  - Line and arrow drawing show a direction-aligned square size preview.
  - Selected line and arrow annotations show endpoint handles and L-bracket corner markers around the tight bounding box of the drawn geometry (not including the endpoint handles themselves).
  - Dragging the body moves the whole annotation.
  - Dragging an endpoint reshapes the annotation.

### GF-MAN-ANN-004 - Rectangle And Filled Rectangle Editing

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Draw one outlined rectangle and one filled rectangle.
  2. Move each one in default mode.
  3. Resize the outlined rectangle from a corner and then a side handle.
- Expected:
  - Neither rectangle mode shows a cursor size preview.
  - Selected rectangles show resize handles when space permits but no L-bracket corner markers.
  - Filled rectangles render as filled shapes and remain movable.
  - Resizing follows the dragged handle correctly.

### GF-MAN-ANN-005 - Overlap And Topmost Selection

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create two overlapping annotations of different types.
  2. Click the overlapping pixels.
  3. Move the selected top annotation away and click again.
- Expected:
  - The topmost annotation selects first.
  - After it moves away, the underlying annotation becomes selectable.

### GF-MAN-ANN-006 - Delete, Undo, And Redo

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Create a region and several annotations.
  2. Select one annotation and press `Delete`.
  3. Use `Ctrl + Z` repeatedly.
  4. Use `Ctrl + Shift + Z` repeatedly.
- Expected:
  - Delete removes only the selected annotation.
  - Undo walks backward through both region and annotation history.
  - Redo restores the undone changes in order.

### GF-MAN-ANN-007 - Color Wheel Selection And Cancel

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate any annotation tool.
  2. Right-click to open the color wheel.
  3. Hover different segments.
  4. Pick a segment once, then reopen and dismiss with `Esc`.
  5. Repeat with the Highlighter tool active.
- Expected:
  - The wheel opens centered on the cursor.
  - Hovering highlights the segment under the pointer.
  - Selecting a segment changes the color used by future annotations.
  - Brush, Line, Arrow, Rectangle, and Filled rectangle show the 8-slot annotation palette.
  - Highlighter shows the 6-slot highlighter palette.
  - `Esc` closes the wheel without changing the current color.

### GF-MAN-ANN-008 - Stroke Width Adjustment And Clamp

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate Brush, Highlighter, Line, Arrow, and Rectangle in turn.
  2. Adjust width with mouse wheel and with `Ctrl + =` and `Ctrl + -`.
  3. Attempt to go below the minimum and above the maximum.
  4. Repeat once with Filled Rectangle active.
- Expected:
  - Width changes affect Brush, Highlighter, Line, Arrow, and outlined Rectangle.
  - The value clamps at `1..50`.
  - A centered size overlay appears when enabled.
  - Filled Rectangle rendering does not depend on stroke width.

### GF-MAN-ANN-009 - Undo Then Switch Freehand Tool Preview

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and draw a Highlighter stroke.
  2. Press `Ctrl + Z` to undo it.
  3. Activate the Brush tool and start drawing a new stroke.
  4. Repeat the same sequence in reverse: draw with Brush, undo, then start a Highlighter stroke.
- Expected:
  - The undone stroke disappears immediately after undo.
  - Starting the next freehand stroke does not resurrect pixels from the undone stroke.
  - The live preview only shows the stroke currently being drawn.

### GF-MAN-ANN-010 - Highlighter Reversal Preview Matches Final Stroke

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and activate the Highlighter tool.
  2. Draw quickly in one direction, then reverse sharply and continue the stroke.
  3. Release the mouse and select the finished annotation.
- Expected:
  - The live preview does not leave stray blocks or extra segments from an earlier direction of travel.
  - The committed stroke matches what was shown during the live preview.
  - The selected-annotation brackets enclose all visible highlighter pixels.

## Output, Clipboard, And Notifications

### GF-MAN-OUT-001 - Copy Selection To Clipboard

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and add at least one annotation, including a highlighter
     stroke over visible text or another detailed background.
  2. Press `Ctrl + C`.
  3. Paste into Paint.
- Expected:
  - The overlay closes after copy.
  - The pasted image contains the selected crop plus committed annotations.
  - Highlighter annotations remain semi-transparent in the pasted image.
  - Overlay chrome, labels, toolbar, help, and color wheel are absent from the pasted image.

### GF-MAN-OUT-002 - Direct Save

- Priority: `P0`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Set `default_save_dir` to a scratch folder that does not already exist.
  2. Create a selection and press `Ctrl + S`.
  3. Click the file path in the success toast.
- Expected:
  - The default save directory is created automatically.
  - A file is saved using the configured default format.
  - The overlay closes after save.
  - The success toast shows the saved filename and a thumbnail.
  - Clicking the toast path opens Explorer with the file selected.
  - On `ENV-B`, the toast appears on the monitor containing the pointer.

### GF-MAN-OUT-003 - Save As

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and press `Ctrl + Shift + S`.
  2. Cancel the dialog once.
  3. Save as `PNG`, `JPEG`, and `BMP` across repeated runs.
  4. Save once to a non-default folder, then reopen Save As.
- Expected:
  - Cancel leaves the overlay open.
  - Each supported format saves successfully and opens correctly in Windows.
  - The next Save As session starts in the last folder used for Save As.

### GF-MAN-OUT-004 - Save-And-Copy-File Variants

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Use `Ctrl + Alt + S`.
  2. Paste into an empty Explorer folder or onto the desktop.
  3. Repeat with `Ctrl + Shift + Alt + S`.
- Expected:
  - The file is saved successfully.
  - The clipboard contains a file-drop copy of the saved file, not image pixels.
  - The success message mentions that the file was copied to the clipboard.

### GF-MAN-OUT-005 - Output Cropping And Annotation Intersection

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Create a selection.
  2. Draw annotations that extend both inside and outside the selected bounds.
  3. Save or copy the result.
- Expected:
  - Only the pixels inside the final selection appear in the output.
  - Annotation pixels intersecting the selection are included.
  - Annotation pixels outside the selection are excluded.
  - Overlay UI chrome is never present in the output.

## Config And Persistence

### GF-MAN-CFG-001 - Success Toast Suppression

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Set `[ui] show_balloons=false`.
  2. Relaunch Greenflame.
  3. Perform a successful save or copy.
  4. Trigger a warning condition such as last-region recapture with no previous capture.
- Expected:
  - Success toasts are suppressed.
  - Warning toasts still appear.

### GF-MAN-CFG-002 - Selection Label Visibility Toggles

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Disable `show_selection_size_side_labels` and relaunch.
  2. Make a selection.
  3. Re-enable side labels, disable `show_selection_size_center_label`, and relaunch.
  4. Make a selection again.
- Expected:
  - Side labels disappear only when their setting is disabled.
  - The center label disappears only when its setting is disabled.
  - Selection behavior itself is unchanged.

### GF-MAN-CFG-003 - Tool Size Overlay Duration

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Set `tool_size_overlay_duration_ms=0`.
  2. Adjust annotation width during capture.
  3. Set `tool_size_overlay_duration_ms=1200`.
  4. Adjust width again.
- Expected:
  - With `0`, the centered tool-size overlay does not appear.
  - With a non-zero value, the overlay appears and remains visible for roughly the configured duration.

### GF-MAN-CFG-004 - Tool Color And Width Persistence

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Change the brush color slot, the highlighter color slot, and the shared stroke
     width.
  2. Close the overlay and exit the app.
  3. Relaunch and start a fresh capture.
  4. Open the brush color wheel and then the highlighter color wheel.
- Expected:
  - The previously chosen brush color slot is restored.
  - The previously chosen highlighter color slot is restored.
  - The saved stroke width is restored.
  - Custom brush and highlighter palette colors appear in their configured wheel
    slots.

## Mixed-DPI And Multi-Monitor

### GF-MAN-DPI-001 - Mixed-DPI Pointer Alignment

- Priority: `P0`
- Run on: `ENV-B`
- Steps:
  1. Start interactive capture on each monitor.
  2. Move the cursor slowly across the overlay.
  3. Create, move, and resize selections.
  4. Draw annotations and hover resize handles.
- Expected:
  - Crosshair, handles, toolbar hit-testing, cursor previews, and color wheel stay under the pointer.
  - No visual offset appears when crossing monitors with different DPI.

### GF-MAN-DPI-002 - Cross-Monitor Selection And Save

- Priority: `P1`
- Run on: `ENV-B`
- Steps:
  1. Create a selection spanning a monitor boundary.
  2. Ensure part of the selection sits on a monitor with negative virtual coordinates.
  3. Save or copy the result.
- Expected:
  - The saved image dimensions match the visible selection.
  - No seam, jump, or clipping appears at the monitor boundary.
  - Annotations and selection handles remain aligned across the full selection.

### GF-MAN-DPI-003 - Non-Primary Monitor Quick Select And Hotkeys

- Priority: `P1`
- Run on: `ENV-B`
- Steps:
  1. Place a test window on the non-primary monitor.
  2. Use `Ctrl`-click and `Shift`-click quick-select on that monitor.
  3. Use `Ctrl + Prt Scrn` and `Shift + Prt Scrn` while the target is on that monitor.
- Expected:
  - Window and monitor selection target the correct non-primary monitor content.
  - Clipboard output matches the actual target, not the primary monitor.

## CLI End-To-End

### GF-MAN-CLI-001 - Help And Version

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Run `greenflame.exe --help`.
  2. Run `greenflame.exe --version`.
- Expected:
  - Each command prints the expected text and exits with code `0`.
  - No tray icon remains running after either command.

### GF-MAN-CLI-002 - Region, Monitor, And Desktop Capture Happy Path

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Run `greenflame.exe --region <x,y,w,h> --output <file>`.
  2. On multi-monitor, run `greenflame.exe --monitor <id> --output <file>`.
  3. Run `greenflame.exe --desktop --output <file>`.
- Expected:
  - Each command exits successfully.
  - Each file is created at the requested path.
  - The output images visually match the requested region, monitor, or full virtual desktop.

### GF-MAN-CLI-003 - Window Capture Success And Common Errors

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Run `greenflame.exe --window "<unique-title>" --output <file>` against one visible window.
  2. Open two visible Notepad windows with matching titles and run the command again.
  3. Minimize the target window and run the command again.
- Expected:
  - The single-match case succeeds and saves the target window.
  - The ambiguous case exits with code `7` and lists matching candidates.
  - The minimized case exits with code `13`.

### GF-MAN-CLI-004 - Window Obscuration And Off-Screen Warnings

- Priority: `P2`
- Run on: `ENV-B`
- Steps:
  1. Partially cover a target window with another visible window.
  2. Move part of the target window outside visible desktop bounds.
  3. Run `greenflame.exe --window "<title>" --output <file>`.
- Expected:
  - The command still saves an image when a capturable area exists.
  - `stderr` includes the appropriate warning text about obscuration and off-screen clipping when applicable.
  - The saved output reflects the actual visible capture limitations.

### GF-MAN-CLI-005 - Explicit Output Overwrite And Format Resolution

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create an output file in advance.
  2. Run a CLI capture with the same `--output` path and no `--overwrite`.
  3. Repeat with `--overwrite`.
  4. Run once with an extensionless `--output` path and `--format jpg`.
- Expected:
  - Existing explicit outputs are rejected without `--overwrite`.
  - The overwrite case succeeds and replaces the file.
  - The extensionless path resolves to a `.jpg` file.
  - The command exits without starting a tray instance.
