---
title: Human Test Plan
summary: Manual validation plan for Win32, rendering, DPI, tray, and other end-to-end behaviors that automated tests cannot cover reliably.
audience:
  - contributors
  - qa
status: reference
owners:
  - core-team
last_updated: 2026-03-22
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
- Start from a known config file at `%USERPROFILE%\.config\greenflame\greenflame.json`.

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
  - The right-click menu includes region, monitor, window, desktop, last-region, last-window, start-with-Windows, `Open config file...`, About, and Exit actions.
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

### GF-MAN-TRAY-006 - Open Config File From Tray

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Exit Greenflame.
  2. Delete `%USERPROFILE%\.config\greenflame\greenflame.json` if it exists.
  3. Launch `greenflame.exe`.
  4. Right-click the tray icon and choose `Open config file...`.
  5. Close the opened editor or shell surface.
  6. Repeat once with the config file already present.
- Expected:
  - Choosing `Open config file...` creates the config file first when it does not yet exist.
  - The opened target is `%USERPROFILE%\.config\greenflame\greenflame.json`.
  - The tray instance stays resident and responsive after the file is opened.
  - Repeating the action with an existing file opens the same file without showing a warning dialog.

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
  - The selection border is visually stable with no marching-ants or marquee-style motion.
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
  4. Activate an annotation tool, start a stroke, and press `Esc` before releasing the mouse.
  5. With a normal stable selection and no sub-UI open, press `Esc`.
- Expected:
  - `Esc` closes the innermost active UI first.
  - Color wheel closes before the overlay cancels.
  - Help closes before the overlay cancels.
  - Pressing `Esc` during an active annotation gesture cancels only the in-progress gesture and leaves the tool armed.
  - With no nested UI open, the overlay cancels or backs out cleanly.

### GF-MAN-UI-001 - Overlay Help

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a stable selection.
  2. Press `Ctrl + H`.
  3. Close the help overlay with `Ctrl + H`, then reopen it with the toolbar `Help`
     button.
  4. Try clicking, dragging, typing tool hotkeys, and using the mouse wheel.
  5. Close the help overlay with `Esc`, reopen it with the toolbar button again, then
     dismiss it with a left-click.
- Expected:
  - A help overlay opens near the cursor.
  - It shows the current shortcut reference.
  - All listed shortcuts remain readable without clipping; when content would overflow vertically, the layout reflows cleanly into two columns.
  - The toolbar `Help` button opens help on button release and does not toggle an
    annotation tool.
  - While visible, other overlay interactions are blocked.
  - Keyboard, `Esc`, and left-click dismissal all work.
  - After dismissing help with `Esc` or a left-click, the toolbar button under the
    cursor becomes hovered and clickable again without requiring mouse movement.

### GF-MAN-UI-002 - Toolbar Placement And Tooltips

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Create selections near each screen edge and corner.
  2. Hover each toolbar button.
  3. Toggle a few tools on and off.
- Expected:
  - The toolbar stays attached to the selection and remains visible on-screen.
  - The `Help` button stays last on the right and is visually separated from the
    annotation tools by one reserved button slot of space.
  - Hovering shows an opaque tooltip for each button.
  - Tool button tooltips include the tool name followed by its hotkey in
    parentheses, such as `Brush (B)`.
  - The help button tooltip includes the help hotkey in parentheses.
  - Hovering any visible toolbar button shows the standard pointer cursor.
  - Button state tracks the active tool correctly.

## Annotations

### GF-MAN-ANN-001 - Tool Toggling

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection.
  2. Toggle `B`, `H`, `L`, `A`, `R`, `F`, `E`, `G`, `T`, and `N` from the keyboard.
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
  2. Press and hold the left mouse button once without moving the cursor.
  3. Draw a freehand brush stroke.
  4. Activate the Highlighter tool and move the cursor without drawing.
  5. Press and hold the left mouse button once without moving the cursor.
  6. Draw a highlighter stroke across visible text or another detailed background.
  7. Start another highlighter stroke, hold the pointer still long enough to trigger
     straighten, then move the cursor and release.
  8. Return to default mode and click each stroke.
  9. Drag each selected stroke.
  10. Select the straightened highlighter stroke and drag each endpoint handle.
- Expected:
  - The Brush tool shows a circular size preview.
  - Pressing the left mouse button with the Brush tool shows an initial circular mark immediately, before any mouse movement.
  - The Highlighter tool shows an axis-aligned square size preview.
  - Pressing the left mouse button with the Highlighter tool shows an initial square mark immediately, before any mouse movement.
  - Both committed strokes render cleanly.
  - The Highlighter stroke remains semi-transparent, darkens underlying content with a marker-like multiply effect, and leaves the underlying detail legible.
  - Pause-to-straighten snaps the in-progress highlighter stroke to a straight bar from the original start point to the live cursor position.
  - Selecting either stroke shows L-bracket corner markers hugging the tight bounding box of the stroke geometry.
  - A straightened highlighter stroke exposes draggable start and end handles in default mode, and dragging either handle reshapes only that endpoint.
  - Both strokes can be moved in default mode.

### GF-MAN-ANN-002A - Bubble Tool Placement, Drag Preview, And Editing

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and activate the Bubble tool.
  2. Move the cursor inside the selection without pressing the mouse.
  3. Press and hold the left mouse button, then drag before releasing.
  4. Release to commit the bubble.
  5. Place a second bubble.
  6. Undo once, then place another bubble.
  7. Return to default mode, select a committed bubble, and drag it.
  8. Right-click with the Bubble tool active and choose a different color and font.
- Expected:
  - The Bubble tool shows a circular size preview while armed.
  - Pressing the left mouse button shows the live bubble immediately on mouse-down.
  - Dragging before release moves the live bubble so the committed bubble lands at the release position.
  - Bubble numbering increments with each committed placement and decrements correctly when the latest bubble is undone and replaced.
  - Committed bubbles can be selected and moved in default mode.
  - The Bubble style wheel shows annotation colors plus font choices, and the next placed bubble uses the chosen style.

### GF-MAN-ANN-003 - Line And Arrow Editing

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Draw one line and one arrow.
  2. While drawing the arrow, note the live shape before release.
  3. Return to default mode and select each one.
  4. Drag the body of each shape.
  5. Drag each endpoint handle.
- Expected:
  - Line and arrow drawing show a direction-aligned square size preview.
  - The live arrow being drawn renders as a clean single silhouette without a contrasting outline around it.
  - The committed arrow renders with a visible outline that improves contrast against the screenshot.
  - Selected line and arrow annotations show endpoint handles and L-bracket corner markers around the tight bounding box of the drawn geometry (not including the endpoint handles themselves).
  - Dragging the body moves the whole annotation.
  - Dragging an endpoint reshapes the annotation.

### GF-MAN-ANN-004 - Rectangle, Filled Rectangle, Ellipse, And Filled Ellipse Editing

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Draw one outlined rectangle and one filled rectangle.
  2. Draw one outlined ellipse and one filled ellipse.
  3. Move each one in default mode.
  4. Resize the outlined rectangle from a corner and then a side handle.
  5. Resize the outlined ellipse from a corner and then a side handle.
- Expected:
  - Rectangle and Ellipse show an axis-aligned square cursor preview sized to their stroke width while armed.
  - Filled Rectangle and Filled Ellipse do not expose a cursor stroke-size preview while armed.
  - Selected rectangles and ellipses show resize handles when space permits but no L-bracket corner markers.
  - Filled rectangles render as filled shapes and remain movable.
  - Filled ellipses render as filled shapes and remain movable.
  - Resizing follows the dragged handle correctly for both outlined shapes.

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
  - Brush, Line, Arrow, Rectangle, Filled rectangle, Ellipse, and Filled ellipse show the 8-slot annotation palette.
  - Highlighter shows the 6-slot highlighter palette.
  - `Esc` closes the wheel without changing the current color.

### GF-MAN-ANN-008 - Stroke Width Adjustment And Clamp

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate Brush, Highlighter, Line, Arrow, Rectangle, and Ellipse in turn.
  2. Adjust width with mouse wheel and with `Ctrl + =` and `Ctrl + -`.
  3. Attempt to go below the minimum and above the maximum.
  4. Repeat once with Filled Rectangle active.
  5. Repeat once with Filled Ellipse active.
- Expected:
  - Width changes affect Brush, Highlighter, Line, Arrow, outlined Rectangle, and outlined Ellipse.
  - The value clamps at `1..50`.
  - A centered size overlay appears when enabled.
  - Filled Rectangle rendering does not depend on stroke width.
  - Filled Ellipse rendering does not depend on stroke width.

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

### GF-MAN-ANN-011 - Text Tool Activation And Style Defaults

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and activate the Text tool with `T`.
  2. Move the pointer inside the selection, then left-click to start a text draft.
  3. Cancel the draft, keep the Text tool armed, and right-click inside the selection.
  4. Use the mouse wheel and `Ctrl + =` / `Ctrl + -` before starting the next draft.
  5. Start a new draft and inspect the initial font, color, and point size.
- Expected:
  - The armed Text tool shows an `I-beam` cursor plus an outlined `A` placement preview that tracks the pointer inside the selection.
  - Left-click inside the selection starts a live text draft at the click point.
  - Right-click while the Text tool is armed opens the hub-and-ring text style wheel
    (see `GF-MAN-ANN-015`).
  - Pre-draft size stepping changes the default text point size for the next draft.
  - The next draft uses the chosen color, font, and point size.

### GF-MAN-ANN-015 - Text Style Wheel Hub-And-Ring

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection, activate the Text tool, and right-click inside the selection
     with no draft active.
  2. Observe the wheel layout.
  3. Hover over the left hub semi-circle, then the right hub semi-circle.
  4. Click the left (color) hub semi-circle.
  5. Hover a color segment in the outer ring and click it.
  6. Reopen the wheel and click the right (font) hub semi-circle.
  7. Hover a font segment in the outer ring and click it.
  8. Reopen the wheel and press `Esc`.
  9. Reopen the wheel again and note which mode is active.
  10. Activate a non-Text annotation tool, right-click to open its color wheel, and
      compare the layout to the text style wheel.
- Expected:
  - The wheel shows a center hub with two semi-circles separated by a visible vertical
    gap.  The left semi-circle contains a small hue-spectrum gradient rectangle; the
    right semi-circle shows a lowercase `A` in the current font.
  - Hovering a hub semi-circle shows a green tint on that semi-circle; the outer ring
    is unchanged.
  - Color mode (initial): the outer ring shows 8 annotation color segments.  The active
    hub side (color) has an inverted (darker) fill.
  - Clicking the left hub when already in color mode is a no-op; the wheel stays open.
  - After selecting a color segment, the wheel closes and that color is used by the next
    text draft.
  - Font mode: the outer ring changes to 4 font segments each labeled with `A` in its
    font.  The right hub has the inverted fill.
  - After selecting a font segment, the wheel closes and that font is used by the next
    text draft.  The right-hub `A` updates when the wheel is reopened.
  - `Esc` closes the wheel without changing color or font.
  - Reopening the wheel opens in the mode last used in the session (font mode if font
    was the last mode selected).
  - Right-click while a text draft is active does not open the style wheel.
  - Non-text annotation color wheels show no hub; only a plain color ring.

### GF-MAN-ANN-012 - Text Draft Editing, Navigation, And Formatting

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection, activate the Text tool, and start a non-empty text draft.
  2. Enter printable text, then use `Insert` to switch between insert and overwrite mode.
  3. Drag with the mouse to create and adjust a selection.
  4. Exercise caret motion and selection extension with `Ctrl + Arrow`, `Home`, `End`, `PgUp`, and `PgDn`.
  5. Exercise `Ctrl + A`, `Ctrl + C`, `Ctrl + X`, `Ctrl + V`, `Ctrl + Z`, and `Ctrl + Shift + Z`.
  6. Exercise `Ctrl + B`, `Ctrl + I`, `Ctrl + U`, `Alt + Shift + 5`, and `Ctrl + Enter`.
  7. Right-click while actively editing, then try mouse wheel and `Ctrl + =` / `Ctrl + -`.
- Expected:
  - `WM_CHAR` entry inserts printable characters at the caret.
  - Insert mode shows a thin caret and overwrite mode shows a block caret.
  - Caret blink timing follows the platform blink setting.
  - Mouse dragging updates the text selection highlight correctly.
  - Navigation and selection extension follow the expected word, line, and document boundaries.
  - Copy, cut, paste, select-all, and draft-local undo/redo affect only the active draft and do not trigger overlay-global undo/redo.
  - Bold, italic, underline, and strikethrough toggles apply correctly and do not insert extra text.
  - `Ctrl + Enter` inserts a newline into the active draft.
  - Right-click while actively editing does not open the style wheel.
  - Size stepping is ignored while actively editing.

### GF-MAN-ANN-013 - Text Draft Commit, Cancel, And Toolbar Interaction

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Start an empty text draft and click outside it inside the selection.
  2. Start a non-empty text draft and click outside it inside the selection.
  3. Start an empty text draft and click a toolbar button.
  4. Start a non-empty text draft and click a toolbar button.
  5. Start a non-empty text draft and press `Enter`.
  6. Start another non-empty text draft and press `Esc`.
- Expected:
  - Clicking outside an empty draft discards it and starts the next draft at the new click point.
  - Clicking outside a non-empty draft commits it and starts the next draft at the new click point.
  - Clicking a toolbar button while editing discards an empty draft or commits a non-empty draft, then applies the button action without starting a new text draft at the button location.
  - `Enter` commits the active draft.
  - `Esc` cancels the active draft and keeps the Text tool armed.

### GF-MAN-ANN-014 - Committed Text Behavior And Output

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create and commit a text annotation.
  2. Return to default mode, select the committed text, move it, and delete it.
  3. Create and commit another text annotation, then attempt to re-enter text-editing on it.
  4. With a live draft still visible, save or copy the selection.
- Expected:
  - Committed text annotations can be selected, moved, and deleted.
  - Committed text annotations are not re-editable as text.
  - Saved or copied output includes committed text annotations.
  - Saved or copied output excludes live draft caret, selection highlight, and other draft chrome.

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
  - Highlighter annotations remain semi-transparent in the pasted image and preserve the marker-like darkening effect over the captured content.
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

### GF-MAN-CFG-004 - Tool Size Overlay Dismisses On Draw Start

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and activate the `Brush` tool.
  2. Change the brush size so the centered size overlay appears.
  3. Before the overlay times out, press and drag to start a brush stroke.
  4. Repeat with `Highlighter`, `Line`, and `Arrow`.
- Expected:
  - The first press dismisses the centered size overlay immediately.
  - That same press starts the annotation gesture without requiring an extra click.

### GF-MAN-CFG-005 - Per-Tool Size Step Persistence

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Start a capture and set distinct non-default size steps for Brush, Highlighter,
     Line, Arrow, Rectangle, Bubble, and Text.
  2. Switch back through those tools during the same capture and note the displayed
     size step and visible preview size for each one.
  3. Close the overlay and exit the app.
  4. Relaunch Greenflame, start a fresh capture, and revisit the same tools.
- Expected:
  - Each tool keeps its own independent size step instead of reusing one shared value.
  - Switching tools within the same session restores that tool's last-used size step immediately.
  - After relaunch, each tool restores the same size step it had before exit.
  - Filled Rectangle still ignores size stepping.

### GF-MAN-CFG-006 - Tool Color Persistence

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Change the brush color slot and the highlighter color slot.
  2. Close the overlay and exit the app.
  3. Relaunch and start a fresh capture.
  4. Open the brush color wheel and then the highlighter color wheel.
- Expected:
  - The previously chosen brush color slot is restored.
  - The previously chosen highlighter color slot is restored.
  - Custom brush and highlighter palette colors appear in their configured wheel
    slots.

### GF-MAN-CFG-007 - Highlighter Straighten Config Behavior

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Set `tools.highlighter.pause_straighten_ms=0` and relaunch Greenflame.
  2. Start a capture and draw a highlighter stroke while moving continuously.
  3. Set `tools.highlighter.pause_straighten_ms=800` and relaunch.
  4. Draw one continuously moving stroke and one stroke where the pointer pauses long
     enough to trigger straighten.
- Expected:
  - With `pause_straighten_ms=0`, each highlighter stroke starts as a straight bar immediately.
  - With a non-zero value, a continuously moving stroke remains freehand until the pause threshold is met.
  - After the pause threshold is met, the stroke snaps to a straight bar and continues tracking the cursor endpoint until release.

### GF-MAN-CFG-008 - Text Size Step Persistence

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Start a capture, activate the Text tool, and set a non-default text size step
     before drafting.
  2. Cancel or finish the capture, then exit Greenflame.
  3. Relaunch Greenflame, start a fresh capture, activate the Text tool, and start a new draft.
- Expected:
  - The chosen text size step persists after closing and reopening the app.
  - The new draft starts with the previously chosen mapped point size.

### GF-MAN-CFG-009 - CLI Padding Color Config

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Set `save.padding_color` to a non-default value such as `#ffffff`.
  2. Run `greenflame.exe --desktop --padding 12 --output <file>`.
  3. Inspect the outer padding in the saved image.
  4. Change `save.padding_color` back to `#000000` and repeat.
- Expected:
  - The saved image uses the configured `save.padding_color` when `--padding` is present.
  - Changing `save.padding_color` changes the rendered padding on the next CLI capture.

### GF-MAN-CFG-010 - Invalid Config Startup Warning And Editor Link

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Exit Greenflame.
  2. Edit `%USERPROFILE%\.config\greenflame\greenflame.json` so it contains invalid JSON, for example a missing comma between two top-level properties.
  3. Launch `greenflame.exe`.
  4. Observe the tray toast.
  5. Click the config-file path shown in the toast.
- Expected:
  - An error toast appears on startup even if success toasts are disabled.
  - The toast presents the error in this order: a short summary, the parser or validation detail when available, the clickable config-file path, then the consequence text.
  - The consequence text explains that defaults or partial settings are being used, the file will not be saved while invalid, and transient changes may be lost after a valid reload.
  - When available, the detail block includes the parser or validation detail and line/column.
  - Clicking the path opens `%USERPROFILE%\.config\greenflame\greenflame.json` in the same editor/open flow used by `Open config file...`.

### GF-MAN-CFG-011 - Invalid Config Reload Warning And Save Suppression

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Launch Greenflame with a valid config file.
  2. While the tray app is running, edit `%USERPROFILE%\.config\greenflame\greenflame.json` and introduce an invalid syntax or schema error.
  3. Wait for the file watcher reload.
  4. Make a persisted config change from the UI, such as changing a tool size or current color.
  5. Exit Greenflame.
  6. Inspect the config file on disk.
  7. Fix the config file so it becomes valid again, with an intentionally different persisted value from the transient change made in step 4.
  8. Relaunch Greenflame or wait for the live reload.
- Expected:
  - The tray app shows the same error toast when the watched file becomes invalid.
  - The invalid file is not overwritten by the transient UI change made while the file is broken.
  - When the file becomes valid again, the tray app shows a short info toast that config persistence is active again, with no file link.
  - After the file is fixed, the valid on-disk config reloads and wins over the transient in-memory change from step 4.

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
  2. Create one window titled exactly `Codex` and another whose title merely contains `Codex`, then run `greenflame.exe --window "Codex" --output <file>`.
  3. Open two visible windows with the same exact title and run `greenflame.exe --window "<title>" --output <file>`.
  4. Copy one reported `hwnd` from the ambiguity output and rerun with `greenflame.exe --window-hwnd <hex> --output <file>`.
  5. Minimize the exact target window and rerun the appropriate command.
- Expected:
  - The single-match case succeeds and saves the target window.
  - When one exact-title match exists among broader substring matches, `--window "<name>"` selects the exact-title window automatically.
  - The truly ambiguous case exits with code `7` and lists each candidate with `hwnd`, class, and rect.
  - `--window-hwnd <hex>` selects the exact reported window even when titles are identical.
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

### GF-MAN-CLI-006 - Padding Color Override And Off-Desktop Fill

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Run `greenflame.exe --region <x,y,w,h> --padding 12 --output <file>` and confirm synthetic padding is added around the saved image.
  2. Run `greenflame.exe --window "<unique-title>" --padding 8,16,24,32 --output <file>`.
  3. Run `greenflame.exe --monitor <id> --padding 12 --padding-color "#ffffff" --output <file>`.
  4. Place a target so part of the requested capture area lies outside the virtual desktop, then rerun the padded command.
  5. Run `greenflame.exe --monitor <id> --padding 12 --padding-color "#00ff00" --output <file>.png` and inspect the saved PNG in a viewer that shows transparency distinctly.
- Expected:
  - Padded captures preserve the nominal captured image size and add the requested outer padding.
  - `--padding-color` overrides the configured padding color for that invocation only.
  - Off-desktop portions of the nominal capture area are filled with the same padding color instead of being clipped away.
  - When off-desktop fill occurs, `stderr` includes the fill warning and the command still succeeds if any capturable pixels exist.
  - The saved PNG padding is an opaque solid color, not transparent or semi-transparent.

### GF-MAN-CLI-007 - Invalid Config Startup Stderr Omits UI-Only Consequences

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Exit any running tray instance of Greenflame.
  2. Edit `%USERPROFILE%\.config\greenflame\greenflame.json` so it contains invalid JSON, for example a missing comma between two top-level properties.
  3. Run `greenflame.exe --region <x,y,w,h> --output <file>` from a console.
  4. Inspect `stderr`.
- Expected:
  - The capture still proceeds with defaults or partial config and writes the requested output when the capture itself succeeds.
  - `stderr` includes the config file path, the short summary, and the parser or validation detail when available.
  - `stderr` includes the first consequence sentence explaining that defaults or partial settings are being used.
  - `stderr` does not include the UI-only consequence text about the file not being saved or transient changes being lost after reload.
  - The command exits without starting a tray instance.

### GF-MAN-CLI-008 - Inline `--annotate` Happy Path And Padding Edge Coverage

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Run `greenflame.exe --region 100,100,220,160 --output "%TEMP%\greenflame-inline-line.png" --overwrite --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":219,\"y\":159},\"size\":4,\"color\":\"#ff0000\"}]}"`.
  2. Run `greenflame.exe --region 100,100,220,160 --padding 32 --padding-color "#202020" --output "%TEMP%\greenflame-inline-padding.png" --overwrite --annotate "{\"annotations\":[{\"type\":\"rectangle\",\"left\":-16,\"top\":-12,\"width\":120,\"height\":72,\"size\":3,\"color\":\"#00ff00\"},{\"type\":\"bubble\",\"center\":{\"x\":-4,\"y\":-4},\"size\":12}]}"`.
- Expected:
  - Step 1 exits successfully and draws a red diagonal line across the saved capture.
  - Step 2 exits successfully and draws both annotations over the final padded image, including the portions that extend into the padding fill.
  - The output images are written exactly to the requested paths.
  - No tray instance remains running after either one-shot command.

### GF-MAN-CLI-009 - Fixture-Based `--annotate` Coverage

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
1. Run `greenflame.exe --region 100,100,240,180 --padding 40 --padding-color "#202020" --output "%TEMP%\greenflame-local-mixed.png" --overwrite --annotate ".\schemas\examples\cli_annotations\local_mixed_edge_cases.json"`.
2. Run `greenflame.exe --desktop --padding 64 --padding-color "#101010" --output "%TEMP%\greenflame-global-padding.png" --overwrite --annotate ".\schemas\examples\cli_annotations\global_padding_edge_cases.json"`.
3. Run `greenflame.exe --region 100,100,240,180 --padding 48 --padding-color "#202020" --output "%TEMP%\greenflame-brush-padding.png" --overwrite --annotate ".\schemas\examples\cli_annotations\brush_padding_edge_cases.json"`.
- Expected:
  - The local-space fixture renders the mixed annotation set relative to the capture origin, including text, bubbles, and out-of-bounds geometry.
  - The global-space fixture renders annotations relative to the virtual-desktop origin, including negative or off-screen coordinates where applicable.
  - The brush fixture renders a continuous neon-magenta stroke that starts in the upper-left padding, passes through the captured content, and exits into the lower-right padding without being clipped at the original capture boundary.
  - In all three cases, annotations that land in padded areas remain visible on top of the padding color.
  - Bubble numbering matches annotation order among bubbles only: first bubble is `1`, second bubble is `2`, and so on.

### GF-MAN-CLI-010 - Invalid `--annotate` Inputs Fail With Exit `14`

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Run `greenflame.exe --desktop --output "%TEMP%\greenflame-missing-file.png" --overwrite --annotate ".\schemas\examples\cli_annotations\does_not_exist.json"`.
  2. Run `greenflame.exe --desktop --output "%TEMP%\greenflame-malformed-json.png" --overwrite --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":10,\"y\":10}}"`.
  3. Run `greenflame.exe --region 100,100,220,160 --output "%TEMP%\greenflame-invalid-unknown-key.png" --overwrite --annotate ".\schemas\examples\cli_annotations\invalid_unknown_key.json"`.
  4. Run `greenflame.exe --region 100,100,220,160 --output "%TEMP%\greenflame-invalid-font.png" --overwrite --annotate ".\schemas\examples\cli_annotations\invalid_missing_font_family.json"`.
- Expected:
  - Each command exits with code `14`.
  - The missing-file case reports that the annotation file could not be read.
  - The malformed inline JSON case reports a JSON syntax error rooted at `--annotate`.
  - The unknown-key fixture reports a strict validation failure pointing at the offending property path.
  - The missing-font fixture reports that the explicit font family is not installed.
  - None of the four commands leaves behind a partially written output file.

### GF-MAN-CLI-011 - CLI Window Capture Backends

- Priority: `P2`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. For one visible unobscured window, run:
     `greenflame.exe --window "<unique-title>" --window-capture gdi --output "%TEMP%\greenflame-window-gdi-visible.png" --overwrite`
  2. Without moving the window, run:
     `greenflame.exe --window "<unique-title>" --window-capture wgc --output "%TEMP%\greenflame-window-wgc-visible.png" --overwrite`
  3. Without moving the window, run:
     `greenflame.exe --window "<unique-title>" --window-capture auto --output "%TEMP%\greenflame-window-auto-visible.png" --overwrite`
  4. Fully obscure the same target window with another window, then rerun the `gdi` and `wgc` commands with new output names.
  5. Move the same target window so part of it sits outside the visible desktop, then rerun:
     `greenflame.exe --window "<unique-title>" --window-capture gdi --padding 32 --output "%TEMP%\greenflame-window-gdi-offscreen.png" --overwrite`
     and
     `greenflame.exe --window "<unique-title>" --window-capture wgc --padding 32 --output "%TEMP%\greenflame-window-wgc-offscreen.png" --overwrite`
  6. Rerun once with:
     `greenflame.exe --window-hwnd <hex> --window-capture wgc --output "%TEMP%\greenflame-window-wgc-hwnd.png" --overwrite`
  7. Rerun once with:
     `greenflame.exe --window "<unique-title>" --window-capture wgc --padding 32 --annotate ".\schemas\examples\cli_annotations\local_mixed_edge_cases.json" --output "%TEMP%\greenflame-window-wgc-annotated.png" --overwrite`
  8. Minimize the target window and run:
     `greenflame.exe --window "<unique-title>" --window-capture wgc --output "%TEMP%\greenflame-window-wgc-minimized.png" --overwrite`
  9. Create two windows whose titles both match the same `--window` query, leave one visible, minimize the other, and run:
     `greenflame.exe --window "<shared-title>" --window-capture wgc --output "%TEMP%\greenflame-window-wgc-minimized-match-warning.png" --overwrite`
  10. Minimize every matching window for that same title query and rerun the same command with a new output name.
- Expected:
  - `gdi` continues to reflect current visible-desktop limitations for unobscured, obscured, and partially off-screen windows.
  - `wgc` captures the target window itself for unobscured, obscured, and partially off-screen cases, without the usual GDI warning text.
  - `auto` behaves like `wgc` when WGC succeeds.
  - The partially off-screen `wgc` case still adds only synthetic outer padding; it does not fill missing source pixels inside the nominal window image.
  - The `--window-hwnd` `wgc` path behaves the same as the title-based `wgc` path for the same target window.
  - The annotated `wgc` capture renders annotations over the final padded image using the same semantics as other CLI captures.
  - The minimized-window `wgc` command still fails with exit code `13`.
  - When one visible title match and one or more minimized title matches coexist, the `wgc` title-based command still succeeds and `stderr` warns that the minimized matches were skipped.
  - When all title matches are minimized, the `wgc` title-based command exits with code `13` and reports that matching windows were minimized rather than saying no window matched.
  - Record whether any yellow capture border appears on screen or in the saved output while `wgc` is active.
