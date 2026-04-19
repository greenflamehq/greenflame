---
title: Documentation Index
summary: Navigation guide for Greenflame repository documentation outside the core authoritative entry points.
audience:
  - contributors
status: reference
owners:
  - core-team
last_updated: 2026-04-19
tags:
  - docs
  - index
  - navigation
---

# Documentation Index

This file is a navigation index for repository documentation.

The primary authority order remains:

1. [README.md](../README.md)
2. [build.md](build.md)
3. [testing.md](testing.md)

Use the tables below to find the right secondary document quickly.

## Core Docs

| Document | Use when |
| --- | --- |
| [README.md](../README.md) | You need user-facing behavior, CLI examples, configuration keys, shortcuts, or exit-code meaning |
| [build.md](build.md) | You need configure/build presets, toolchain expectations, or formatting/lint policy |
| [testing.md](testing.md) | You need unit-test policy, `ctest` commands, or where new automated coverage belongs |
| [annotation_tools.md](annotation_tools.md) | You need current overlay annotation-tool behavior, shortcuts, or persisted tool settings |
| [manual_test_plan.md](manual_test_plan.md) | You need manual coverage for Win32, overlay, WGC, cursor, or other behavior not fully covered by unit tests |
| [resource_processing.md](resource_processing.md) | You need asset-pipeline rules for embedded runtime resources or alpha-mask toolbar glyphs |
| [coverage.md](coverage.md) | You need LLVM coverage workflow details |
| [flow.dot](flow.dot) | You need the maintained source diagram for flow or architecture visualization |

## Design Docs

| Area | Document | Use when |
| --- | --- | --- |
| Overlay interaction | [design/undo_command_system.md](design/undo_command_system.md) | You need undo-stack ownership, command boundaries, or revert behavior |
| Overlay interaction | [design/wheel_keyboard_navigation.md](design/wheel_keyboard_navigation.md) | You need wheel keyboard navigation behavior or focus and hover rules |
| Overlay interaction | [design/highlighter_opacity_wheel_design.md](design/highlighter_opacity_wheel_design.md) | You need highlighter opacity wheel semantics or preset behavior |
| Overlay interaction | [design/text_style_wheel_redesign.md](design/text_style_wheel_redesign.md) | You need Text or Bubble style-wheel behavior and reopen rules |
| Annotation feature | [design/text_annotation_design.md](design/text_annotation_design.md) | You need text drafting, re-editing, clipboard, or rich-text export behavior |
| Annotation feature | [design/bubble_annotation_design.md](design/bubble_annotation_design.md) | You need Bubble tool behavior, sizing, counters, or rendering details |
| Annotation feature | [design/obfuscate_tool.md](design/obfuscate_tool.md) | You need obfuscate rasterization, preview and commit flow, or config behavior |
| Annotation feature | [design/obfuscate_risk_warning.md](design/obfuscate_risk_warning.md) | You need obfuscate warning, acknowledgment, or CLI rejection behavior |
| Capture and export | [design/captured_cursor_design.md](design/captured_cursor_design.md) | You need captured-cursor persistence, export layering, or pin parity behavior |
| Capture and export | [design/pinned_image_design.md](design/pinned_image_design.md) | You need pinned-image creation, controls, lifetime, or export semantics |
| CLI feature | [design/cli_annotation_design.md](design/cli_annotation_design.md) | You need CLI annotation pipeline ownership, validation, or save-order behavior |
| CLI feature | [design/cli_annotations.md](design/cli_annotations.md) | You need the CLI annotation JSON format, schema rules, or examples |
| CLI feature | [design/cli_input_image_annotation_design.md](design/cli_input_image_annotation_design.md) | You need `--input` plus `--annotate` behavior, overwrite rules, or coordinate semantics |
| CLI feature | [design/capture_padding_design.md](design/capture_padding_design.md) | You need CLI padding parser rules, color precedence, or out-of-bounds fill behavior |
| CLI feature | [design/cli_window_capture.md](design/cli_window_capture.md) | You need public `--window-capture` backend semantics and exit-code behavior |
| CLI feature | [design/cli_window_capture_wgc_design.md](design/cli_window_capture_wgc_design.md) | You need WGC backend architecture, controller fallback rules, or Win32 pipeline details |
