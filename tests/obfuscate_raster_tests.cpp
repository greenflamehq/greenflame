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

namespace {

// An opaque noise source of width x height.
[[nodiscard]] BgraBitmap Make_noise(int32_t width, int32_t height) {
    BgraBitmap bitmap{.width_px = width,
                      .height_px = height,
                      .row_bytes = width * 4,
                      .premultiplied_bgra =
                          std::vector<uint8_t>(static_cast<size_t>(width) * 4u *
                                               static_cast<size_t>(height))};
    constexpr uint32_t multiplier = 2654435761u;
    for (size_t at = 0; at < bitmap.premultiplied_bgra.size(); ++at) {
        bitmap.premultiplied_bgra[at] =
            (at % 4u) == 3u
                ? uint8_t{255}
                : static_cast<uint8_t>((static_cast<uint32_t>(at) * multiplier) >> 24u);
    }
    return bitmap;
}

// The rect of bitmap as its own bitmap.
[[nodiscard]] BgraBitmap Cut(BgraBitmap const &bitmap, RectPx rect) {
    BgraBitmap cut{.width_px = rect.Width(),
                   .height_px = rect.Height(),
                   .row_bytes = rect.Width() * 4,
                   .premultiplied_bgra =
                       std::vector<uint8_t>(static_cast<size_t>(rect.Width()) * 4u *
                                            static_cast<size_t>(rect.Height()))};
    for (int32_t y = 0; y < rect.Height(); ++y) {
        for (int32_t x = 0; x < rect.Width() * 4; ++x) {
            cut.premultiplied_bgra[static_cast<size_t>(y * cut.row_bytes + x)] =
                bitmap.premultiplied_bgra[static_cast<size_t>(
                    (rect.top + y) * bitmap.row_bytes + rect.left * 4 + x)];
        }
    }
    return cut;
}

// source with every pixel outside present made transparent (no source under it), as
// Build_obfuscate_source leaves the part of an obfuscate outside the capture.
[[nodiscard]] BgraBitmap Keep_only(BgraBitmap source, RectPx present) {
    for (int32_t y = 0; y < source.height_px; ++y) {
        for (int32_t x = 0; x < source.width_px; ++x) {
            if (!present.Contains({x, y})) {
                for (int32_t c = 0; c < 4; ++c) {
                    source.premultiplied_bgra[static_cast<size_t>(y * source.row_bytes +
                                                                  x * 4 + c)] = 0;
                }
            }
        }
    }
    return source;
}

// Inside present, raster must equal expected; outside, every byte must be 0.
void Expect_inside_matches(BgraBitmap const &raster, RectPx present,
                           BgraBitmap const &expected) {
    ASSERT_TRUE(raster.Is_valid());
    for (int32_t y = 0; y < raster.height_px; ++y) {
        for (int32_t x = 0; x < raster.width_px; ++x) {
            for (int32_t c = 0; c < 4; ++c) {
                uint8_t const actual = raster.premultiplied_bgra[static_cast<size_t>(
                    y * raster.row_bytes + x * 4 + c)];
                if (present.Contains({x, y})) {
                    uint8_t const want =
                        expected.premultiplied_bgra[static_cast<size_t>(
                            (y - present.top) * expected.row_bytes +
                            (x - present.left) * 4 + c)];
                    ASSERT_EQ(actual, want) << "x=" << x << " y=" << y << " c=" << c;
                } else {
                    ASSERT_EQ(actual, 0) << "x=" << x << " y=" << y << " c=" << c;
                }
            }
        }
    }
}

} // namespace

// An obfuscate that reaches past the capture (dragged off an edge) has no source for
// that part. It must not fail, must not average a fill colour in, and must draw
// nothing there; inside the capture it pixelates what is there.
TEST(obfuscate_raster, PartWithNoSourceStaysTransparent_RightAndBottom) {
    constexpr int32_t block = 8;
    BgraBitmap const source = Make_noise(40, 30);
    // The capture ends inside the obfuscate, off a block boundary.
    RectPx const present = RectPx::From_ltrb(0, 0, 27, 19);
    BgraBitmap const raster = Rasterize_obfuscate(Keep_only(source, present), block);
    // The grid is anchored at the obfuscate's top-left, as for the clipped area.
    Expect_inside_matches(raster, present,
                          Rasterize_obfuscate(Cut(source, present), block));
}

TEST(obfuscate_raster, PartWithNoSourceStaysTransparent_LeftAndTopOnTheGrid) {
    constexpr int32_t block = 8;
    BgraBitmap const source = Make_noise(40, 30);
    RectPx const present = RectPx::From_ltrb(16, 8, 40, 30);
    BgraBitmap const raster = Rasterize_obfuscate(Keep_only(source, present), block);
    Expect_inside_matches(raster, present,
                          Rasterize_obfuscate(Cut(source, present), block));
}

TEST(obfuscate_raster, PartWithNoSourceKeepsTheGridAnchoredAtTheObfuscate) {
    constexpr int32_t block = 8;
    BgraBitmap const source = Make_noise(24, 16);
    // The capture starts 3 px in: the first block column averages only its 5
    // present pixels, and the next blocks sit where they would with no clip.
    RectPx const present = RectPx::From_ltrb(3, 0, 24, 16);
    BgraBitmap const raster = Rasterize_obfuscate(Keep_only(source, present), block);
    BgraBitmap const full = Rasterize_obfuscate(source, block);
    for (int32_t y = 0; y < 16; ++y) {
        for (int32_t x = 0; x < 24; ++x) {
            size_t const at = static_cast<size_t>(y * raster.row_bytes + x * 4);
            if (x < 3) {
                EXPECT_EQ(raster.premultiplied_bgra[at + 3u], 0);
            } else if (x >= block) {
                // Whole blocks: the same as with the source all there.
                EXPECT_EQ(raster.premultiplied_bgra[at], full.premultiplied_bgra[at]);
                EXPECT_EQ(raster.premultiplied_bgra[at + 3u], 255);
            } else {
                // The partial block: the average of its present pixels only.
                uint64_t sum = 0;
                uint64_t count = 0;
                int32_t const cell_top = (y / block) * block;
                for (int32_t sy = cell_top; sy < cell_top + block; ++sy) {
                    for (int32_t sx = 3; sx < block; ++sx) {
                        sum += source.premultiplied_bgra[static_cast<size_t>(
                            sy * source.row_bytes + sx * 4)];
                        ++count;
                    }
                }
                EXPECT_EQ(raster.premultiplied_bgra[at],
                          static_cast<uint8_t>(sum / count));
                EXPECT_EQ(raster.premultiplied_bgra[at + 3u], 255);
            }
        }
    }
}

TEST(obfuscate_raster, PartWithNoSourceStaysTransparent_Blur) {
    BgraBitmap const source = Make_noise(40, 30);
    RectPx const present = RectPx::From_ltrb(5, 4, 33, 30);
    BgraBitmap const raster = Rasterize_obfuscate(Keep_only(source, present), 1);
    Expect_inside_matches(raster, present,
                          Rasterize_obfuscate(Cut(source, present), 1));
}

TEST(obfuscate_raster, NoSourceAtAllDrawsNothing) {
    BgraBitmap const source = Make_noise(12, 10);
    BgraBitmap const raster =
        Rasterize_obfuscate(Keep_only(source, RectPx::From_ltrb(0, 0, 0, 0)), 4);
    ASSERT_TRUE(raster.Is_valid());
    EXPECT_TRUE(std::ranges::all_of(raster.premultiplied_bgra,
                                    [](uint8_t value) { return value == 0; }));
}

TEST(obfuscate_raster, SameObfuscateRaster_RejectsOtherDimensions) {
    // The same bytes read as 2x1 or 1x2 are different rasters.
    ObfuscateAnnotation annotation{};
    Set_obfuscate_raster(annotation,
                         Make_bitmap(2, 1, {RGB(10, 20, 30), RGB(40, 50, 60)}));
    ObfuscateRasterIdentity const uploaded = Obfuscate_raster_identity(annotation);
    annotation.bitmap_width_px = 1;
    annotation.bitmap_height_px = 2;
    annotation.bitmap_row_bytes = 4;
    EXPECT_FALSE(
        Is_same_obfuscate_raster(uploaded, Obfuscate_raster_identity(annotation)));
}
