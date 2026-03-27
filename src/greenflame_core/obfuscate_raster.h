#pragma once

#include "greenflame_core/obfuscate_annotation_types.h"

namespace greenflame::core {

inline constexpr int32_t kObfuscateMinBlockSize = 1;
inline constexpr int32_t kObfuscateDefaultBlockSize = 10;
inline constexpr int32_t kObfuscateMaxBlockSize = 50;
inline constexpr int32_t kObfuscateBlurRadiusPx = 10;

struct BgraBitmap final {
    int32_t width_px = 0;
    int32_t height_px = 0;
    int32_t row_bytes = 0;
    std::vector<uint8_t> premultiplied_bgra = {};

    [[nodiscard]] bool Is_valid() const noexcept;
    bool operator==(BgraBitmap const &) const noexcept = default;
};

[[nodiscard]] int32_t Clamp_obfuscate_block_size(int32_t block_size) noexcept;

[[nodiscard]] BgraBitmap Rasterize_obfuscate(BgraBitmap const &source,
                                             int32_t block_size);

} // namespace greenflame::core
