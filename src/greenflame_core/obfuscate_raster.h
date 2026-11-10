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

// The obfuscate raster of source: block averages (block_size 1: a blur). Pixels of
// source with alpha 0 have no source under them (the obfuscate reaches past the
// capture): they stay transparent, and the rest is pixelated from what exists alone,
// on the grid anchored at source's top-left.
[[nodiscard]] BgraBitmap Rasterize_obfuscate(BgraBitmap const &source,
                                             int32_t block_size);

// A content hash of raster bytes. Never 0, which marks a raster with no known hash.
[[nodiscard]] uint64_t Hash_raster_pixels(std::span<const uint8_t> pixels) noexcept;

// Stores raster as the obfuscate's pixels, with its hash.
void Set_obfuscate_raster(ObfuscateAnnotation &annotation, BgraBitmap raster) noexcept;

// What an uploaded copy of an obfuscate raster is keyed by: the pixels' address,
// size and dimensions, and their hash, so a freed and reused address holding other
// pixels never matches.
struct ObfuscateRasterIdentity final {
    uint8_t const *data = nullptr;
    size_t size = 0;
    int32_t width_px = 0;
    int32_t height_px = 0;
    uint64_t hash = 0;

    bool operator==(ObfuscateRasterIdentity const &) const noexcept = default;
};

[[nodiscard]] ObfuscateRasterIdentity
Obfuscate_raster_identity(ObfuscateAnnotation const &annotation) noexcept;

// True when an upload made for uploaded can be drawn for current. A raster with no
// hash never matches, so it uploads on every draw.
[[nodiscard]] bool Is_same_obfuscate_raster(ObfuscateRasterIdentity uploaded,
                                            ObfuscateRasterIdentity current) noexcept;

} // namespace greenflame::core
