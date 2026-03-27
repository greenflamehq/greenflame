#include "greenflame_core/pixel_ops.h"
#include "greenflame_core/rect_px.h"

using namespace greenflame::core;

namespace {

constexpr int kBytesPerPixel = 4;
constexpr uint8_t kOpaque = 255;
constexpr uint32_t kBlendRoundBias = 127;

[[nodiscard]] size_t Pixel_offset(int x, int y, int row_bytes) noexcept {
    return static_cast<size_t>(y) * static_cast<size_t>(row_bytes) +
           static_cast<size_t>(x) * static_cast<size_t>(kBytesPerPixel);
}

void Set_bgra_pixel(std::span<uint8_t> pixels, int row_bytes, int x, int y,
                    uint8_t blue, uint8_t green, uint8_t red, uint8_t alpha) noexcept {
    size_t const offset = Pixel_offset(x, y, row_bytes);
    pixels[offset] = blue;
    pixels[offset + 1] = green;
    pixels[offset + 2] = red;
    pixels[offset + 3] = alpha;
}

[[nodiscard]] uint8_t Expected_source_over_channel(uint8_t dst, uint8_t src,
                                                   uint8_t alpha) noexcept {
    uint32_t const dst_term =
        static_cast<uint32_t>(dst) * (static_cast<uint32_t>(kOpaque) - alpha);
    uint32_t const blended =
        static_cast<uint32_t>(src) +
        ((dst_term + kBlendRoundBias) / static_cast<uint32_t>(kOpaque));
    return static_cast<uint8_t>(std::min(blended, static_cast<uint32_t>(kOpaque)));
}

[[nodiscard]] uint8_t Expected_multiply_channel(uint8_t dst, uint8_t src,
                                                uint8_t alpha) noexcept {
    uint32_t const coefficient = static_cast<uint32_t>(kOpaque) - alpha + src;
    uint32_t const blended = static_cast<uint32_t>(dst) *
                             std::min(coefficient, static_cast<uint32_t>(kOpaque));
    return static_cast<uint8_t>((blended + kBlendRoundBias) /
                                static_cast<uint32_t>(kOpaque));
}

} // namespace

TEST(pixel_ops, DimPixelsOutsideRect_4x4Selection) {
    // 4x4 image, rowBytes = 16
    int const w = 4, h = 4, row_bytes = 16;
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes) * h);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 200;     // B
        pixels[i + 1] = 200; // G
        pixels[i + 2] = 200; // R
        pixels[i + 3] = 255;
    }
    RectPx sel = RectPx::From_ltrb(1, 1, 3, 3);

    Dim_pixels_outside_rect(pixels, w, h, row_bytes, sel);

    // Inside (1,1)-(3,3): unchanged 200,200,200
    for (int y = 1; y < 3; ++y) {
        for (int x = 1; x < 3; ++x) {
            size_t off =
                (static_cast<size_t>(y) * row_bytes + static_cast<size_t>(x) * 4);
            EXPECT_EQ(pixels[off], 200);
            EXPECT_EQ(pixels[off + 1], 200);
            EXPECT_EQ(pixels[off + 2], 200);
        }
    }
    // Outside: halved to 100
    EXPECT_EQ(pixels[0], 100);
    EXPECT_EQ(pixels[1], 100);
    EXPECT_EQ(pixels[2], 100);
    EXPECT_EQ(pixels[4], 100);  // (1,0)
    EXPECT_EQ(pixels[12], 100); // (0,1)
}

TEST(pixel_ops, DimPixelsOutsideRect_EmptySelectionDimsAll) {
    int const w = 2, h = 2, row_bytes = 8;
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes) * h, 200);
    for (size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }
    RectPx empty_sel = RectPx::From_ltrb(0, 0, 0, 0);

    Dim_pixels_outside_rect(pixels, w, h, row_bytes, empty_sel);

    for (size_t i = 0; i < pixels.size(); i += 4) {
        EXPECT_EQ(pixels[i], 100);
        EXPECT_EQ(pixels[i + 1], 100);
        EXPECT_EQ(pixels[i + 2], 100);
    }
}

TEST(pixel_ops, BlendRectOntoPixels_FullOpacityOverwrites) {
    int const w = 2, h = 2, row_bytes = 8;
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes) * h);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 0;
        pixels[i + 1] = 0;
        pixels[i + 2] = 0;
        pixels[i + 3] = 255;
    }
    RectPx rect = RectPx::From_ltrb(0, 0, 2, 2);

    // Blend color (r, g, b) = (100, 150, 200) -> BGRA buffer is (B, G, R) = (200, 150,
    // 100)
    Blend_rect_onto_pixels(pixels, w, h, row_bytes, rect, RGB(100, 150, 200), 255);

    for (size_t i = 0; i < pixels.size(); i += 4) {
        EXPECT_EQ(pixels[i], 200);     // B
        EXPECT_EQ(pixels[i + 1], 150); // G
        EXPECT_EQ(pixels[i + 2], 100); // R
    }
}

TEST(pixel_ops, BlendRectOntoPixels_HalfAlphaBlends) {
    int const w = 2, h = 2, row_bytes = 8;
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes) * h);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 0;
        pixels[i + 1] = 0;
        pixels[i + 2] = 0;
        pixels[i + 3] = 255;
    }
    RectPx rect = RectPx::From_ltrb(0, 0, 2, 2);
    Blend_rect_onto_pixels(pixels, w, h, row_bytes, rect, RGB(200, 200, 200), 128);
    // 0.5 * 0 + 0.5 * 200 = 100
    EXPECT_EQ(pixels[0], 100);
    EXPECT_EQ(pixels[1], 100);
    EXPECT_EQ(pixels[2], 100);
}

TEST(pixel_ops, BlendPremultipliedLayerOntoOpaquePixels_HonorsAlphaExtremes) {
    int const width = 2;
    int const height = 1;
    int const row_bytes = width * kBytesPerPixel;
    std::vector<uint8_t> pixels = {
        10, 20, 30, kOpaque, 40, 50, 60, kOpaque,
    };
    std::vector<uint8_t> layer_pixels(static_cast<size_t>(row_bytes) * height, 0);
    Set_bgra_pixel(layer_pixels, row_bytes, 0, 0, 200, 180, 160, 0);
    Set_bgra_pixel(layer_pixels, row_bytes, 1, 0, 7, 8, 9, kOpaque);

    Blend_premultiplied_layer_onto_opaque_pixels(
        pixels, width, height, row_bytes, layer_pixels, row_bytes,
        RectPx::From_ltrb(0, 0, width, height));

    EXPECT_EQ(pixels[0], 10);
    EXPECT_EQ(pixels[1], 20);
    EXPECT_EQ(pixels[2], 30);
    EXPECT_EQ(pixels[3], kOpaque);
    EXPECT_EQ(pixels[4], 7);
    EXPECT_EQ(pixels[5], 8);
    EXPECT_EQ(pixels[6], 9);
    EXPECT_EQ(pixels[7], kOpaque);
}

TEST(pixel_ops, BlendPremultipliedLayerOntoOpaquePixels_UsesSourceOver) {
    int const width = 1;
    int const height = 1;
    int const row_bytes = width * kBytesPerPixel;
    std::vector<uint8_t> pixels = {100, 150, 200, kOpaque};
    std::vector<uint8_t> layer_pixels = {16, 32, 64, 128};

    Blend_premultiplied_layer_onto_opaque_pixels(
        pixels, width, height, row_bytes, layer_pixels, row_bytes,
        RectPx::From_ltrb(0, 0, width, height));

    EXPECT_EQ(pixels[0], Expected_source_over_channel(100, 16, 128));
    EXPECT_EQ(pixels[1], Expected_source_over_channel(150, 32, 128));
    EXPECT_EQ(pixels[2], Expected_source_over_channel(200, 64, 128));
    EXPECT_EQ(pixels[3], kOpaque);
}

TEST(pixel_ops, BlendPremultipliedBitmapOntoOpaquePixels_PositionsLocalBitmap) {
    int const width = 4;
    int const height = 3;
    int const row_bytes = width * kBytesPerPixel;
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes) * height, 20);
    for (size_t index = 3; index < pixels.size(); index += kBytesPerPixel) {
        pixels[index] = kOpaque;
    }

    int const layer_width = 2;
    int const layer_height = 1;
    int const layer_row_bytes = layer_width * kBytesPerPixel;
    std::vector<uint8_t> layer_pixels(
        static_cast<size_t>(layer_row_bytes) * layer_height, 0);
    Set_bgra_pixel(layer_pixels, layer_row_bytes, 0, 0, 5, 6, 7, kOpaque);
    Set_bgra_pixel(layer_pixels, layer_row_bytes, 1, 0, 8, 9, 10, kOpaque);

    Blend_premultiplied_bitmap_onto_opaque_pixels(
        pixels, width, height, row_bytes, layer_pixels, layer_width, layer_height,
        layer_row_bytes, RectPx::From_ltrb(1, 1, 3, 2));

    EXPECT_EQ(pixels[Pixel_offset(0, 1, row_bytes)], 20);
    EXPECT_EQ(pixels[Pixel_offset(0, 1, row_bytes) + 1u], 20);
    EXPECT_EQ(pixels[Pixel_offset(0, 1, row_bytes) + 2u], 20);
    EXPECT_EQ(pixels[Pixel_offset(1, 1, row_bytes)], 5);
    EXPECT_EQ(pixels[Pixel_offset(1, 1, row_bytes) + 1u], 6);
    EXPECT_EQ(pixels[Pixel_offset(1, 1, row_bytes) + 2u], 7);
    EXPECT_EQ(pixels[Pixel_offset(2, 1, row_bytes)], 8);
    EXPECT_EQ(pixels[Pixel_offset(2, 1, row_bytes) + 1u], 9);
    EXPECT_EQ(pixels[Pixel_offset(2, 1, row_bytes) + 2u], 10);
    EXPECT_EQ(pixels[Pixel_offset(3, 1, row_bytes)], 20);
    EXPECT_EQ(pixels[Pixel_offset(3, 1, row_bytes) + 1u], 20);
    EXPECT_EQ(pixels[Pixel_offset(3, 1, row_bytes) + 2u], 20);
}

TEST(pixel_ops, MultiplyPremultipliedLayerOntoOpaquePixels_UsesMultiplyBlend) {
    int const width = 1;
    int const height = 1;
    int const row_bytes = width * kBytesPerPixel;
    std::vector<uint8_t> pixels = {200, 180, 160, kOpaque};
    std::vector<uint8_t> layer_pixels = {0, 64, 128, 128};

    Multiply_premultiplied_layer_onto_opaque_pixels(
        pixels, width, height, row_bytes, layer_pixels, row_bytes,
        RectPx::From_ltrb(0, 0, width, height));

    EXPECT_EQ(pixels[0], Expected_multiply_channel(200, 0, 128));
    EXPECT_EQ(pixels[1], Expected_multiply_channel(180, 64, 128));
    EXPECT_EQ(pixels[2], Expected_multiply_channel(160, 128, 128));
    EXPECT_EQ(pixels[3], kOpaque);
}

TEST(pixel_ops, PremultipliedLayerComposition_ClipsToLayerBounds) {
    int const width = 3;
    int const height = 2;
    int const row_bytes = width * kBytesPerPixel;
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes) * height, 20);
    for (size_t index = 3; index < pixels.size(); index += kBytesPerPixel) {
        pixels[index] = kOpaque;
    }

    std::vector<uint8_t> layer_pixels(static_cast<size_t>(row_bytes) * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Set_bgra_pixel(layer_pixels, row_bytes, x, y, 5, 6, 7, kOpaque);
        }
    }

    Blend_premultiplied_layer_onto_opaque_pixels(pixels, width, height, row_bytes,
                                                 layer_pixels, row_bytes,
                                                 RectPx::From_ltrb(-1, 0, 2, height));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < 2; ++x) {
            size_t const offset = Pixel_offset(x, y, row_bytes);
            EXPECT_EQ(pixels[offset], 5);
            EXPECT_EQ(pixels[offset + 1], 6);
            EXPECT_EQ(pixels[offset + 2], 7);
            EXPECT_EQ(pixels[offset + 3], kOpaque);
        }
    }

    for (int y = 0; y < height; ++y) {
        size_t const offset = Pixel_offset(2, y, row_bytes);
        EXPECT_EQ(pixels[offset], 20);
        EXPECT_EQ(pixels[offset + 1], 20);
        EXPECT_EQ(pixels[offset + 2], 20);
        EXPECT_EQ(pixels[offset + 3], kOpaque);
    }
}

TEST(pixel_ops, PremultipliedLayerComposition_PreservesLayerOrder) {
    int const width = 1;
    int const height = 1;
    int const row_bytes = width * kBytesPerPixel;
    std::vector<uint8_t> pixels = {20, 40, 80, kOpaque};
    std::vector<uint8_t> first_layer = {128, 0, 0, 128};
    std::vector<uint8_t> second_layer = {0, 0, 128, 128};

    Blend_premultiplied_layer_onto_opaque_pixels(
        pixels, width, height, row_bytes, first_layer, row_bytes,
        RectPx::From_ltrb(0, 0, width, height));
    Blend_premultiplied_layer_onto_opaque_pixels(
        pixels, width, height, row_bytes, second_layer, row_bytes,
        RectPx::From_ltrb(0, 0, width, height));

    uint8_t const after_first_blue = Expected_source_over_channel(20, 128, 128);
    uint8_t const after_first_green = Expected_source_over_channel(40, 0, 128);
    uint8_t const after_first_red = Expected_source_over_channel(80, 0, 128);
    uint8_t const expected_blue =
        Expected_source_over_channel(after_first_blue, 0, 128);
    uint8_t const expected_green =
        Expected_source_over_channel(after_first_green, 0, 128);
    uint8_t const expected_red =
        Expected_source_over_channel(after_first_red, 128, 128);

    uint8_t const reverse_first_blue = Expected_source_over_channel(20, 0, 128);
    uint8_t const reverse_first_red = Expected_source_over_channel(80, 128, 128);
    uint8_t const reverse_expected_blue =
        Expected_source_over_channel(reverse_first_blue, 128, 128);
    uint8_t const reverse_expected_red =
        Expected_source_over_channel(reverse_first_red, 0, 128);

    EXPECT_EQ(pixels[0], expected_blue);
    EXPECT_EQ(pixels[1], expected_green);
    EXPECT_EQ(pixels[2], expected_red);
    EXPECT_EQ(pixels[3], kOpaque);
    EXPECT_NE(pixels[0], reverse_expected_blue);
    EXPECT_NE(pixels[2], reverse_expected_red);
}
