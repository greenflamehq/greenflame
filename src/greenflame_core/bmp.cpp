#include "greenflame_core/bmp.h"

namespace greenflame::core {

namespace {

constexpr size_t kFileHeaderSize = 14;
constexpr size_t kInfoHeaderSize = 40;
constexpr uint16_t kBmpMagic = 0x4D42; // 'BM'

#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t bfType = kBmpMagic;
    uint32_t bfSize = 0;
    uint16_t bfReserved1 = 0;
    uint16_t bfReserved2 = 0;
    uint32_t bfOffBits = kFileHeaderSize + kInfoHeaderSize;
};

struct BmpInfoHeader {
    uint32_t biSize = kInfoHeaderSize;
    int32_t biWidth = 0;
    int32_t biHeight = 0; // positive = bottom-up
    uint16_t biPlanes = 1;
    uint16_t biBitCount = 32;
    uint32_t biCompression = 0; // BI_RGB
    uint32_t biSizeImage = 0;
    int32_t biXPelsPerMeter = 0;
    int32_t biYPelsPerMeter = 0;
    uint32_t biClrUsed = 0;
    uint32_t biClrImportant = 0;
};
#pragma pack(pop)

} // namespace

std::vector<uint8_t> Build_bmp_bytes(std::span<const uint8_t> pixels, int width,
                                     int height, int row_bytes) {
    if (width <= 0 || height <= 0 || row_bytes <= 0) return {};
    size_t const image_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(height);
    if (pixels.size() < image_size) return {};

    BmpFileHeader file_header;
    file_header.bfSize =
        static_cast<uint32_t>(kFileHeaderSize + kInfoHeaderSize + image_size);

    BmpInfoHeader info_header;
    info_header.biWidth = width;
    info_header.biHeight = height;
    info_header.biSizeImage = static_cast<uint32_t>(image_size);

    std::vector<uint8_t> out;
    out.reserve(kFileHeaderSize + kInfoHeaderSize + image_size);
    out.resize(kFileHeaderSize + kInfoHeaderSize);
    std::copy_n(reinterpret_cast<uint8_t const *>(&file_header), kFileHeaderSize,
                out.begin());
    std::copy_n(reinterpret_cast<uint8_t const *>(&info_header), kInfoHeaderSize,
                out.begin() + static_cast<std::ptrdiff_t>(kFileHeaderSize));
    out.insert(out.end(), pixels.begin(),
               pixels.begin() + static_cast<std::ptrdiff_t>(image_size));
    return out;
}

namespace {

constexpr size_t kBytesPerPixel = 4;
constexpr uint16_t kBitsPerPixel = 32;

template <typename T> T Read_le(std::span<const uint8_t> bytes, size_t offset) {
    T value{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(T),
                reinterpret_cast<uint8_t *>(&value));
    return value;
}

} // namespace

std::optional<BmpImage> Parse_bmp_bytes(std::span<const uint8_t> bytes) {
    if (bytes.size() < kFileHeaderSize + kInfoHeaderSize) return std::nullopt;
    BmpFileHeader const file_header = Read_le<BmpFileHeader>(bytes, 0);
    BmpInfoHeader const info_header = Read_le<BmpInfoHeader>(bytes, kFileHeaderSize);
    if (file_header.bfType != kBmpMagic || info_header.biSize != kInfoHeaderSize ||
        info_header.biBitCount != kBitsPerPixel || info_header.biCompression != 0 ||
        info_header.biWidth <= 0 || info_header.biHeight <= 0) {
        return std::nullopt;
    }
    size_t const row_bytes = static_cast<size_t>(info_header.biWidth) * kBytesPerPixel;
    size_t const image_size = row_bytes * static_cast<size_t>(info_header.biHeight);
    size_t const offset = file_header.bfOffBits;
    if (offset < kFileHeaderSize + kInfoHeaderSize || offset > bytes.size() ||
        bytes.size() - offset < image_size) {
        return std::nullopt;
    }
    BmpImage image;
    image.width = info_header.biWidth;
    image.height = info_header.biHeight;
    auto const first = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
    image.pixels.assign(first, first + static_cast<std::ptrdiff_t>(image_size));
    return image;
}

std::optional<BmpDiff> Diff_bmp_bytes(std::span<const uint8_t> a,
                                      std::span<const uint8_t> b, int tolerance) {
    std::optional<BmpImage> const image_a = Parse_bmp_bytes(a);
    std::optional<BmpImage> const image_b = Parse_bmp_bytes(b);
    if (!image_a || !image_b || image_a->width != image_b->width ||
        image_a->height != image_b->height) {
        return std::nullopt;
    }
    BmpDiff diff;
    std::span<const uint8_t> const pa(image_a->pixels);
    std::span<const uint8_t> const pb(image_b->pixels);
    for (size_t px = 0; px + kBytesPerPixel <= pa.size(); px += kBytesPerPixel) {
        int pixel_max = 0;
        for (size_t c = 0; c < kBytesPerPixel; ++c) {
            int const delta =
                std::abs(static_cast<int>(pa[px + c]) - static_cast<int>(pb[px + c]));
            pixel_max = std::max(pixel_max, delta);
        }
        if (pixel_max > 0) ++diff.differing_pixels;
        if (pixel_max > tolerance) ++diff.pixels_over_tolerance;
        diff.max_channel_delta = std::max(diff.max_channel_delta, pixel_max);
    }
    return diff;
}

} // namespace greenflame::core
