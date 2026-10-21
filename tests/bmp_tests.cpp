#include "greenflame_core/bmp.h"

using namespace greenflame::core;

TEST(bmp, Build_bmp_bytes_2x2_ValidBMP) {
    int const w = 2, h = 2, row_bytes = 8;
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes) * h, 0x80);

    std::vector<uint8_t> bmp = Build_bmp_bytes(pixels, w, h, row_bytes);

    EXPECT_GE(bmp.size(), 14u + 40u + 16u);
    EXPECT_EQ(bmp[0], 'B');
    EXPECT_EQ(bmp[1], 'M');
    uint32_t const file_size =
        static_cast<uint32_t>(bmp[2]) | (static_cast<uint32_t>(bmp[3]) << 8) |
        (static_cast<uint32_t>(bmp[4]) << 16) | (static_cast<uint32_t>(bmp[5]) << 24);
    EXPECT_EQ(file_size, 14u + 40u + static_cast<size_t>(row_bytes) * h);
    uint32_t const off_bits =
        static_cast<uint32_t>(bmp[10]) | (static_cast<uint32_t>(bmp[11]) << 8) |
        (static_cast<uint32_t>(bmp[12]) << 16) | (static_cast<uint32_t>(bmp[13]) << 24);
    EXPECT_EQ(off_bits, 14u + 40u);
    // Info header: width at 18, height at 22
    int32_t const img_w = static_cast<int32_t>(static_cast<uint8_t>(bmp[18])) |
                          (static_cast<int32_t>(static_cast<uint8_t>(bmp[19])) << 8) |
                          (static_cast<int32_t>(static_cast<uint8_t>(bmp[20])) << 16) |
                          (static_cast<int32_t>(static_cast<uint8_t>(bmp[21])) << 24);
    EXPECT_EQ(img_w, 2);
    int32_t const img_h = static_cast<int32_t>(static_cast<uint8_t>(bmp[22])) |
                          (static_cast<int32_t>(static_cast<uint8_t>(bmp[23])) << 8) |
                          (static_cast<int32_t>(static_cast<uint8_t>(bmp[24])) << 16) |
                          (static_cast<int32_t>(static_cast<uint8_t>(bmp[25])) << 24);
    EXPECT_EQ(img_h, 2);
    // Pixel data follows; first byte of first row = first byte of pixels
    EXPECT_EQ(bmp[54], 0x80);
}

TEST(bmp, Build_bmp_bytes_InvalidInput_ReturnsEmpty) {
    std::vector<uint8_t> small(4, 0);
    EXPECT_TRUE(Build_bmp_bytes(small, 0, 1, 4).empty());
    EXPECT_TRUE(Build_bmp_bytes(small, 1, 0, 4).empty());
    EXPECT_TRUE(Build_bmp_bytes(small, 1, 1, 0).empty());
    EXPECT_TRUE(Build_bmp_bytes(small, 10, 10, 40).empty()); // buffer too small
}

namespace {

constexpr int kDiffWidth = 3;
constexpr int kDiffHeight = 2;
constexpr int kDiffRowBytes = kDiffWidth * 4;
constexpr uint8_t kGrey = 0x80;
constexpr int kChangedDelta = 7;

std::vector<uint8_t> Grey_pixels() {
    return std::vector<uint8_t>(static_cast<size_t>(kDiffRowBytes) * kDiffHeight,
                                kGrey);
}

std::vector<uint8_t> Bmp_of(std::vector<uint8_t> const &pixels) {
    return Build_bmp_bytes(pixels, kDiffWidth, kDiffHeight, kDiffRowBytes);
}

} // namespace

TEST(bmp, Parse_bmp_bytes_RoundTripsBuild) {
    std::vector<uint8_t> pixels = Grey_pixels();
    pixels[5] = 0x11;
    std::optional<BmpImage> const image = Parse_bmp_bytes(Bmp_of(pixels));
    ASSERT_TRUE(image.has_value());
    if (!image.has_value()) {
        return;
    }
    EXPECT_EQ(image->width, kDiffWidth);
    EXPECT_EQ(image->height, kDiffHeight);
    EXPECT_EQ(image->pixels, pixels);
}

TEST(bmp, Parse_bmp_bytes_RejectsGarbageAndTruncated) {
    std::vector<uint8_t> const garbage(64, 0x42);
    EXPECT_FALSE(Parse_bmp_bytes(garbage).has_value());
    std::vector<uint8_t> truncated = Bmp_of(Grey_pixels());
    truncated.pop_back();
    EXPECT_FALSE(Parse_bmp_bytes(truncated).has_value());
}

TEST(bmp, Diff_bmp_bytes_Identical_IsZero) {
    std::vector<uint8_t> const bmp = Bmp_of(Grey_pixels());
    std::optional<BmpDiff> const diff = Diff_bmp_bytes(bmp, bmp, 0);
    ASSERT_TRUE(diff.has_value());
    if (!diff.has_value()) {
        return;
    }
    EXPECT_EQ(diff->differing_pixels, 0u);
    EXPECT_EQ(diff->pixels_over_tolerance, 0u);
    EXPECT_EQ(diff->max_channel_delta, 0);
}

TEST(bmp, Diff_bmp_bytes_OneChangedPixel_CountsOneWithItsDelta) {
    std::vector<uint8_t> changed = Grey_pixels();
    changed[4 + 2] = static_cast<uint8_t>(kGrey + kChangedDelta); // pixel 1, red
    std::optional<BmpDiff> const diff =
        Diff_bmp_bytes(Bmp_of(Grey_pixels()), Bmp_of(changed), 0);
    ASSERT_TRUE(diff.has_value());
    if (!diff.has_value()) {
        return;
    }
    EXPECT_EQ(diff->differing_pixels, 1u);
    EXPECT_EQ(diff->pixels_over_tolerance, 1u);
    EXPECT_EQ(diff->max_channel_delta, kChangedDelta);
}

TEST(bmp, Diff_bmp_bytes_ToleranceEdge) {
    std::vector<uint8_t> changed = Grey_pixels();
    changed[0] = static_cast<uint8_t>(kGrey - kChangedDelta);
    std::vector<uint8_t> const base = Bmp_of(Grey_pixels());
    std::vector<uint8_t> const other = Bmp_of(changed);
    std::optional<BmpDiff> const at = Diff_bmp_bytes(base, other, kChangedDelta);
    ASSERT_TRUE(at.has_value());
    if (!at.has_value()) {
        return;
    }
    EXPECT_EQ(at->differing_pixels, 1u);
    EXPECT_EQ(at->pixels_over_tolerance, 0u);
    std::optional<BmpDiff> const under = Diff_bmp_bytes(base, other, kChangedDelta - 1);
    ASSERT_TRUE(under.has_value());
    if (!under.has_value()) {
        return;
    }
    EXPECT_EQ(under->pixels_over_tolerance, 1u);
}

TEST(bmp, Diff_bmp_bytes_SizeMismatchOrGarbage_IsNullopt) {
    std::vector<uint8_t> const base = Bmp_of(Grey_pixels());
    std::vector<uint8_t> const wider(
        static_cast<size_t>(kDiffRowBytes + 4) * kDiffHeight, kGrey);
    std::vector<uint8_t> const other =
        Build_bmp_bytes(wider, kDiffWidth + 1, kDiffHeight, kDiffRowBytes + 4);
    EXPECT_FALSE(Diff_bmp_bytes(base, other, 0).has_value());
    std::vector<uint8_t> const garbage(64, 0x42);
    EXPECT_FALSE(Diff_bmp_bytes(base, garbage, 0).has_value());
}

// Pixel (x, y) in top-down coordinates of a kDiffWidth x kDiffHeight BMP buffer.
static size_t Pixel_offset(int x, int y) {
    return static_cast<size_t>((kDiffHeight - 1 - y) * kDiffRowBytes + x * 4);
}

TEST(bmp, Diff_bmp_bytes_MaskSkipsPixelsOutsideIt) {
    std::vector<uint8_t> const base = Bmp_of(Grey_pixels());
    std::vector<uint8_t> changed = Grey_pixels();
    changed[Pixel_offset(2, 0)] = static_cast<uint8_t>(kGrey + kChangedDelta);
    std::vector<uint8_t> const other = Bmp_of(changed);

    std::array<RectPx, 1> const left_two = {RectPx::From_ltrb(0, 0, 2, kDiffHeight)};
    std::optional<BmpDiff> const outside = Diff_bmp_bytes(base, other, 0, left_two);
    ASSERT_TRUE(outside.has_value());
    if (outside.has_value()) {
        EXPECT_EQ(outside->differing_pixels, 0u);
    }

    std::array<RectPx, 1> const top_right = {RectPx::From_ltrb(2, 0, 3, 1)};
    std::optional<BmpDiff> const inside = Diff_bmp_bytes(base, other, 0, top_right);
    ASSERT_TRUE(inside.has_value());
    if (inside.has_value()) {
        EXPECT_EQ(inside->differing_pixels, 1u);
        EXPECT_EQ(inside->max_channel_delta, kChangedDelta);
    }

    // The bottom row of the same column does not see the top-row change.
    std::array<RectPx, 1> const bottom_right = {RectPx::From_ltrb(2, 1, 3, 2)};
    std::optional<BmpDiff> const below = Diff_bmp_bytes(base, other, 0, bottom_right);
    ASSERT_TRUE(below.has_value());
    if (below.has_value()) {
        EXPECT_EQ(below->differing_pixels, 0u);
    }
}

TEST(bmp, Diff_bmp_bytes_OverlappingMaskRectsCountOnce) {
    std::vector<uint8_t> const base = Bmp_of(Grey_pixels());
    std::vector<uint8_t> changed = Grey_pixels();
    changed[Pixel_offset(1, 1)] = static_cast<uint8_t>(kGrey + kChangedDelta);
    std::array<RectPx, 2> const overlapping = {RectPx::From_ltrb(0, 0, 2, kDiffHeight),
                                               RectPx::From_ltrb(1, 0, 3, kDiffHeight)};
    std::optional<BmpDiff> const diff =
        Diff_bmp_bytes(base, Bmp_of(changed), 0, overlapping);
    ASSERT_TRUE(diff.has_value());
    if (diff.has_value()) {
        EXPECT_EQ(diff->differing_pixels, 1u);
    }
}
