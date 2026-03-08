---
title: Resource Processing
summary: How to prepare embedded runtime assets such as alpha-mask toolbar glyphs.
audience: contributors
status: reference
owners:
  - core-team
last_updated: 2026-03-08
tags:
  - resources
  - icons
  - imagemagick
---

# Resource Processing

This document describes the workflow for preparing runtime resource assets that are
embedded into `greenflame.exe`.

## Resource location

- Store embedded runtime assets under `resources/`.
- Keep original source artwork when it may be reused or revised later.
- Check in the derived runtime asset that the executable actually embeds.
- Do not introduce build-time image conversion steps for these assets.

## Alpha-mask toolbar glyph workflow

Use this when an icon should be treated purely as transparency and tinted at draw
time by the application.

Current convention:

- black pixels become opaque
- white pixels become transparent
- gray pixels become intermediate alpha

For the brush glyph, the source file is:

- `resources/brush.png`

The derived embedded asset is:

- `resources/brush-mask.png`

Generate the derived asset once with ImageMagick:

```bat
magick resources\brush.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\brush-mask.png
```

What this does:

- converts the source to grayscale
- inverts it so dark source pixels map to strong alpha
- copies that grayscale into the alpha channel
- forces RGB to solid white so the asset is effectively alpha-only for tinting
- strips metadata from the derived asset

## Embedding rule

- Embed the derived asset from `resources/` via `resources/greenflame.rc.in`.
- Load and decode the embedded asset once at runtime, then reuse the cached result.
- Tint the glyph at draw time instead of baking a final color into the asset.

## Future icons

For future toolbar glyphs or similar monochrome assets, follow the same pattern:

1. Add the editable source artwork under `resources/`.
2. Generate a derived alpha-mask asset once with ImageMagick.
3. Check in both the source and the derived asset.
4. Embed the derived asset into the EXE.
5. Render it as a tintable alpha mask at runtime.
