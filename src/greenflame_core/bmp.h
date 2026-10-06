#pragma once

namespace greenflame::core {

// Builds a BMP file (with headers) from a 32bpp BGRA pixel buffer.
// Pixels are assumed in BMP row order: row 0 = bottom of image (as returned
// by GetDIBits with positive biHeight). rowBytes = (width * 4 + 3) & ~3.
// Returns empty vector on invalid input.
[[nodiscard]] std::vector<uint8_t>
Build_bmp_bytes(std::span<const uint8_t> pixels, int width, int height, int row_bytes);

// A 32bpp BMP read back from bytes. Pixels keep the file's row order, packed at
// width * 4 bytes per row.
struct BmpImage final {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels = {};
};

// Parses the format Build_bmp_bytes writes: uncompressed (BI_RGB) 32bpp with a
// 40-byte info header and positive height. Returns nullopt for anything else.
[[nodiscard]] std::optional<BmpImage> Parse_bmp_bytes(std::span<const uint8_t> bytes);

struct BmpDiff final {
    // Pixels where any channel differs at all.
    size_t differing_pixels = 0;
    // Pixels where some channel differs by more than the tolerance.
    size_t pixels_over_tolerance = 0;
    // Largest absolute difference of any channel of any pixel, 0..255.
    int max_channel_delta = 0;
};

// Compares two BMP files pixel by pixel. Returns nullopt if either fails to parse
// or their sizes differ.
[[nodiscard]] std::optional<BmpDiff>
Diff_bmp_bytes(std::span<const uint8_t> a, std::span<const uint8_t> b, int tolerance);

} // namespace greenflame::core
