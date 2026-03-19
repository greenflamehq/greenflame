---
title: Resource Processing
summary: How to prepare embedded runtime assets such as alpha-mask toolbar glyphs.
audience: contributors
status: reference
owners:
  - core-team
last_updated: 2026-03-09
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

Examples of source files:

- `resources/brush.png`
- `resources/highlighter.png`
- `resources/line.png`
- `resources/arrow.png`
- `resources/rectangle.png`
- `resources/filled_rectangle.png`
- `resources/ellipse.png`
- `resources/filled_ellipse.png`
- `resources/help.png`
- `resources/bubble.png`

Derived embedded assets follow the same naming pattern:

- `resources/brush-mask.png`
- `resources/highlighter-mask.png`
- `resources/line-mask.png`
- `resources/arrow-mask.png`
- `resources/rectangle-mask.png`
- `resources/filled_rectangle-mask.png`
- `resources/ellipse-mask.png`
- `resources/filled_ellipse-mask.png`
- `resources/help-mask.png`
- `resources/bubble-mask.png`

Generate the derived asset once with ImageMagick:

```bat
magick resources\brush.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\brush-mask.png
```

For example:

```bat
magick resources\highlighter.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\highlighter-mask.png
```

And:

```bat
magick resources\arrow.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\arrow-mask.png
magick resources\rectangle.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\rectangle-mask.png
magick resources\filled_rectangle.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\filled_rectangle-mask.png
magick resources\ellipse.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\ellipse-mask.png
magick resources\filled_ellipse.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\filled_ellipse-mask.png
magick resources\help.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\help-mask.png
magick resources\bubble.png -colorspace Gray -negate -alpha copy -fill white -colorize 100 -strip resources\bubble-mask.png
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
