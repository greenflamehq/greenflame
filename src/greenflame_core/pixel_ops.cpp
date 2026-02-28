#include "greenflame_core/pixel_ops.h"

namespace greenflame::core {

namespace {
constexpr int kColorChannelMax = 255;
constexpr float kColorChannelMaxF = static_cast<float>(kColorChannelMax);
} // namespace

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
                            int row_bytes, RectPx rect, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t alpha) noexcept {
    if (width <= 0 || height <= 0 || row_bytes <= 0 || alpha == 0) return;
    float const a = static_cast<float>(alpha) / kColorChannelMaxF;
    float const ia = 1.f - a;
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

} // namespace greenflame::core
