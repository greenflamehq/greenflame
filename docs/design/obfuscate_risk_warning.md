---
title: Obfuscate Risk Warning
summary: Adds a first-use warning gate for the Obfuscate tool and persists explicit acknowledgement.
audience: contributors
status: draft
owners:
  - core-team
last_updated: 2026-03-27
tags:
  - overlay
  - annotations
  - tools
  - obfuscate
  - config
---

# Obfuscate Risk Warning

## Overview

When a user tries to activate **Obfuscate** for the first time, Greenflame should
show a modal in-overlay warning before allowing normal use of the tool. The goal is
to make the risk explicit: blur and pixelation are useful for casual concealment, but
they are **not** cryptographically safe redaction.

This warning is inspired by Flameshot's existing pixelation warning. The Greenflame
copy should keep the same core message while fitting Greenflame's overlay UI:

- obfuscation is not a security feature
- hidden content, especially text, may sometimes be recovered
- users who need strong redaction should use a solid opaque shape instead

## Config

Persist the acknowledgement as:

`tools.obfuscate.risk_acknowledged`

Why this name:

- it describes a concrete persisted fact, not a vague consent action
- it stays scoped to the obfuscate tool
- it reads cleanly as a boolean with a default of `false`

Config behavior:

- default: `false`
- parse/serialize as a boolean under `tools.obfuscate`
- only write it when the value is `true`, matching the current "write non-defaults"
  config style
- rejecting the warning or dismissing it with `Esc` does not persist anything

## User Experience

Trigger conditions:

- applies when the user attempts to arm **Obfuscate** from the toolbar button
- applies when the user attempts to arm **Obfuscate** with the hotkey
- applies to CLI commands that would create obfuscate annotations
- does not apply once `tools.obfuscate.risk_acknowledged == true`

Flow:

1. The normal tool-selection path runs, so **Obfuscate** becomes the active tool.
2. If `tools.obfuscate.risk_acknowledged == false`, a modal warning panel appears on
   top of the overlay.
3. While the panel is visible, other overlay interactions are blocked.
4. If the user accepts, the panel closes, the tool remains armed, and the config is
   saved immediately.
5. If the user rejects or presses `Esc`, the panel closes, the Obfuscate tool is
   cleared, and no config value is saved.

Dismissal rules:

- `Esc` means "reject"
- clicking outside the panel is ignored; this warning should require an explicit
  button choice or `Esc`
- the help overlay must not be shown on top of this warning, and this warning should
  win `Esc` priority over normal overlay cancellation

## CLI Behavior

The CLI must remain non-interactive.

If a CLI invocation would create one or more obfuscate annotations and
`tools.obfuscate.risk_acknowledged == false`, the command must fail fast instead of
prompting.

Scope:

- applies to `--annotate` payloads that contain `type: "obfuscate"`
- should also apply to any future CLI obfuscate-producing entry point

CLI failure behavior:

- do not open any UI
- do not prompt on stdin/stdout
- write a clear stderr message telling the user that obfuscation risk must be
  acknowledged in config before CLI use
- mention the same core warning text used by the UI
- include the config key name: `tools.obfuscate.risk_acknowledged`
- include the resolved config file path from the existing app-config path logic
- suggest setting the value to `true` to proceed

Suggested stderr text shape:

> Obfuscate is not a security feature. Pixelated or blurred areas can sometimes be
> reconstructed, especially around text or strong-contrast details. To use obfuscate
> from the CLI, set `tools.obfuscate.risk_acknowledged` to `true` in:
> `<resolved config path>`

If the config path cannot be resolved, the error should still name the required key
and say that the config file path could not be determined.

Recommended new process exit code:

- enum name: `CliObfuscateRiskUnacknowledged`
- numeric value: `18`

Implementation note:

- keep all process exit codes in `src/greenflame_core/process_exit_code.h`
- update the README exit-code table when this is implemented

## Warning Panel

This should use the same D2D/DWrite presentation path as the help overlay: dimmed
backdrop, centered panel, monitor-aware placement, and on-overlay rendering rather
than a native Win32 dialog.

Title:

`⚠️ Warning ⚠️`

Suggested body copy:

> Obfuscate is not a security feature. Pixelated or blurred areas can sometimes be
> reconstructed, especially around text or strong-contrast details. If you need to
> permanently hide sensitive content, use a filled opaque shape instead.

Suggested buttons:

- primary: `I Understand`
- secondary: `Use Another Tool`

Layout:

- two square buttons
- side by side at the bottom of the panel
- same fill/outline/pressed color scheme as the round toolbar buttons

Implementation note:

- do not reuse the round `OverlayButton` class directly for this panel
- extract shared visual styling from `overlay_button.*`, but keep separate hit-test
  geometry for circular toolbar buttons versus rectangular modal buttons

## Architecture

Keep the policy in the existing Win32 overlay layer, not in `greenflame_core`.

Recommended structure:

- add a new warning-panel object in `src/greenflame/win/` beside the existing help
  overlay object
- extract the shared backdrop/panel chrome from `OverlayHelpOverlay` so both help and
  warning can use the same rendering primitives
- keep the warning-specific state local to `OverlayWindow`, because it depends on UI
  modality, button hit-testing, and persisted config save timing

Primary touch points:

- `src/greenflame/win/overlay_window.*`
  - gate first activation of Obfuscate
  - route keyboard and mouse input while the warning is visible
  - save config on acceptance
- `src/greenflame/win/overlay_help_overlay.*`
  - extract shared panel rendering code
- `src/greenflame/win/overlay_button.*`
  - share button colors/pressed styling with a square-button variant
- `src/greenflame_core/app_config.*`
- `src/greenflame_core/app_config_json.*`
  - add `tools.obfuscate.risk_acknowledged`
- `src/greenflame_core/process_exit_code.h`
  - add `CliObfuscateRiskUnacknowledged = 18`
- `src/greenflame_core/app_controller.*`
  - fail CLI annotation flows before capture/save when obfuscate is requested without
    acknowledgement
- `README.md`
  - document the new CLI exit code

## Testing Plan

Automated:

- config parse test for `tools.obfuscate.risk_acknowledged`
- config serialize round-trip test for `tools.obfuscate.risk_acknowledged = true`
- schema validation test that rejects non-boolean values for the new key
- CLI test: obfuscate annotation input fails with exit code `18` when the config flag
  is unset
- CLI test: the stderr message names the config key and config path
- CLI test: obfuscate annotation input succeeds once the config flag is `true`

Manual:

- first activation from the toolbar shows the warning
- first activation from the hotkey shows the warning
- accepting keeps Obfuscate selected and suppresses the warning on later uses
- rejecting clears the active tool and does not persist acknowledgement
- pressing `Esc` while the warning is visible behaves the same as reject
- while the warning is visible, drawing, selection changes, toolbar interaction, and
  help-overlay toggling are blocked
