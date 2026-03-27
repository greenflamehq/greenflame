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

    bool operator==(ObfuscateAnnotation const &) const noexcept = default;
};

} // namespace greenflame::core
