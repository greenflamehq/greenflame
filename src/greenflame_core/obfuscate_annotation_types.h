#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

struct ObfuscateAnnotation final {
    RectPx bounds = {};
    int32_t block_size = 10;
    int32_t bitmap_width_px = 0;
    int32_t bitmap_height_px = 0;
    int32_t bitmap_row_bytes = 0;
    std::vector<uint8_t> premultiplied_bgra = {};
    // Hash_raster_pixels of premultiplied_bgra, set with it by Set_obfuscate_raster; 0
    // when unknown. Draw code keys GPU uploads on it, so it must match the pixels.
    uint64_t raster_hash = 0;

    bool operator==(ObfuscateAnnotation const &) const noexcept = default;
};

} // namespace greenflame::core
