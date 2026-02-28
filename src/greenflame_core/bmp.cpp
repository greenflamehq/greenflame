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

} // namespace greenflame::core
