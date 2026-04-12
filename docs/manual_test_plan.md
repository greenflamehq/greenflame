---
title: Human Test Plan
summary: Manual validation plan for Win32, rendering, DPI, tray, and other end-to-end behaviors that automated tests cannot cover reliably.
audience:
  - contributors
  - qa
status: reference
owners:
  - core-team
last_updated: 2026-04-03
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
  - The right-click menu includes region, monitor, window, desktop, last-region, last-window, `Include captured cursor`, start-with-Windows, `Open config file...`, About, and Exit actions.
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
  - Each action honors the persisted `capture.include_cursor` setting.

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
  - The modified-hotkey warning lists each conflicted modified `Prt Scrn`
    shortcut explicitly.
  - `%TEMP%\greenflame-debug.log` records one `hotkey` entry per failed
    registration with the Windows error code/message.
  - The warning does not identify the owning process because Windows does not
    expose that information for failed `RegisterHotKey` calls.
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
  - After the drag is committed, the stable selection border sits just outside the
    clipped screenshot region instead of being tucked under the dim edge.
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
  5. Drag or resize the selection against each virtual-desktop edge.
- Expected:
  - Corner and side drags resize in the expected directions.
  - Dragging inside the selection moves the selection.
  - The selection stops at the virtual-desktop boundary and never extends beyond it.
  - The solid selection border remains visibly above the dim outside the clipped
    screenshot region while moving or resizing.
  - When pressed against a desktop edge, the visible screenshot pixels remain a 1:1 crop with no stretching or smearing.
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
  - For `Ctrl` window quick-select, the previewed window is visually lifted above overlapping windows instead of showing only the original visible desktop crop.
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
  2. Create a selection and open the selection wheel, then press `Esc`.
  3. Open the help overlay and press `Esc`.
  4. Activate an annotation tool, start a stroke, and press `Esc` before releasing the mouse.
  5. With a normal stable selection and no sub-UI open, press `Esc`.
- Expected:
  - `Esc` closes the innermost active UI first.
  - Selection wheel closes before the overlay cancels.
  - Help closes before the overlay cancels.
  - Pressing `Esc` during an active annotation gesture cancels only the in-progress gesture and leaves the tool armed.
  - With no nested UI open, the overlay cancels or backs out cleanly.

### GF-MAN-SEL-006 - Ctrl Window Lift, Off-Screen Badge, And Visible-Only Editing

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Partially obscure a target window so that only a narrow visible sliver remains, then start interactive capture.
  2. Press and release `Ctrl` without moving the mouse and confirm the preview enters and exits immediately at the current pointer position.
  3. Hold `Ctrl` over that visible sliver and move the cursor around the lifted preview before clicking.
  4. Repeat with the same target moved 1 px outside the desktop edge and verify the size labels.
  5. Repeat with the target substantially outside the desktop bounds.
  6. Repeat with a target window larger than the full desktop if available.
  7. Commit each selection with `Ctrl`-click, then try drawing, moving annotations, and placing text near the visible boundary.
  8. Exercise partially off-screen window previews and confirm the center size label adds the off-screen note beneath the `width x height` text.
  9. Commit a partially off-screen window selection, then start a move drag and a resize drag.
  10. Commit a partially off-screen window selection and start editing annotations.
- Expected:
  - Pressing or releasing `Ctrl` alone updates the preview immediately; a mouse move is not required to enter or leave the lifted window mode.
  - While `Ctrl` is held, the same candidate window keeps hit-test precedence while the pointer remains inside its lifted visible area.
  - A partially obscured target can still be selected from a small visible sliver, and the lifted preview shows the target window's own pixels rather than the occluding window.
  - Entering, updating, and committing the lifted `Ctrl` preview does not silently close or crash the overlay, including for partially off-screen and oversized windows.
  - For windows extending off-screen, the committed editable region is limited to the visible desktop intersection.
  - For partially off-screen windows, the side and center size labels show the full captured window width and height, not only the visible on-desktop intersection.
  - The fixed note text `Includes off-screen pixels` appears beneath the center `width x height` label only when full off-screen pixels are included.
  - The lifted `Ctrl` window path never introduces a second captured cursor from WGC itself; at most one captured cursor is visible, and `Ctrl + K` still fully hides or shows it.
  - The center size label keeps the larger `width x height` text and uses the smaller dimension-label font for the off-screen note beneath it.
  - As soon as a committed window-backed selection is actually moved or resized, it immediately drops full-window/window-source semantics and the interactive size labels switch to the real region size.
  - Once the selection is committed and normal editing begins, the off-screen note disappears together with the rest of the interactive size label.
  - Annotation creation, drag, resize, text placement, and cursor previews stay constrained to the visible portion; invisible off-screen pixels are never directly editable.

### GF-MAN-SEL-007 - WGC Debug Log For Interactive Ctrl Window Preview

- Priority: `P2`
- Run on: `ENV-A`, `ENV-B`
- Prerequisite: Build Greenflame with `GREENFLAME_LOG` enabled.
- Steps:
  1. Delete `%TEMP%\greenflame-debug.log` if it exists.
  2. Start interactive capture and exercise `Ctrl` window preview on a normal window, a partially obscured window, and a partially off-screen window.
  3. If a failure or silent close is observed, reopen `%TEMP%\greenflame-debug.log`.
- Expected:
  - `%TEMP%\greenflame-debug.log` is created.
  - The log records interactive preview requests and WGC capture/session events for the tested windows.
  - If a capture fails, the log includes a corresponding `failure:` line instead of leaving the path silent.

### GF-MAN-SEL-008 - Reopen Ctrl Window Preview After Full Cancel

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Start interactive capture from the tray.
  2. Hold `Ctrl` and `Ctrl`-click a normal capturable window.
  3. Press `Esc`, then press `Esc` again so the overlay fully closes.
  4. Start interactive capture from the tray again.
  5. Hold `Ctrl` without clicking, then move across one or more capturable windows.
- Expected:
  - The overlay stays open after the second launch.
  - Holding `Ctrl` after the reopen does not crash or silently close Greenflame.
  - The lifted window preview still appears and updates normally after the full cancel/reopen cycle.

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
  - The `Keyboard Shortcuts` title and the `Ctrl+H or Esc to close` hint share the same visual header line; the close hint does not render higher than the title.
  - The shortcut list includes `Ctrl + K` for the captured-cursor toggle.
  - The shortcut list includes `Ctrl + P` for pinning the current selection to the desktop.
  - All listed shortcuts remain readable without clipping, and the `Annotation Tools` section stays in the second column.
  - The final `Annotation Tools` rows, including the mouse-wheel size shortcuts, remain fully visible on a 1920x1080 display.
  - The toolbar `Help` button opens help on button release and does not toggle an
    annotation tool.
  - While visible, other overlay interactions are blocked.
  - Keyboard, `Esc`, and left-click dismissal all work.
  - After dismissing help with `Esc` or a left-click, the toolbar button under the
    cursor becomes hovered and clickable again without requiring mouse movement.
- Regression: the help-overlay close hint previously rendered a few pixels too high relative to the title text.

### GF-MAN-UI-002 - Toolbar Placement And Tooltips

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Create selections near each screen edge and corner.
  2. Hover each toolbar button.
  3. Toggle a few tools on and off.
- Expected:
  - The toolbar stays attached to the selection and remains visible on-screen.
  - The toolbar layout is `[annotation tools][spacer][cursor][pin][spacer][help]`.
  - The `Help` button stays last on the right.
  - Hovering shows an opaque tooltip for each button.
  - Tool button tooltips include the tool name followed by its hotkey in
    parentheses, such as `Brush (B)`.
  - The captured-cursor button uses a stable cursor glyph in both states, with
    tooltip text that reflects the action (`Show...` or `Hide...`).
  - The pin button uses a stable pin glyph with tooltip text `Pin to desktop (Ctrl+P)`.
  - The help button tooltip includes the help hotkey in parentheses.
  - Hovering any visible toolbar button shows the standard pointer cursor.
  - Button state tracks the active tool correctly.

### GF-MAN-UI-003 - Obfuscate Risk Warning

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Start from a config where `tools.obfuscate.risk_acknowledged` is unset or `false`.
  2. Create a stable selection.
  3. Arm `Obfuscate` from the toolbar.
  4. Move the pointer over each warning button, then click outside the panel.
  5. Press `Esc`.
  6. Re-arm `Obfuscate` with the `O` hotkey.
  7. Click the accept button.
  8. Close the overlay, reopen it, and arm `Obfuscate` again.
- Expected:
  - The first toolbar activation opens an in-overlay warning dialog.
  - The dialog blocks drawing, selection edits, toolbar actions, mouse-wheel size changes, right-click color-wheel actions, and help overlay toggling.
  - Hover and pressed states are shown only on the two warning buttons.
  - Clicking outside the panel does nothing.
  - Pressing `Esc` dismisses the dialog and clears the Obfuscate tool without clearing the selection.
  - The hotkey path opens the same warning dialog.
  - Accepting the warning keeps Obfuscate armed.
  - After acceptance is persisted, later Obfuscate activations do not show the warning again.

## Captured Cursor

### GF-MAN-CURSOR-001 - Overlay Toggle, Output Layering, And Persistence

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Set `capture.include_cursor=false` in the config and relaunch Greenflame.
  2. Place the live pointer somewhere obvious inside a new interactive selection.
  3. Press `Ctrl + K`, then press it again.
  4. Use the toolbar cursor button to turn the captured cursor on.
  5. Draw one annotation over the captured cursor and save or copy the result.
  6. Start a new interactive capture.
  7. Exit and relaunch Greenflame, then start another interactive capture.
- Expected:
  - `Ctrl + K` and the toolbar button toggle the captured cursor for the current screenshot.
  - If the capture started with the cursor hidden, toggling it on during the same capture still reveals the cursor sampled at capture time.
  - When shown, the captured cursor appears inside the frozen screenshot, not as a live overlay primitive.
  - Saved and copied output shows the captured cursor below annotations.
  - The captured cursor cannot be selected, moved, resized, or hit-tested as an annotation.
  - The toggled state persists into the next capture and after app restart.

### GF-MAN-CURSOR-002 - Direct Clipboard Captures Honor Persisted Cursor Setting

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Set `capture.include_cursor=true` in the config and relaunch Greenflame.
  2. Trigger `Ctrl + Prt Scrn`, `Shift + Prt Scrn`, and `Ctrl + Shift + Prt Scrn`, pasting each result into Paint.
  3. Set `capture.include_cursor=false` in the config and relaunch Greenflame.
  4. Repeat the same three hotkeys and paste each result again.
- Expected:
  - With `capture.include_cursor=true`, each direct clipboard capture includes the captured cursor when it falls inside the captured area.
  - With `capture.include_cursor=false`, the same capture paths omit the captured cursor.
  - None of these paths opens the overlay.

### GF-MAN-CURSOR-002A - Tray Menu Cursor Toggle Persists Config Default

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Set `capture.include_cursor=false` in the config and relaunch Greenflame.
  2. Right-click the tray icon and confirm `Include captured cursor` is unchecked.
  3. Click `Include captured cursor`.
  4. Reopen the tray menu and confirm the item is now checked.
  5. Trigger a direct clipboard capture such as `Ctrl + Prt Scrn` and verify the cursor behavior.
  6. Exit and relaunch Greenflame, reopen the tray menu, and verify the item remains checked.
  7. Toggle the item off again and repeat the same verification.
- Expected:
  - The tray item checkmark matches the current persisted `capture.include_cursor` value.
  - Toggling the tray item updates the config-backed default without manually editing the config file.
  - Direct clipboard captures honor the new persisted value immediately.
  - The value remains stable across app restart.

### GF-MAN-CURSOR-003 - Cursor Placement Across Types And Edges

- Priority: `P2`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Capture a screenshot while showing the normal arrow cursor near each edge of the selection so the cursor is fully inside, partially intersecting, and fully outside the selected output.
  2. Repeat with an I-beam cursor over editable text.
  3. Repeat with resize, crosshair, busy/wait, and one custom application cursor if available.
  4. On `ENV-B`, repeat at least one case across a mixed-DPI monitor boundary and once with negative virtual-desktop coordinates involved.
- Expected:
  - Fully visible cursors are placed at the correct hotspot-aligned location.
  - Partially intersecting cursors are clipped normally instead of jumping or disappearing.
  - Fully out-of-bounds cursors are omitted from the output.
  - Placement stays stable across cursor types, including I-beam.
  - Record any hotspot offset regression immediately, especially for I-beam placement.

## Pinned Images

### GF-MAN-PIN-001 - Create Pin From Rendered Selection

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Start interactive capture and create a stable selection.
  2. Turn on the captured-cursor toggle and place the captured cursor somewhere obvious inside the selection.
  3. Add at least one visible annotation to the selection.
  4. Press `Ctrl + P`.
  5. Compare the pinned image against the just-selected on-screen content.
- Expected:
  - The overlay closes only after the pin is created successfully.
  - A new always-on-top pinned-image window appears at the selection's screen location at 100% scale.
  - The newly created pin is active immediately and uses the stronger green halo treatment.
  - The pinned bitmap matches the rendered overlay export exactly: committed annotations are included, the captured cursor is included only when toggled on, and no overlay chrome is baked into the bitmap.
  - The visible green halo surrounds the pin window but is not part of the bitmap content itself.

### GF-MAN-PIN-002 - Halo State, Drag, And Zoom

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Create one pinned image.
  2. Click another app, then click the pin again.
  3. Drag the pin to a new position.
  4. Use the mouse wheel to zoom in and out.
  5. Use `Ctrl + =` and `Ctrl + -` to zoom in and out again.
- Expected:
  - The halo remains visible even when the pin is inactive.
  - The active pin uses a stronger, less transparent green halo than the idle pin.
  - Dragging moves the pin cleanly with no border-resize affordance or resize-handle behavior.
  - Wheel zoom and keyboard zoom both resize the displayed image around its center.
  - Pointer hit-testing remains aligned with the visible pin while dragging or zooming.

### GF-MAN-PIN-003 - Context Menu, Shortcuts, Rotation, And Opacity

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create one pinned image and right-click it.
  2. Verify the context-menu item order and visible accelerators.
  3. Use the menu to rotate right once and decrease opacity once.
  4. Use `Ctrl + Left`, `Ctrl + Right`, `Ctrl + Up`, and `Ctrl + Down`.
  5. Hold `Ctrl + Up` and `Ctrl + Down` long enough to trigger key repeat.
  6. With the pin still rotated and partially transparent, copy it to Paint and save it to a file.
- Expected:
  - Right-click activates the pin and keeps its stronger halo state while the menu is open.
  - The menu contains `Copy to clipboard`, `Save to file`, `Rotate Right`, `Rotate Left`, `Increase Opacity`, `Decrease Opacity`, and `Close` in that grouped order.
  - The menu shows the expected keyboard shortcuts for those actions.
  - Rotation changes the displayed orientation in 90-degree increments.
  - Opacity changes affect only the on-screen bitmap, not the halo strength.
  - Holding `Ctrl + Up` or `Ctrl + Down` continuously steps the opacity until the configured clamp is reached; repeated keydown events do not require individual key presses.
  - Copied and saved output reflects the current rotation but remains fully opaque even if the on-screen pin opacity was reduced.

### GF-MAN-PIN-004 - Multiple Pins And Independent Lifetime

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create one pinned image from a first selection.
  2. Start a second capture and create a different pinned image.
  3. Move, rotate, or change opacity on only one of the pins.
  4. Close one pin with `Esc`.
  5. Exit Greenflame from the tray.
- Expected:
  - Multiple pinned images can coexist at once.
  - Each pin keeps its own position, scale, rotation, and opacity.
  - Closing one pin does not affect the others.
  - Exiting the app closes all remaining pins.

### GF-MAN-PIN-005 - Cross-Monitor Placement And Capture Exclusion

- Priority: `P2`
- Run on: `ENV-B`
- Steps:
  1. Create a pinned image on a non-primary monitor, including a case with negative virtual-desktop coordinates if available.
  2. Move or zoom that pin near or across a monitor boundary.
  3. Start a new interactive capture and direct clipboard capture that would otherwise include the pin's on-screen area.
- Expected:
  - Initial pin placement matches the original selection position on the correct monitor.
  - Moving and zooming the pin across monitor boundaries does not introduce DPI jumps, seams, or pointer offset.
  - Later captures do not include the pinned-image window itself.

### GF-MAN-PIN-006 - Pinning Off-Screen And Oversized Ctrl-Selected Windows

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Use interactive `Ctrl` window selection on a target that extends 1 px outside the desktop, then pin it.
  2. Repeat with a target substantially outside the desktop bounds.
  3. Repeat with a target larger than the full desktop if available.
  4. Repeat on a non-primary or negative-coordinate monitor if available.
- Expected:
  - The new pin uses the full captured window bitmap, not just the visible editable portion.
  - Initial pin scale is still `100%`.
  - Initial pin placement uses the full window capture rect, so partially off-screen and oversized source windows may produce pins that also begin partly off-screen.
  - Negative-coordinate and mixed-DPI monitor placement stays correct with no jumps or offset between the original window location and the new pin.

## Annotations

### GF-MAN-ANN-001 - Tool Toggling

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection.
  2. Toggle `B`, `H`, `L`, `A`, `R`, `Shift+R`, `E`, `Shift+E`, `O`, `T`, and `N` from the keyboard.
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
  - Selecting either stroke shows a clockwise animated marquee around the tight bounding box of the visible stroke geometry.
  - Once selected, dragging from blank pixels inside that marquee still moves the whole stroke.
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
  - Selecting a committed bubble shows a clockwise animated marquee around the bubble bounds, and the bubble remains movable in default mode.
  - Once selected, dragging from empty pixels inside the bubble marquee still moves the bubble.
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
  - Selected line and arrow annotations show endpoint handles plus a clockwise animated marquee around the tight bounding box of the drawn geometry (not including the endpoint handles themselves).
  - Once selected, dragging from blank pixels inside that marquee moves the whole annotation.
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
  - Selected rectangles and ellipses show resize handles when space permits plus a clockwise animated marquee that sits 1 px outside the visible shape instead of covering shape pixels.
  - For outlined shapes, dragging from the empty interior still moves the selected annotation.
  - Filled rectangles render as filled shapes and remain movable.
  - Filled ellipses render as filled shapes and remain movable.
  - Resizing follows the dragged handle correctly for both outlined shapes.

### GF-MAN-ANN-004A - Obfuscate Creation, Modes, And Editing

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection and activate the Obfuscate tool.
  2. Move the cursor without drawing, then right-click once.
  3. Set a non-default block size greater than `1` and drag out an obfuscate over detailed content.
  4. Release the mouse and compare the committed block-pixelated result to the preview visible at mouse-up.
  5. Change the block size to `1` and drag out a second obfuscate over different detailed content.
  6. Release the mouse and compare the committed blur result to the preview visible at mouse-up.
  7. Return to default mode, select each committed obfuscate, move one, and resize the other from both a corner and a side handle.
- Expected:
  - The Obfuscate tool shows an axis-aligned square cursor preview while armed.
  - Right-click does not open a selection wheel for Obfuscate.
  - During the initial drag, the live obfuscate preview includes any already-committed annotations underneath the dragged bounds instead of sampling only the raw screenshot.
  - The first obfuscate commits as block pixelation, with visibly discrete square cells.
  - The committed block-pixelated result matches the preview state at mouse-up aside from minor GPU-vs-CPU sampling differences.
  - The second obfuscate commits as blur rather than block pixelation.
  - The committed blur result matches the preview state at mouse-up aside from minor GPU-vs-CPU sampling differences.
  - Selected obfuscates show resize handles when space permits plus a clockwise animated marquee that sits 1 px outside the obfuscate bounds.
  - Moving and resizing keep the live preview aligned to the dragged bounds, and the committed result matches the final bounds after release.

### GF-MAN-ANN-004B - Reactive Obfuscate Preview, Stacking, And Output

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Draw a normal annotation over detailed content, then place an obfuscate above part of it.
  2. Return to default mode and drag the lower annotation beneath the obfuscate.
  3. Undo and redo that drag.
  4. Save once and copy once, then inspect the output in an external viewer or Paint.
  5. If practical, add a second obfuscate that overlaps the first, then drag the same lower annotation again beneath both.
- Expected:
  - While the lower annotation is moving, the obfuscate preview updates live instead of showing stale committed pixels.
  - A single live obfuscate preview continues to include the current lower annotation state underneath it during the drag.
  - Undo and redo restore both the lower annotation change and the recomputed obfuscate content together.
  - The saved and copied outputs match the final on-screen obfuscate result for the single-obfuscate case.
  - When two obfuscates overlap, the stacked preview updates in z-order without leaving stale intermediate content.
  - When two obfuscates overlap, each live preview still includes the current lower annotation state rather than regressing to raw-capture sampling.

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

### GF-MAN-ANN-005A - Multi-Selection Toggle, Marquee, And Group Move

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a region and add at least three annotations with two close enough to share a useful group bounds box.
  2. In default mode, click one annotation to select it.
  3. `Ctrl+click` a second annotation to add it to the selection.
  4. `Ctrl+click` that same second annotation again.
  5. `Ctrl+drag` a marquee that touches two annotations but fully contains only one of them.
  6. With two annotations selected, drag from blank space inside the group marquee but not on any annotation pixels.
  7. With two annotations selected, inspect line, rectangle, ellipse, and obfuscate selections if available.
- Expected:
  - `Ctrl+click` toggles the topmost covered annotation in or out of the selection.
  - `Ctrl+drag` adds every touched annotation; full containment is not required.
  - The live `Ctrl+drag` rectangle uses the animated black/white selected-annotation marquee rather than the green capture-region selection border.
  - That live `Ctrl+drag` marquee is already animating before `Ctrl` is released and does not pause while the drag remains active.
  - The live `Ctrl+drag` marquee can extend outside the captured screenshot region
    onto the dimmed overlay; it is not clipped to the bright capture area.
  - When more than one annotation is selected, the marquee encloses the union of the member selection frames.
  - When more than one annotation is selected, dragging from blank space inside that group marquee moves the whole group.
  - Group moves preserve relative spacing between the selected annotations.
  - When more than one annotation is selected, no endpoint or resize handles are shown.
  - If the group shrinks back to one annotation, that annotation's normal single-selection handles return immediately.

### GF-MAN-ANN-006 - Delete, Undo, And Redo

- Priority: `P0`
- Run on: `ENV-A`
- Steps:
  1. Create a region and several annotations.
  2. Select one annotation and press `Delete`.
  3. Recreate or reselect multiple annotations and press `Delete` again.
  4. Use `Ctrl + Z` repeatedly.
  5. Use `Ctrl + Shift + Z` repeatedly.
- Expected:
  - Delete removes the current selected annotation set, whether that set has one item or many.
  - Multi-selection delete is undone and redone as one step.
  - Undo walks backward through both region and annotation history.
  - Redo restores the undone changes in order.

### GF-MAN-ANN-007 - Selection Wheel Selection And Cancel

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate any annotation tool except Obfuscate.
  2. Right-click to open the selection wheel.
  3. Hover different segments.
  4. Pick a segment once, then reopen and dismiss with `Esc`.
  5. Repeat with the Highlighter tool active.
- Expected:
  - The wheel opens centered on the cursor.
  - Each ring segment shows a full light-gray perimeter border with a full inset black perimeter border.
  - Hovering inflates the segment under the pointer without widening its angular span.
  - The currently selected segment keeps the smaller outward-only inflation when no other segment is hovered.
  - Selecting a segment changes the color used by future annotations.
  - Brush, Line, Arrow, Rectangle, Filled rectangle, Ellipse, and Filled ellipse show the 8-slot annotation palette.
  - Highlighter shows the 6-slot highlighter palette, and when its hub is visible the hub halves use the same quiet border language as the ring with a subtler curved-edge inflation for active and hovered states.
  - `Esc` closes the wheel without changing the current color.

### GF-MAN-ANN-007A - Selection Wheel Dismissed By Toolbar Click

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate any annotation tool except Obfuscate.
  2. Right-click to open the selection wheel.
  3. Without moving the wheel, left-click directly on a toolbar button (e.g. switch to a different annotation tool).
  4. Repeat, but this time right-click to open the wheel then click the Undo toolbar button if available, or the Help button.
- Expected:
  - The selection wheel closes immediately on the toolbar click.
  - The toolbar button's action is executed (tool switches, or Help opens) — the click is not swallowed.
  - No phantom annotation is started and no stray mouse-up event fires after the dismiss.

### GF-MAN-ANN-008 - Tool Size Adjustment And Clamp

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate Brush, Highlighter, Line, Arrow, Rectangle, Ellipse, Bubble, and Obfuscate in turn.
  2. Adjust size with mouse wheel and with `Ctrl + =` and `Ctrl + -`.
  3. Attempt to go below the minimum and above the maximum.
  4. Repeat once with Filled Rectangle active.
  5. Repeat once with Filled Ellipse active.
- Expected:
  - Size changes affect Brush, Highlighter, Line, Arrow, outlined Rectangle, outlined Ellipse, Bubble, and Obfuscate.
  - The value clamps at `1..50`.
  - A centered size overlay appears when enabled.
  - With Obfuscate active, `1` produces blur mode and values above `1` produce block pixelation.
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
  - The selected-annotation marquee encloses all visible highlighter pixels and advances clockwise.

### GF-MAN-ANN-010A - Live Brush And Text Drafts Stay Under The Dim

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection that leaves obvious dimmed desktop visible around it.
  2. Activate the Brush tool, start a stroke inside the selection, and drag the live
     stroke so part of it extends outside the selection before releasing the mouse.
  3. Repeat with the Highlighter tool.
  4. Activate the Text tool, click near a selection edge, and type enough text for the
     live draft to extend into the dimmed area before committing it.
- Expected:
  - While the brush or highlighter gesture is still active, the portion outside the
    selection stays under the dim instead of painting over the obscured region.
  - While the text draft is active, glyphs, selection highlight, and caret stay under
    the dim outside the selection instead of painting over the obscured region.
  - Committing each draft does not change the outside-selection layering; the committed
    result matches the live preview.
- Regression: brush/highlighter strokes and live text drafts previously painted on top
  of the dimmed region until commit.

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
  10. Activate a non-Text annotation tool, right-click to open its selection wheel, and
      compare the layout to the text style wheel.
- Expected:
  - The wheel shows a center hub with two semi-circles separated by a visible vertical
    gap.  The left semi-circle contains a small hue-spectrum gradient rectangle; the
    right semi-circle shows an `A` in the current font.
  - Each hub semi-circle uses the same restyled border language as the ring: a full
    light-gray perimeter border with a full inset black perimeter border, including the
    straight center edge.  The old green structural outline/divider is absent.
  - Inactive hub halves use a near-neutral light fill; the active hub half uses a
    clearly stronger mint-tinted fill.  Hovering a hub semi-circle keeps the ring
    unchanged and gives that hub half the strongest fill plus a larger outward
    inflation on the curved outer edge only.
  - Ring segments use the same restyled treatment as the non-text wheel: a full
    light-gray perimeter border, a full inset black perimeter border, smaller
    outward-only inflation for the selected segment, and larger inflation for the
    explicitly hovered segment.
  - Color mode (initial): the outer ring shows 8 annotation color segments.  The active
    hub side (color) stands out via the mint fill and the stronger curved-edge
    inflation, but it still does not invert to a dark green fill.
  - Clicking the left hub when already in color mode is a no-op; the wheel stays open.
  - After selecting a color segment, the wheel closes and that color is used by the next
    text draft.
  - Font mode: the outer ring changes to 4 font segments each labeled with a black `A`
    in its font.  The inactive hub glyphs look intentionally dimmer, while the active
    hub glyph stays darker and easier to read.
  - The same dimming rule applies to non-text hub content: inactive hue-strip and
    opacity icons look toned down, while the active hub content is full strength.
    The highlighter opacity icon is a generated rectangular strip, like the hue strip,
    using black bands with varying alpha instead of colored bands or a bitmap.
  - After selecting a font segment, the wheel closes and that font is used by the next
    text draft.  The right-hub `A` updates when the wheel is reopened.
  - `Esc` closes the wheel without changing color or font.
  - Reopening the wheel opens in the mode last used in the session (font mode if font
    was the last mode selected).
  - Right-click while a text draft is active does not open the style wheel.
  - Non-text annotation selection wheels show no hub; only a plain color ring.

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

### GF-MAN-ANN-012A - Shift+Down On Last Line Selects To End

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a selection, activate the Text tool, and type a multi-line draft (e.g. two lines using `Ctrl + Enter`).
  2. Click to place the caret somewhere in the middle of the last line (not at the end).
  3. Press `Shift + Down`.
- Expected:
  - The selection extends to the very end of the last line (i.e. the entire remaining text on the last line becomes selected).
  - The caret is positioned at the end of the text.
- Regression: previously `Shift + Down` on the last line did not move the caret, so nothing was selected.

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
  3. With a live draft still visible, save or copy the selection.
- Expected:
  - Committed text annotations can be selected, moved, and deleted, and selection shows a clockwise animated marquee around the text visual bounds.
  - Once selected, dragging from transparent pixels inside the text marquee still moves the whole annotation.
  - Saved or copied output includes committed text annotations.
  - Saved or copied output excludes live draft caret, selection highlight, and other draft chrome.

### GF-MAN-ANN-014A - Committed Text Re-editing Via Double-Click

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create and commit a text annotation with multi-word text (e.g. "hello world").
  2. With no annotation tool active, single-click the annotation to select it.
  3. Hover the cursor over the selected annotation and verify the cursor changes to an I-beam.
  4. Double-click somewhere in the middle of the text (e.g. between "hello" and "world").
  5. Verify the text editing mode is entered with the caret placed near the click position.
  6. Edit the text (add, delete, or replace characters) and commit by clicking outside.
  7. Verify the annotation now shows the updated text and undo restores the previous text.
  8. Undo again to verify the original create is also undone.
  9. Create a text annotation, then multi-select it together with another annotation.
  10. Double-click the text annotation while in multi-select.
- Expected:
  - The I-beam cursor appears when hovering over the body of a single-selected text annotation (no active tool).
  - Double-clicking a text annotation (selected or unselected) enters text editing mode directly.
  - The text caret appears near the click position; clicking past end-of-line places the caret at the end of that line.
  - Committing the re-edit creates a new undo entry; undoing restores the previous text, and undoing again removes the annotation.
  - Double-clicking a text annotation that is part of a multi-selection does not enter text editing mode.
  - Double-clicking a non-text annotation (line, arrow, rectangle, etc.) does not enter text editing mode.

### GF-MAN-ANN-022 - Spell-Check Squiggles During Text Editing

- Priority: `P1`
- Run on: `ENV-A`

#### Basic spell-check (single language)

- Prerequisite: Set `"spell_check_languages": ["en-US"]` under `tools.text` in the config file. No restart required; the change is picked up automatically within ~400 ms.
- Steps:
  1. Activate the Text tool and click to start a new annotation.
  2. Type a deliberately misspelled word such as `helo wrold`.
  3. Observe the draft annotation while still in editing mode.
  4. Click outside the annotation to commit it.
  5. Inspect the saved or copied output.
  6. Re-open the config, set `"spell_check_languages": []` (empty array), and save.
  7. Repeat steps 1–3.
- Expected:
  - While editing (step 3): red squiggly underlines appear under each misspelled word.
  - After committing (step 4): squiggles are no longer visible on the overlay.
  - In saved/copied output (step 5): no squiggles appear in the image.
  - With no language configured (step 7): no squiggles appear at all.

#### Multi-language intersection

- Prerequisite: Set `"spell_check_languages": ["en-US", "fr-CA"]`.
- Steps:
  1. Activate the Text tool and type a sentence mixing English and French words, e.g. `helo bonjour wrold monde`.
  2. Observe squiggles while still in editing mode.
- Expected:
  - Words that are valid in **either** language (e.g. `bonjour`, `monde`) show no squiggle.
  - Words that are wrong in **all** configured languages (e.g. `helo`, `wrold`) show red squiggles.

#### Hot-reload of languages while app is running

- Steps:
  1. Set `"spell_check_languages": ["en-US"]`, save. Confirm squiggles appear for misspelled English words.
  2. While the app is running, change the config to `"spell_check_languages": ["fr-CA"]` and save.
  3. Wait ~400 ms (no restart), then open a text annotation.
  4. Type a word that is misspelled in French but valid in English (or vice versa).
- Expected: The active language changes without restarting. Squiggle behavior reflects the new language immediately.

#### Invalid language warning

- Steps:
  1. Set `"spell_check_languages": ["xx-XX"]` (unsupported tag) and save while the app is running.
  2. Observe the tray icon area.
  3. Also test at startup: set the same config and launch the app fresh.
- Expected:
  - A tray balloon warning appears identifying the unsupported language tag (e.g. `'xx-XX'`).
  - No crash occurs; spell checking silently does nothing for the unsupported language.
  - The warning also appears shortly after launch when an unsupported language is already in the config.

### GF-MAN-ANN-016 - Selection Wheel Keyboard Navigation (Up/Down arrows)

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate the Brush tool. Right-click the selection to open the selection wheel.
  2. Without moving the mouse, press `↓` once and observe which segment highlights.
  3. Press `↓` several more times and verify the highlight advances clockwise each time.
  4. Press `↑` to navigate counter-clockwise.
  5. Press `↑` past segment 0 and verify the highlight wraps to the last segment.
  6. Press `Enter` to confirm the highlighted segment.
  7. Draw a stroke and verify it uses the newly selected color.
- Expected:
  - Each `↓` press advances the highlight one segment clockwise; each `↑` press
    advances it one segment counter-clockwise.
  - Navigation wraps seamlessly at both ends.
  - Pressing `Enter` selects the highlighted segment, closes the wheel, and subsequent
    annotations use that color.
  - Holding `↓` or `↑` (key repeat) keeps cycling.

### GF-MAN-ANN-017 - Selection Wheel Scroll-Wheel Navigation

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Open the selection wheel with right-click.
  2. Scroll the mouse wheel upward (away from you) several clicks.
  3. Scroll downward (toward you) several clicks past the starting position.
  4. Scroll past the boundary in either direction to verify wrap.
  5. Press `Enter` to confirm, then open the wheel again and verify the selection persists.
- Expected:
  - Scroll up moves the highlight counter-clockwise; scroll down moves it clockwise.
  - Fractional ticks accumulate; the highlight does not jump until a full tick is reached.
  - Wrap is seamless in both directions.
  - `Enter` confirms the navigated segment.

### GF-MAN-ANN-018 - Selection Wheel Nav Hover And Mouse Hover Priority

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Open the selection wheel. Press `↓` twice to establish a nav hover two segments
     clockwise from the current selection.
  2. Move the mouse directly onto a different segment.
  3. Observe which segment is highlighted.
  4. Move the mouse off all segments (into empty space between/outside the ring).
  5. Observe the hover state.
  6. Without moving the mouse, press `↓` once more.
  7. Observe the hover state.
- Expected:
  - Step 2–3: moving the mouse onto a segment immediately clears the nav hover and
    the hovered segment becomes the one under the cursor.
  - Step 4–5: moving the mouse off all segments does **not** clear the hover; the
    previously mouse-hovered segment remains highlighted.
  - Step 6–7: pressing `↓` sets a new nav hover, overriding the mouse position;
    the highlight jumps to the next segment regardless of where the cursor is.

### GF-MAN-ANN-019 - Selection Wheel Tab Shows Wheel And Cycles Views (Text Tool)

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Activate the Text tool (no draft active). Ensure the selection wheel is closed.
  2. Press bare `Tab` (no modifiers) and observe whether the wheel opens.
  3. Press `Tab` again while the wheel is open.
  4. Press `Tab` a third time to verify the view cycles back.
  5. Close the wheel with `Esc`.
  6. Activate the Brush tool and press `Tab`.
  7. While the Brush wheel is open, press `Tab` again.
- Expected:
  - Step 2: the wheel opens at the cursor position.
  - Step 3: the wheel cycles from Color view to Font view (or vice versa); nav hover and
    hub hover are cleared on each cycle.
  - Step 4: the view returns to the view shown in step 2.
  - Step 7: pressing `Tab` on a single-view wheel (Brush) is a no-op — the wheel stays
    open and the view does not change.

### GF-MAN-ANN-020 - Selection Wheel Enter With No Prior Nav/Mouse Hover

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Open the selection wheel with right-click.
  2. Move the mouse entirely off the ring and hub (empty space inside or outside).
  3. Without pressing any navigation key, press `Enter` immediately.
- Expected:
  - The currently selected segment (the one with the smaller selection inflation) is treated as the
    effective hover and is re-confirmed.
  - The wheel closes; no color change occurs (same segment was selected).

### GF-MAN-ANN-021 - Selection Wheel Scroll Remainder Reset On Show/Dismiss

- Priority: `P2`
- Run on: `ENV-A`
- Steps:
  1. Open the selection wheel. Scroll the mouse wheel by a small fractional amount that
     does not yet advance a full segment (only possible on high-resolution scroll devices;
     otherwise scroll exactly one tick and note the starting segment).
  2. Dismiss the wheel with `Esc`.
  3. Reopen the wheel and scroll by the same fractional amount again.
- Expected:
  - The scroll accumulator is reset on dismiss. The second scroll does not carry over the
    remainder from the first session and does not advance the highlight unexpectedly.

### GF-MAN-TXT-RTF-001 - Rich Text Copy And Paste Within Greenflame

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Open a text draft and type mixed-style text: some bold, some italic, some plain.
  2. Select the entire text and press Ctrl+C.
  3. Dismiss the draft and open a new text annotation. Press Ctrl+V.
- Expected:
  - The pasted text reproduces the bold, italic, underline, and strikethrough flags exactly.
  - Font face, color, and size are not imported (Greenflame uses its own text style system).

### GF-MAN-TXT-RTF-002 - Rich Text Cut And Paste Within Greenflame

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Open a text draft with mixed-style content (bold, italic, plain).
  2. Select a styled portion and press Ctrl+X.
  3. Verify the cut range is deleted from the source annotation.
  4. Open a new text annotation and press Ctrl+V.
- Expected:
  - The cut range is removed from the source.
  - The pasted text in the new annotation reproduces the original style flags exactly.

### GF-MAN-TXT-RTF-003 - Paste Rich Text From Word Into Greenflame

- Priority: `P1`
- Run on: `ENV-A`
- Setup: Microsoft Word (or another RTF-capable app) available.
- Steps:
  1. In Word, type text with bold, italic, underline, a custom color, and a large font size applied.
  2. Select it and press Ctrl+C.
  3. In Greenflame, open a text annotation draft and press Ctrl+V.
- Expected:
  - Bold, italic, and underline flags are preserved in the pasted runs.
  - Color and font size are stripped; Greenflame renders using its own text color and size.
  - No crash occurs.

### GF-MAN-TXT-RTF-004 - Copy Rich Text From Greenflame And Paste Into Word

- Priority: `P1`
- Run on: `ENV-A`
- Setup: Microsoft Word (or another RTF-capable app) available.
- Steps:
  1. In Greenflame, create a text draft with bold, italic, underline, and strikethrough runs.
  2. Select all and press Ctrl+C.
  3. Paste into Word.
- Expected:
  - Bold, italic, underline, and strikethrough are visible in Word using Word's default font.

### GF-MAN-TXT-RTF-005 - Paste Plain Text From Notepad Into Greenflame

- Priority: `P2`
- Run on: `ENV-A`
- Steps:
  1. Copy plain text from Notepad (no RTF on clipboard).
  2. Open a Greenflame text draft with bold active.
  3. Press Ctrl+V.
- Expected:
  - Text is inserted using the current typing style (bold in this case).
  - No crash occurs.

### GF-MAN-TXT-RTF-006 - Ctrl+C With No Selection Does Not Modify Clipboard

- Priority: `P2`
- Run on: `ENV-A`
- Steps:
  1. Copy some known text to the clipboard from another app.
  2. Open a Greenflame text draft with no selection (cursor only).
  3. Press Ctrl+C.
  4. Paste into Notepad.
- Expected:
  - The clipboard retains the original text from step 1; Greenflame does not overwrite it.

### GF-MAN-TXT-HTML-001 - Paste Bold/Italic/Underline From Chrome Or Edge

- Priority: `P1`
- Run on: `ENV-A`
- Setup: Google Chrome or Microsoft Edge available.
- Steps:
  1. In a Google Doc (via Chrome or Edge), type text with bold, italic, and underline applied to separate words. Select it and press Ctrl+C.
  2. In Greenflame, open a text annotation draft and press Ctrl+V.
- Expected:
  - Bold, italic, and underline flags appear in the pasted runs, matching the source.
  - Font face, size, and color are not imported.
  - No crash occurs.

### GF-MAN-TXT-HTML-002 - Paste Strikethrough From Google Docs

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. In Google Docs, apply strikethrough to a word. Select and copy.
  2. Paste into a Greenflame text annotation.
- Expected:
  - The strikethrough flag is present on the pasted run.

### GF-MAN-TXT-HTML-003 - Paste From Firefox (text/html Fallback)

- Priority: `P2`
- Run on: `ENV-A`
- Setup: Mozilla Firefox available.
- Steps:
  1. In Firefox, select styled text (bold + italic) from any web page and press Ctrl+C.
  2. Paste into a Greenflame text annotation.
- Expected:
  - Bold and italic flags are preserved.
  - No crash occurs.

### GF-MAN-TXT-HTML-004 - HTML Paste Falls Back To Plain Text When No Style Present

- Priority: `P2`
- Run on: `ENV-A`
- Steps:
  1. In a browser, copy plain, unstyled text from a web page.
  2. Paste into a Greenflame text annotation.
- Expected:
  - Text is inserted with the current typing style (no crash, no garbage).

### GF-MAN-TXT-HTML-005 - Copy From Greenflame And Paste Into Google Docs

- Priority: `P1`
- Run on: `ENV-A`
- Setup: Google Docs open in Chrome or Edge.
- Steps:
  1. In Greenflame, create a text annotation with bold, italic, underline, and strikethrough runs.
  2. Select all and press Ctrl+C.
  3. Paste into a Google Doc.
- Expected:
  - Bold, italic, underline, and strikethrough are visible in Google Docs using its default font.
  - No extra formatting (no colored text, no unexpected font size change).

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
  - Diagonal and curved annotation edges remain anti-aliased in the pasted image and match the live overlay appearance.
  - Highlighter annotations remain semi-transparent in the pasted image and preserve the marker-like darkening effect over the captured content.
  - Highlighter edges remain anti-aliased in the pasted image.
  - Overlay chrome, labels, toolbar, help, and selection wheel are absent from the pasted image.

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
  - The saved image preserves the live overlay's anti-aliased rendering for brush, line, arrow, ellipse, and filled ellipse annotations.
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
  - Each saved format preserves the same anti-aliased annotation rendering seen in the live overlay.
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

### GF-MAN-OUT-006 - Ctrl Window Export Includes Off-Screen Pixels

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Use interactive `Ctrl` window selection on a target that extends slightly off-screen and add visible annotations near the visible edge.
  2. Save once, copy once, and pin once.
  3. Repeat with the same selection after manually moving it.
  4. Repeat with the same selection after manually resizing it.
- Expected:
  - Before any manual move or resize, save/copy/pin include the full captured window pixels, including the off-screen portion.
  - The visible annotations appear at the correct offset inside the full exported image.
  - After a manual move or resize, the selection falls back to ordinary region semantics and subsequent save/copy/pin use only the reshaped visible region.

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
     Line, Arrow, Rectangle, Bubble, Obfuscate, and Text.
  2. Switch back through those tools during the same capture and note the displayed
     size step and visible preview size for each one.
  3. Close the overlay and exit the app.
  4. Relaunch Greenflame, start a fresh capture, and revisit the same tools.
- Expected:
  - Each tool keeps its own independent size step instead of reusing one shared value.
  - Switching tools within the same session restores that tool's last-used size step immediately.
  - After relaunch, each tool restores the same size step it had before exit.
  - Obfuscate restores its previous `block_size`, including blur mode when it was left at `1`.
  - Filled Rectangle still ignores size stepping.

### GF-MAN-CFG-006 - Tool Color Persistence

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Change the brush color slot and the highlighter color slot.
  2. Close the overlay and exit the app.
  3. Relaunch and start a fresh capture.
  4. Open the brush selection wheel and then the highlighter selection wheel.
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

### GF-MAN-CFG-007A - Freehand Smoothing Config Behavior

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Set `tools.brush.smoothing_mode="off"` and
     `tools.highlighter.smoothing_mode="off"`, then relaunch Greenflame.
  2. Start a capture and draw one deliberately jagged brush stroke and one
     deliberately jagged freehand highlighter stroke.
  3. Set `tools.brush.smoothing_mode="smooth"` and
     `tools.highlighter.smoothing_mode="smooth"`, then relaunch Greenflame.
  4. Draw comparable brush and freehand highlighter strokes over detailed content.
  5. While drawing with smoothing enabled, watch the live stroke tip near the
     cursor.
  6. Draw a single long freehand highlighter stroke across much of the selection
     and keep moving continuously for several seconds.
  7. With `tools.highlighter.pause_straighten_ms` left enabled, draw another
     highlighter stroke that pauses long enough to straighten.
- Expected:
  - With both smoothing modes set to `off`, both committed strokes preserve the
    current raw freehand path shape.
  - With Brush smoothing set to `smooth`, the committed brush stroke looks visibly
    cleaner while still following the intended path endpoints.
  - With Highlighter smoothing set to `smooth`, the committed freehand highlighter
    stroke looks cleaner without obvious relocation or multiply-dark seams.
  - During the live smoothed preview, the stroke body looks cleaned up while the tip
    stays attached to the cursor instead of lagging behind it.
  - During a long live highlighter preview, the older body stays visually uniform
    instead of growing darker or seamier as the stroke extends, and responsiveness
    does not visibly degrade just because the stroke is longer.
  - Releasing that long highlighter stroke returns control promptly; the overlay does
    not sit unresponsive for multiple seconds while the committed annotation redraws.
  - A straightened highlighter stroke still commits as the same explicit straight bar
    shape rather than being reinterpreted as a smoothed freehand path.

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
  5. Create a pinned image and move or zoom it across the same mixed-DPI boundary.
- Expected:
  - Crosshair, handles, toolbar hit-testing, cursor previews, and selection wheel stay under the pointer.
  - Pinned-image dragging, zooming, and context-menu activation stay under the pointer.
  - No visual offset appears when crossing monitors with different DPI.

### GF-MAN-DPI-002 - Cross-Monitor Selection And Save

- Priority: `P1`
- Run on: `ENV-B`
- Steps:
  1. Create a selection spanning a monitor boundary.
  2. Ensure part of the selection sits on a monitor with negative virtual coordinates.
  3. Add at least one annotation that crosses the boundary, including an obfuscate if practical.
  4. Save or copy the result.
- Expected:
  - The saved image dimensions match the visible selection.
  - No seam, jump, or clipping appears at the monitor boundary.
  - Annotations, obfuscate content, and selection handles remain aligned across the full selection.

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

### GF-MAN-CLI-002A - CLI Captured Cursor Config And Overrides

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
  1. Set `capture.include_cursor=false` in the config.
  2. Run `greenflame.exe --desktop --output <file> --overwrite`.
  3. Run `greenflame.exe --desktop --cursor --output <file> --overwrite`.
  4. Set `capture.include_cursor=true` in the config.
  5. Run `greenflame.exe --desktop --output <file> --overwrite`.
  6. Run `greenflame.exe --desktop --no-cursor --output <file> --overwrite`.
- Expected:
  - Live CLI captures use the persisted config value by default.
  - `--cursor` forces inclusion for that invocation only.
  - `--no-cursor` forces exclusion for that invocation only.
  - Neither CLI override changes the saved config file.

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
  - The saved outputs keep the same anti-aliased annotation edges as interactive export, including the diagonal line in step 1.
  - The output images are written exactly to the requested paths.
  - No tray instance remains running after either one-shot command.

### GF-MAN-CLI-009 - Fixture-Based `--annotate` Coverage

- Priority: `P1`
- Run on: `ENV-A`, `ENV-B`
- Steps:
1. Run `greenflame.exe --region 100,100,240,180 --padding 40 --padding-color "#202020" --output "%TEMP%\greenflame-local-mixed.png" --overwrite --annotate ".\schemas\examples\cli_annotations\local_mixed_edge_cases.json"`.
2. Run `greenflame.exe --desktop --padding 64 --padding-color "#101010" --output "%TEMP%\greenflame-global-padding.png" --overwrite --annotate ".\schemas\examples\cli_annotations\global_padding_edge_cases.json"`.
3. Run `greenflame.exe --region 100,100,240,180 --padding 48 --padding-color "#202020" --output "%TEMP%\greenflame-brush-padding.png" --overwrite --annotate ".\schemas\examples\cli_annotations\brush_padding_edge_cases.json"`.
4. Run `greenflame.exe --region 100,100,240,180 --output "%TEMP%\greenflame-single-obfuscate.png" --overwrite --annotate ".\schemas\examples\cli_annotations\single_obfuscate.json"`.
5. Run `greenflame.exe --region 100,100,240,180 --output "%TEMP%\greenflame-brush-obfuscate.png" --overwrite --annotate ".\schemas\examples\cli_annotations\brush_with_obfuscate_overlay.json"`.
6. Run `greenflame.exe --region 100,100,240,180 --output "%TEMP%\greenflame-obfuscate-stack.png" --overwrite --annotate ".\schemas\examples\cli_annotations\alternating_obfuscate_stack.json"`.
- Expected:
  - The local-space fixture renders the mixed annotation set relative to the capture origin, including text, bubbles, and out-of-bounds geometry.
  - The global-space fixture renders annotations relative to the virtual-desktop origin, including negative or off-screen coordinates where applicable.
  - The brush fixture renders a continuous neon-magenta stroke that starts in the upper-left padding, passes through the captured content, and exits into the lower-right padding without being clipped at the original capture boundary.
  - The single-obfuscate fixture renders one committed obfuscate rectangle using block pixelation over the captured image.
  - The brush-plus-obfuscate fixture renders the brush squiggle first, then obfuscates the covered portion of that squiggle rather than drawing the squiggle on top of the obfuscation.
  - The alternating fixture preserves paint order across `annotation -> obfuscate -> annotation -> obfuscate`, including blur mode when `size == 1` on the final obfuscate.
  - Diagonal and curved annotation edges remain anti-aliased in all six outputs, including where geometry extends into padding.
  - In all six cases, annotations that land in padded areas remain visible on top of the padding color.
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

### GF-MAN-CLI-012 - `--input` Validation And One-Shot Console Behavior

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Run `greenflame.exe --input "%TEMP%\greenflame-source.png"`.
  2. Run `greenflame.exe --input "%TEMP%\greenflame-source.png" --annotate "{\"annotations\":[]}"`.
  3. Run `greenflame.exe --input "%TEMP%\greenflame-source.png" --annotate "{\"coordinate_space\":\"global\",\"annotations\":[]}" --overwrite`.
  4. Run `greenflame.exe --input "%TEMP%\greenflame-source.png" --annotate "{\"annotations\":[]}" --overwrite --cursor`.
  5. Run `greenflame.exe --input "%TEMP%\greenflame-source.png" --annotate "{\"annotations\":[]}" --overwrite --no-cursor`.
  6. After each command, inspect the tray area.
- Expected:
  - Step 1 fails validation because `--annotate` is required.
  - Step 2 fails validation because either `--output` or `--overwrite` is required.
  - Step 3 fails with exit code `14` because `global` coordinates are not supported with `--input`.
  - Steps 4 and 5 fail validation because `--input` is incompatible with `--cursor` and `--no-cursor`.
  - None of the five commands starts or leaves behind a tray instance.

### GF-MAN-CLI-013 - `--input` In-Place Overwrite And Explicit Output

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create an opaque PNG or JPEG input image such as `%TEMP%\greenflame-source.png`.
  2. Run `greenflame.exe --input "%TEMP%\greenflame-source.png" --overwrite --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":40,\"y\":20},\"size\":3}]}"`.
  3. Recreate the original source image, then run `greenflame.exe --input "%TEMP%\greenflame-source.png" --output "%TEMP%\greenflame-annotated.png" --annotate "{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":0,\"y\":0},\"end\":{\"x\":40,\"y\":20},\"size\":3}]}"`.
  4. Recreate the original source image again, then run `greenflame.exe --input "%TEMP%\greenflame-source.jpg" --output "%TEMP%\greenflame-annotated" --annotate "{\"annotations\":[]}" --overwrite`.
- Expected:
  - Step 2 succeeds and overwrites the input file in place.
  - Step 3 succeeds and writes only the explicit output path.
  - Step 4 succeeds and writes `%TEMP%\greenflame-annotated.jpg`, preserving the probed input format for the extensionless explicit output path.
  - The annotated `--input` outputs preserve the same anti-aliased annotation rendering semantics as interactive export.
  - The commands exit without starting a tray instance.

### GF-MAN-CLI-014 - `--input` Transparent Image Rejection

- Priority: `P1`
- Run on: `ENV-A`
- Steps:
  1. Create a PNG input with at least one transparent pixel.
  2. Run `greenflame.exe --input "%TEMP%\greenflame-transparent.png" --overwrite --annotate "{\"annotations\":[]}"`.
- Expected:
  - The command fails with exit code `16`.
  - `stderr` reports that image transparency is not supported with `--input` in V1.
  - The source image is left unchanged.

### GF-MAN-CLI-015 - Uncapturable Window Rejection

- Priority: `P1`
- Run on: `ENV-A`
- Setup: Have Signal Desktop (or any app that sets `WDA_EXCLUDEFROMCAPTURE`) running and visible.
- Steps:
  1. Run `greenflame.exe --window "Signal" --output "%TEMP%\signal.png"`.
  2. Run `greenflame.exe --window "Signal" --output "%TEMP%\signal.png"` while another window with "Signal" in its title is also open (e.g. "Signal Beta").
  3. Note the HWND printed in the candidate list from step 2, then run `greenflame.exe --window-hwnd <hex-hwnd> --output "%TEMP%\signal.png"` using the Signal HWND.
  4. Start an interactive capture and move the cursor over the Signal window; try Ctrl+click on it.
  5. Start an interactive capture and drag the selection border near the Signal window edge.
- Expected:
  1. Exits with code `17`. `stderr` says the window is protected from screen capture by the application.
  2. Exits with code `17`. `stderr` includes `[uncapturable]` on the Signal candidate line and lists the other candidate without the marker.
  3. Exits with code `17`. `stderr` says the window is protected from screen capture by the application.
  4. Ctrl+click does not select the Signal window; either the window behind it is selected or no window preview appears.
  5. Signal's window edges do not appear as snap candidates during border dragging.
