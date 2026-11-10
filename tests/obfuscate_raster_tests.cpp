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

TEST(obfuscate_raster, HashRasterPixels_FollowsContentAndIsNeverZero) {
    std::vector<uint8_t> const a = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::vector<uint8_t> b = a;
    EXPECT_NE(Hash_raster_pixels(a), 0u);
    EXPECT_NE(Hash_raster_pixels({}), 0u);
    EXPECT_EQ(Hash_raster_pixels(a), Hash_raster_pixels(b));
    b.back() = 13; // the tail past the last whole word counts
    EXPECT_NE(Hash_raster_pixels(a), Hash_raster_pixels(b));
    b = a;
    b.front() = 0;
    EXPECT_NE(Hash_raster_pixels(a), Hash_raster_pixels(b));
}

TEST(obfuscate_raster, SetObfuscateRaster_StoresPixelsWithTheirHash) {
    ObfuscateAnnotation annotation{};
    BgraBitmap raster = Make_bitmap(2, 1, {RGB(10, 20, 30), RGB(40, 50, 60)});
    std::vector<uint8_t> const pixels = raster.premultiplied_bgra;
    Set_obfuscate_raster(annotation, std::move(raster));
    EXPECT_EQ(annotation.bitmap_width_px, 2);
    EXPECT_EQ(annotation.bitmap_height_px, 1);
    EXPECT_EQ(annotation.bitmap_row_bytes, 8);
    EXPECT_EQ(annotation.premultiplied_bgra, pixels);
    EXPECT_EQ(annotation.raster_hash, Hash_raster_pixels(pixels));
}

// The GPU upload of a raster is drawn again only for the very same raster. A raster
// rewritten in place (same storage, same size, the address an upload is keyed by)
// with other pixels must not match: that is what the hash is for.
TEST(obfuscate_raster, SameObfuscateRaster_RejectsOtherPixelsAtTheSameAddress) {
    ObfuscateAnnotation annotation{};
    Set_obfuscate_raster(annotation,
                         Make_bitmap(2, 1, {RGB(10, 20, 30), RGB(40, 50, 60)}));
    ObfuscateRasterIdentity const uploaded = Obfuscate_raster_identity(annotation);
    EXPECT_TRUE(
        Is_same_obfuscate_raster(uploaded, Obfuscate_raster_identity(annotation)));

    uint8_t const *const storage = annotation.premultiplied_bgra.data();
    std::vector<uint8_t> const other =
        Make_bitmap(2, 1, {RGB(11, 20, 30), RGB(40, 50, 60)}).premultiplied_bgra;
    annotation.premultiplied_bgra.assign(other.begin(), other.end());
    annotation.raster_hash = Hash_raster_pixels(annotation.premultiplied_bgra);
    ASSERT_EQ(annotation.premultiplied_bgra.data(), storage);
    EXPECT_FALSE(
        Is_same_obfuscate_raster(uploaded, Obfuscate_raster_identity(annotation)));

    // A copy elsewhere is another upload's business; a raster with no hash never
    // matches.
    ObfuscateAnnotation const copy = annotation;
    EXPECT_FALSE(Is_same_obfuscate_raster(Obfuscate_raster_identity(annotation),
                                          Obfuscate_raster_identity(copy)));
    annotation.raster_hash = 0;
    EXPECT_FALSE(Is_same_obfuscate_raster(Obfuscate_raster_identity(annotation),
                                          Obfuscate_raster_identity(annotation)));
}
