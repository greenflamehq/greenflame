#include "greenflame_core/obfuscate_raster.h"

using namespace greenflame::core;

namespace {

[[nodiscard]] BgraBitmap Make_bitmap(int32_t width_px, int32_t height_px,
                                     std::initializer_list<COLORREF> colors) {
    int32_t const row_bytes = width_px * 4;
    std::vector<COLORREF> const color_values(colors);
    BgraBitmap bitmap{
        .width_px = width_px,
        .height_px = height_px,
        .row_bytes = row_bytes,
        .premultiplied_bgra = std::vector<uint8_t>(
            static_cast<size_t>(row_bytes) * static_cast<size_t>(height_px), 0),
    };

    size_t color_index = 0;
    for (int32_t y = 0; y < height_px; ++y) {
        for (int32_t x = 0; x < width_px; ++x) {
            COLORREF const color = color_values[color_index];
            size_t const offset =
                static_cast<size_t>(y) * static_cast<size_t>(row_bytes) +
                static_cast<size_t>(x) * 4u;
            bitmap.premultiplied_bgra[offset] =
                static_cast<uint8_t>((color >> 16) & 255u);
            bitmap.premultiplied_bgra[offset + 1u] =
                static_cast<uint8_t>((color >> 8) & 255u);
            bitmap.premultiplied_bgra[offset + 2u] = static_cast<uint8_t>(color & 255u);
            bitmap.premultiplied_bgra[offset + 3u] = 255u;
            ++color_index;
        }
    }
    return bitmap;
}

[[nodiscard]] COLORREF Pixel_color(BgraBitmap const &bitmap, int32_t x,
                                   int32_t y) noexcept {
    size_t const offset =
        static_cast<size_t>(y) * static_cast<size_t>(bitmap.row_bytes) +
        static_cast<size_t>(x) * 4u;
    return RGB(bitmap.premultiplied_bgra[offset + 2u],
               bitmap.premultiplied_bgra[offset + 1u],
               bitmap.premultiplied_bgra[offset]);
}

} // namespace

TEST(obfuscate_raster, Pixelate_AveragesWithinEachBlockWithoutBleedingAcrossBlocks) {
    BgraBitmap const source = Make_bitmap(
        4, 2,
        {RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 255),
         RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 255)});

    BgraBitmap const result = Rasterize_obfuscate(source, 2);

    EXPECT_EQ(Pixel_color(result, 0, 0), Pixel_color(result, 1, 0));
    EXPECT_EQ(Pixel_color(result, 0, 1), Pixel_color(result, 1, 1));
    EXPECT_EQ(Pixel_color(result, 2, 0), Pixel_color(result, 3, 0));
    EXPECT_EQ(Pixel_color(result, 2, 1), Pixel_color(result, 3, 1));
    EXPECT_NE(Pixel_color(result, 0, 0), Pixel_color(result, 2, 0));
}

TEST(obfuscate_raster, Pixelate_PreservesPartialEdgeCellsAsIndependentBlocks) {
    BgraBitmap const source =
        Make_bitmap(3, 1, {RGB(255, 0, 0), RGB(0, 0, 255), RGB(0, 255, 0)});

    BgraBitmap const result = Rasterize_obfuscate(source, 2);

    EXPECT_EQ(Pixel_color(result, 0, 0), Pixel_color(result, 1, 0));
    EXPECT_EQ(Pixel_color(result, 2, 0), RGB(0, 255, 0));
}

TEST(obfuscate_raster, BlurMode_PreservesUniformSource) {
    BgraBitmap const source = Make_bitmap(
        4, 4,
        {RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60),
         RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60),
         RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60),
         RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60), RGB(20, 40, 60)});

    BgraBitmap const result = Rasterize_obfuscate(source, 1);

    EXPECT_EQ(result.width_px, source.width_px);
    EXPECT_EQ(result.height_px, source.height_px);
    EXPECT_EQ(Pixel_color(result, 0, 0), RGB(20, 40, 60));
    EXPECT_EQ(Pixel_color(result, 3, 3), RGB(20, 40, 60));
}

TEST(obfuscate_raster, BlurMode_SmoothsNonUniformSourceAndKeepsOpaqueAlpha) {
    BgraBitmap const source =
        Make_bitmap(4, 4,
                    {RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0),
                     RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 255, 255), RGB(0, 0, 0),
                     RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 255, 255), RGB(0, 0, 0),
                     RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0)});

    BgraBitmap const result = Rasterize_obfuscate(source, 1);

    EXPECT_NE(Pixel_color(result, 0, 0), RGB(0, 0, 0));
    EXPECT_NE(Pixel_color(result, 1, 1), RGB(255, 255, 255));
    for (size_t index = 3; index < result.premultiplied_bgra.size(); index += 4u) {
        EXPECT_EQ(result.premultiplied_bgra[index], 255u);
    }
}
