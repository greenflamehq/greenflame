#include "greenflame_core/pixel_ops.h"

namespace greenflame::core {

namespace {
constexpr int kColorChannelMax = 255;
constexpr float kColorChannelMaxF = static_cast<float>(kColorChannelMax);
constexpr uint32_t kAlphaBlendRoundBias = 127;

constexpr uint8_t Colorref_red(COLORREF color) noexcept {
    return static_cast<uint8_t>(color & static_cast<COLORREF>(kColorChannelMax));
}

constexpr uint8_t Colorref_green(COLORREF color) noexcept {
    constexpr int color_green_shift = 8;
    return static_cast<uint8_t>((color >> color_green_shift) &
                                static_cast<COLORREF>(kColorChannelMax));
}

constexpr uint8_t Colorref_blue(COLORREF color) noexcept {
    constexpr int color_blue_shift = 16;
    return static_cast<uint8_t>((color >> color_blue_shift) &
                                static_cast<COLORREF>(kColorChannelMax));
}

[[nodiscard]] uint8_t Blend_premultiplied_channel(uint8_t dst, uint8_t src,
                                                  uint8_t alpha) noexcept {
    uint32_t const dst_term =
        static_cast<uint32_t>(dst) * (static_cast<uint32_t>(kColorChannelMax) - alpha);
    uint32_t const blended =
        static_cast<uint32_t>(src) + ((dst_term + kAlphaBlendRoundBias) / 255u);
    return static_cast<uint8_t>(std::min(blended, static_cast<uint32_t>(255u)));
}

[[nodiscard]] uint8_t Multiply_premultiplied_channel(uint8_t dst, uint8_t src,
                                                     uint8_t alpha) noexcept {
    uint32_t const coefficient = static_cast<uint32_t>(kColorChannelMax) - alpha + src;
    uint32_t const blended =
        static_cast<uint32_t>(dst) * std::min(coefficient, static_cast<uint32_t>(255u));
    return static_cast<uint8_t>((blended + kAlphaBlendRoundBias) / 255u);
}

[[nodiscard]] bool Layer_inputs_are_valid(std::span<uint8_t> pixels, int width,
                                          int height, int row_bytes,
                                          std::span<const uint8_t> layer_pixels,
                                          int layer_row_bytes) noexcept {
    if (width <= 0 || height <= 0 || row_bytes <= 0 || layer_row_bytes <= 0) {
        return false;
    }
    if (row_bytes < width * 4 || layer_row_bytes < width * 4) {
        return false;
    }

    size_t const dest_required =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(height);
    size_t const layer_required =
        static_cast<size_t>(layer_row_bytes) * static_cast<size_t>(height);
    return pixels.size() >= dest_required && layer_pixels.size() >= layer_required;
}

[[nodiscard]] bool Bitmap_inputs_are_valid(std::span<uint8_t> pixels, int width,
                                           int height, int row_bytes,
                                           std::span<const uint8_t> layer_pixels,
                                           int layer_width, int layer_height,
                                           int layer_row_bytes) noexcept {
    if (width <= 0 || height <= 0 || row_bytes <= 0 || layer_width <= 0 ||
        layer_height <= 0 || layer_row_bytes <= 0) {
        return false;
    }
    if (row_bytes < width * 4 || layer_row_bytes < layer_width * 4) {
        return false;
    }

    size_t const dest_required =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(height);
    size_t const layer_required =
        static_cast<size_t>(layer_row_bytes) * static_cast<size_t>(layer_height);
    return pixels.size() >= dest_required && layer_pixels.size() >= layer_required;
}

template <typename BlendPixelFn>
void Composite_premultiplied_layer(std::span<uint8_t> pixels, int width, int height,
                                   int row_bytes, std::span<const uint8_t> layer_pixels,
                                   int layer_row_bytes, RectPx layer_bounds,
                                   BlendPixelFn blend_pixel) noexcept {
    if (!Layer_inputs_are_valid(pixels, width, height, row_bytes, layer_pixels,
                                layer_row_bytes)) {
        return;
    }

    std::optional<RectPx> const clipped =
        RectPx::Intersect(layer_bounds, RectPx::From_ltrb(0, 0, width, height));
    if (!clipped.has_value()) {
        return;
    }

    for (int y = clipped->top; y < clipped->bottom; ++y) {
        size_t const dest_row_offset =
            static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        size_t const layer_row_offset =
            static_cast<size_t>(y) * static_cast<size_t>(layer_row_bytes);
        for (int x = clipped->left; x < clipped->right; ++x) {
            size_t const dest_offset = dest_row_offset + static_cast<size_t>(x) * 4u;
            size_t const layer_offset = layer_row_offset + static_cast<size_t>(x) * 4u;
            uint8_t const alpha = layer_pixels[layer_offset + 3u];
            if (alpha == 0) {
                continue;
            }

            pixels[dest_offset] =
                blend_pixel(pixels[dest_offset], layer_pixels[layer_offset], alpha);
            pixels[dest_offset + 1u] = blend_pixel(
                pixels[dest_offset + 1u], layer_pixels[layer_offset + 1u], alpha);
            pixels[dest_offset + 2u] = blend_pixel(
                pixels[dest_offset + 2u], layer_pixels[layer_offset + 2u], alpha);
            pixels[dest_offset + 3u] = 255u;
        }
    }
}

template <typename BlendPixelFn>
void Composite_premultiplied_bitmap(std::span<uint8_t> pixels, int width, int height,
                                    int row_bytes,
                                    std::span<const uint8_t> layer_pixels,
                                    int layer_width, int layer_height,
                                    int layer_row_bytes, RectPx layer_bounds,
                                    BlendPixelFn blend_pixel) noexcept {
    if (!Bitmap_inputs_are_valid(pixels, width, height, row_bytes, layer_pixels,
                                 layer_width, layer_height, layer_row_bytes)) {
        return;
    }

    RectPx const normalized_bounds = layer_bounds.Normalized();
    std::optional<RectPx> const clipped =
        RectPx::Intersect(normalized_bounds, RectPx::From_ltrb(0, 0, width, height));
    if (!clipped.has_value()) {
        return;
    }

    for (int y = clipped->top; y < clipped->bottom; ++y) {
        int const layer_y = y - normalized_bounds.top;
        if (layer_y < 0 || layer_y >= layer_height) {
            continue;
        }

        size_t const dest_row_offset =
            static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        size_t const layer_row_offset =
            static_cast<size_t>(layer_y) * static_cast<size_t>(layer_row_bytes);
        for (int x = clipped->left; x < clipped->right; ++x) {
            int const layer_x = x - normalized_bounds.left;
            if (layer_x < 0 || layer_x >= layer_width) {
                continue;
            }

            size_t const dest_offset = dest_row_offset + static_cast<size_t>(x) * 4u;
            size_t const layer_offset =
                layer_row_offset + static_cast<size_t>(layer_x) * 4u;
            uint8_t const alpha = layer_pixels[layer_offset + 3u];
            if (alpha == 0) {
                continue;
            }

            pixels[dest_offset] =
                blend_pixel(pixels[dest_offset], layer_pixels[layer_offset], alpha);
            pixels[dest_offset + 1u] = blend_pixel(
                pixels[dest_offset + 1u], layer_pixels[layer_offset + 1u], alpha);
            pixels[dest_offset + 2u] = blend_pixel(
                pixels[dest_offset + 2u], layer_pixels[layer_offset + 2u], alpha);
            pixels[dest_offset + 3u] = 255u;
        }
    }
}
} // namespace

void Force_alpha_opaque(std::span<uint8_t> bgra_pixels) noexcept {
    for (size_t index = 3; index < bgra_pixels.size(); index += 4u) {
        bgra_pixels[index] = 255u;
    }
}

void Dim_pixels_outside_rect(std::span<uint8_t> pixels, int width, int height,
                             int row_bytes, RectPx selection) noexcept {
    if (width <= 0 || height <= 0 || row_bytes <= 0) return;
    for (int y = 0; y < height; ++y) {
        size_t const row_offset =
            static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        for (int x = 0; x < width; ++x) {
            if (x < selection.left || x >= selection.right || y < selection.top ||
                y >= selection.bottom) {
                size_t const off = static_cast<size_t>(x) * 4;
                if (off + 2 < static_cast<size_t>(row_bytes)) {
                    size_t const base = row_offset + off;
                    pixels[base] = static_cast<uint8_t>(pixels[base] >> 1);
                    pixels[base + 1] = static_cast<uint8_t>(pixels[base + 1] >> 1);
                    pixels[base + 2] = static_cast<uint8_t>(pixels[base + 2] >> 1);
                }
            }
        }
    }
}

void Blend_rect_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                            int row_bytes, RectPx rect, COLORREF color,
                            uint8_t alpha) noexcept {
    if (width <= 0 || height <= 0 || row_bytes <= 0 || alpha == 0) return;
    float const a = static_cast<float>(alpha) / kColorChannelMaxF;
    float const ia = 1.f - a;
    int const r = Colorref_red(color);
    int const g = Colorref_green(color);
    int const b = Colorref_blue(color);
    int const x0 = std::max(0, rect.left);
    int const y0 = std::max(0, rect.top);
    int const x1 = std::min(width, rect.right);
    int const y1 = std::min(height, rect.bottom);
    for (int y = y0; y < y1; ++y) {
        size_t const row_offset =
            static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        for (int x = x0; x < x1; ++x) {
            size_t const off = static_cast<size_t>(x) * 4;
            if (off + 2 < static_cast<size_t>(row_bytes)) {
                size_t const base = row_offset + off;
                int const blend_b =
                    static_cast<int>(ia * pixels[base] + a * static_cast<float>(b));
                int const blend_g =
                    static_cast<int>(ia * pixels[base + 1] + a * static_cast<float>(g));
                int const blend_r =
                    static_cast<int>(ia * pixels[base + 2] + a * static_cast<float>(r));
                pixels[base] = static_cast<uint8_t>(
                    blend_b > kColorChannelMax ? kColorChannelMax : blend_b);
                pixels[base + 1] = static_cast<uint8_t>(
                    blend_g > kColorChannelMax ? kColorChannelMax : blend_g);
                pixels[base + 2] = static_cast<uint8_t>(
                    blend_r > kColorChannelMax ? kColorChannelMax : blend_r);
            }
        }
    }
}

void Blend_premultiplied_layer_onto_opaque_pixels(std::span<uint8_t> pixels, int width,
                                                  int height, int row_bytes,
                                                  std::span<const uint8_t> layer_pixels,
                                                  int layer_row_bytes,
                                                  RectPx layer_bounds) noexcept {
    Composite_premultiplied_layer(
        pixels, width, height, row_bytes, layer_pixels, layer_row_bytes, layer_bounds,
        [](uint8_t dst, uint8_t src, uint8_t alpha) noexcept {
            return Blend_premultiplied_channel(dst, src, alpha);
        });
}

void Blend_premultiplied_bitmap_onto_opaque_pixels(
    std::span<uint8_t> pixels, int width, int height, int row_bytes,
    std::span<const uint8_t> layer_pixels, int layer_width, int layer_height,
    int layer_row_bytes, RectPx layer_bounds) noexcept {
    Composite_premultiplied_bitmap(
        pixels, width, height, row_bytes, layer_pixels, layer_width, layer_height,
        layer_row_bytes, layer_bounds,
        [](uint8_t dst, uint8_t src, uint8_t alpha) noexcept {
            return Blend_premultiplied_channel(dst, src, alpha);
        });
}

void Multiply_premultiplied_layer_onto_opaque_pixels(
    std::span<uint8_t> pixels, int width, int height, int row_bytes,
    std::span<const uint8_t> layer_pixels, int layer_row_bytes,
    RectPx layer_bounds) noexcept {
    Composite_premultiplied_layer(
        pixels, width, height, row_bytes, layer_pixels, layer_row_bytes, layer_bounds,
        [](uint8_t dst, uint8_t src, uint8_t alpha) noexcept {
            return Multiply_premultiplied_channel(dst, src, alpha);
        });
}

} // namespace greenflame::core
