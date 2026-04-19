---
title: Obfuscate Risk Warning
summary: Current reference for the first-use Obfuscate warning, persisted acknowledgement, and CLI safety gate.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - overlay
  - obfuscate
  - warning
  - config
  - cli
---

# Obfuscate Risk Warning

This document describes the current shipped warning and acknowledgement flow for
the Obfuscate tool.

For the interactive obfuscate tool itself, see [obfuscate_tool.md](obfuscate_tool.md).

## Current Status

The warning flow is already implemented for both:

- interactive overlay activation of the Obfuscate tool
- CLI paths that would produce obfuscate annotations

The persisted acknowledgement key is:

- `tools.obfuscate.risk_acknowledged`

Current default behavior:

- default value is `false`
- the key is serialized only when the value is `true`
- parse/serialize validation is handled in `app_config_json.*`

## Shared Warning Copy

Warning strings are centralized in `src/greenflame_core/obfuscate_risk_warning.h`.

Current values:

- title: `⚠️ Warning ⚠️`
- lead text: obfuscation is not a security feature and may be reversible
- guidance text: use a filled opaque shape for permanent concealment
- accept button: `I Understand`
- reject button: `Use Another Tool`

The same lead/guidance text is also reused for the CLI rejection message.

## Interactive Overlay Flow

### Trigger conditions

The overlay warning is tied to Obfuscate tool activation.

Current behavior:

- the user can arm Obfuscate from the toolbar or hotkey
- once Obfuscate becomes the active tool, `OverlayWindow::Maybe_show_obfuscate_warning()`
  checks the persisted acknowledgement flag
- if acknowledgement is still false, the warning dialog is shown
- once acknowledgement is true, later Obfuscate activations do not show the warning

### Visible-dialog behavior

The dialog is modal within the overlay.

Current behavior while visible:

- `Esc` rejects the warning
- clicking outside the panel does nothing
- other key handling is suppressed
- drawing, selection edits, help overlay toggling, selection-wheel input, and tool
  interactions are blocked

This matches the current manual test plan.

### Accept and reject behavior

The two button outcomes are intentionally asymmetric.

Current accept behavior:

1. hide the warning dialog
2. set `config_->obfuscate_risk_acknowledged = true`
3. normalize and save config immediately
4. leave Obfuscate armed

Current reject behavior:

1. hide the warning dialog
2. route `On_cancel()` through the controller
3. clear the active Obfuscate tool
4. keep the current selection state intact

Because `Esc` maps to reject, pressing `Esc` while the warning is visible clears
Obfuscate but does not clear the capture selection.

## Overlay Presentation And Ownership

### UI object

The visible panel is implemented as `OverlayWarningDialog` in `src/greenflame/win/`.

Current presentation details:

- the panel is shown on the monitor containing the cursor
- layout is centered within the available overlay rect for that monitor
- rectangular buttons are used for accept/reject
- shared panel chrome comes from `overlay_panel_chrome.*`
- the panel is painted through the existing D2D/DWrite overlay path

### Ownership split

Current ownership is intentionally simple:

- `obfuscate_risk_warning.h`
  - owns shared strings and config-key constants
- `OverlayWarningDialog`
  - owns layout, hover state, pressed state, and D2D paint for the panel
- `OverlayWindow`
  - owns visibility state
  - decides when to show the warning
  - handles accept/reject side effects
  - persists config on acceptance

This is already the current architecture; it is no longer a proposal.

## CLI Gate

### Scope

The CLI remains non-interactive.

Current CLI behavior:

- any prepared annotation set containing an obfuscate annotation is rejected when
  `tools.obfuscate.risk_acknowledged` is still `false`
- this applies in both:
  - capture-mode annotation flows
  - input-image annotation flows

### Gate timing

The gate happens after annotation preparation succeeds, but before output path
resolution and image save work continue.

That ordering is current behavior and is covered by existing tests.

### Failure result

Current CLI rejection behavior:

- exit code: `ProcessExitCode::CliObfuscateRiskUnacknowledged`
- numeric value: `18`
- stderr includes:
  - the warning lead text
  - the guidance text
  - the required config key
  - the resolved app-config path when available

If the config path cannot be determined, the message explicitly says so instead of
omitting the instruction.

## Existing Coverage

Key automated coverage already exists in:

- `tests/app_config_tests.cpp`
- `tests/app_controller_tests.cpp`

Covered areas include:

- parsing and serializing `tools.obfuscate.risk_acknowledged`
- rejecting non-boolean config values
- CLI rejection with exit code `18`
- CLI stderr naming the config key
- CLI stderr including either the resolved config path or the "path could not be
  determined" fallback
- successful CLI obfuscate use once acknowledgement is true

Key manual coverage already exists in `docs/manual_test_plan.md`, especially:

- `GF-MAN-UI-003`

Future changes should keep the warning text constants centralized in
`obfuscate_risk_warning.h` and keep the tool/raster behavior in
`obfuscate_tool.md` instead of reintroducing speculative implementation notes here.
