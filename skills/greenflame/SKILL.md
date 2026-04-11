---
name: greenflame
description: CLI-only Windows screenshot capture and annotation with Greenflame. Use when agent needs to capture a region, window, monitor, or desktop image with `greenflame.exe`; annotate an existing PNG/JPEG/BMP with `--input` and `--annotate`; recover from ambiguous `--window` matches using returned `hwnd`, class, title, and rect data; or run a capture-analyze-annotate workflow that generates Greenflame annotation JSON.
---

# Greenflame

Use the Greenflame CLI only. Do not instruct tray usage, hotkeys, or the interactive overlay.

## Quick Start

- Assume `greenflame.exe` is on `PATH`.
- If it is not on `PATH`, rerun with an explicit executable path.
- Prefer explicit `--output` paths and use `--overwrite` only when replacement is intended.
- Prefer PNG unless the user explicitly wants `jpg/jpeg` or `bmp`.
- Prefer annotation JSON files over inline JSON except for trivial one-line payloads.
- Default to `coordinate_space: "local"`.
- Prefer boxes, arrows, bubbles, and short labels before longer text.
- If text would cover source UI text, prefer adding `--padding` and placing labels in the margin.
- Do not use obfuscate unless the user explicitly asks for concealment. Treat obfuscate as casual concealment, not secure redaction.

## Workflow Selection

### Capture only

Use when the user wants a screenshot without post-processing.

- `--region x,y,w,h` for known physical-pixel desktop bounds when the user wants the pixels currently visible in that rectangle
- `--window "<title>"` for a window by title when title matching is acceptable and the user wants window-capture semantics
- `--window-hwnd 0x...` for an exact window after ambiguity recovery or when the user already provided a handle and wants that specific window
- `--monitor <id>` for a single monitor
- `--desktop` for the full virtual desktop

Do not treat `--region` as a substitute for `--window` or `--window-hwnd`. A region captures the desktop pixels currently visible in that rectangle. A window capture targets a specific window and may behave differently if the window is obscured, off-desktop, moved, or otherwise not equivalent to the current desktop pixels in the matching rect.

### Annotate an existing image

Use `--input <path>` with `--annotate <json|path>` and either `--output` or `--overwrite`.

- `--input` works only with local coordinates.
- `--input` rejects images with non-opaque alpha in v1.
- `--input` cannot be combined with live capture modes, `--window-capture`, `--cursor`, or `--no-cursor`.

### Capture, analyze, then annotate

Use this default sequence for non-trivial agent work:

1. Capture to a file.
2. Inspect the image.
3. Write annotation JSON to a file.
4. Re-run Greenflame with `--input <original-capture> --annotate <json> ...` so the annotation pass uses the exact screenshot that was analyzed.

Do not take a fresh screenshot for the final annotation pass unless the user explicitly asks for a new capture. If the source scene changed between passes, a recapture may no longer match the image that was analyzed or the report being written.

If the image is crowded, add `--padding` before the final annotation pass and place labels in the synthetic margin instead of over source text.

## Window Capture

- Default to `--window-capture auto` for `--window` or `--window-hwnd`.
- Use `wgc` when the user specifically wants the target window itself even if it is obscured or partly off-desktop and you want backend failure surfaced instead of silent GDI fallback.
- Use `gdi` only when the user explicitly wants visible-desktop semantics or is debugging WGC behavior.
- Greenflame already auto-selects a unique case-insensitive exact title match over broader substring matches.
- Do not replace a failed or unsuitable window capture with `--region` just because the current window rect is known. Matching geometry does not make the capture semantics equivalent.

### Ambiguous `--window` recovery

When `--window` returns exit code `7`, parse the candidate list from `stderr`. Candidate lines contain:

`hwnd=0x... class="..." title="..." (x=..., y=..., w=..., h=...)`

They may end with `[uncapturable]`.

Smart retry policy:

- Auto-rerun with `--window-hwnd` only if one candidate clearly matches the user's intent and is not `[uncapturable]`.
- Accept obvious disambiguators from the prompt or current task context: exact app identity, exact title fragment, expected window class, or expected geometry.
- Never auto-pick an `[uncapturable]` candidate.
- If more than one candidate remains plausible, ask the user which `hwnd` to use.

Do not silently substitute a different app window in these cases:

- exit `17` uncapturable window
- exit `13` minimized window
- exit `12` window became unavailable

### Minimized title matches

With `--window-capture auto` or `wgc`, `stderr` may warn that additional matching windows were minimized and skipped.

If all matching title hits are minimized, Greenflame reports minimized-window failure instead of "not found". Treat that as a real target-state issue, not a query typo.

## Annotation Guidance

- Read [references/annotation-language.md](references/annotation-language.md) before generating non-trivial annotation JSON.
- Read [references/capture-workflows.md](references/capture-workflows.md) when selecting a capture mode or recovering from window-targeting failures.
- Read [references/greenflame.annotations.schema.json](references/greenflame.annotations.schema.json) for exact validation rules or when fixing schema failures.
- Reuse [references/examples/](references/examples/) as templates.
- Keep annotations minimal and task-driven.
- Use short text labels. Avoid paragraphs on the image.
- Prefer callouts that point at the target rather than text laid directly over the target.
- Use padding plus out-of-bounds annotation coordinates when you need clean label space.
- Obfuscate only on explicit request, and note the `tools.obfuscate.risk_acknowledged = true` precondition for CLI use.

## Failure Handling

Relevant CLI exits:

- `2` argument or validation failure
- `6` no visible window matched
- `7` window match ambiguous
- `10` output path resolution or reservation failed
- `11` capture or save failed
- `12` matched window became unavailable before capture
- `13` matched window is minimized
- `14` annotation input invalid
- `15` forced `wgc` failed
- `16` input image unreadable or unsupported
- `17` window is excluded from capture
- `18` obfuscate risk not acknowledged

On exit `14`, inspect JSON syntax, schema shape, coordinate-space rules, and font requirements before retrying.
