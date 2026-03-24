#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

// Dims pixels outside the selection rect (halves R,G,B; leaves alpha unchanged).
// Buffer is BGRA, row-major, rowBytes per row. Selection is in pixel coordinates.
void Dim_pixels_outside_rect(std::span<uint8_t> pixels, int width, int height,
                             int row_bytes, RectPx selection) noexcept;

// Alpha-blends a solid color onto the given rect in BGRA pixels.
// alpha 0 = no change, 255 = full overlay color.
void Blend_rect_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                            int row_bytes, RectPx rect, COLORREF color,
                            uint8_t alpha) noexcept;

// Alpha-composites a premultiplied BGRA layer onto an opaque BGRA destination.
// layer_bounds is in destination pixel coordinates and clipped to the destination.
void Blend_premultiplied_layer_onto_opaque_pixels(std::span<uint8_t> pixels, int width,
                                                  int height, int row_bytes,
                                                  std::span<const uint8_t> layer_pixels,
                                                  int layer_row_bytes,
                                                  RectPx layer_bounds) noexcept;

// Applies multiply blending from a premultiplied BGRA layer onto an opaque BGRA
// destination. layer_bounds is in destination pixel coordinates and clipped to the
// destination.
void Multiply_premultiplied_layer_onto_opaque_pixels(
    std::span<uint8_t> pixels, int width, int height, int row_bytes,
    std::span<const uint8_t> layer_pixels, int layer_row_bytes,
    RectPx layer_bounds) noexcept;

} // namespace greenflame::core
