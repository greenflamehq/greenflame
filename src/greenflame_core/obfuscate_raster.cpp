#include "greenflame_core/obfuscate_raster.h"

namespace greenflame::core {

namespace {

constexpr int32_t kChannelsPerPixel = 4;
constexpr size_t kAlphaChannelOffset = 3;

[[nodiscard]] int32_t Bytes_per_row(int32_t width_px) noexcept {
    return width_px * kChannelsPerPixel;
}

[[nodiscard]] size_t Pixel_offset(int32_t x, int32_t y, int32_t row_bytes) noexcept {
    return static_cast<size_t>(y) * static_cast<size_t>(row_bytes) +
           static_cast<size_t>(x * kChannelsPerPixel);
}

struct BlurScratch {
    // Shared prefix-sum arrays for horizontal and vertical passes (both use these
    // sequentially). Sized to max(width_px, height_px) + 1 of the largest region
    // seen. Index 0 is always 0: zero-initialised at construction, never written by
    // the prefix accumulation loop.
    std::vector<uint64_t> prefix_b;
    std::vector<uint64_t> prefix_g;
    std::vector<uint64_t> prefix_r;
    std::vector<uint64_t> prefix_a;

    // Intermediate pixel buffers for the four-pass blur pipeline.
    // Capacity grows to the largest pixel-buffer size seen; never shrinks.
    BgraBitmap horizontal;
    BgraBitmap vertical;
};

[[nodiscard]] BlurScratch &Get_blur_scratch() noexcept {
    // The scratch buffer is intentionally never destroyed at thread exit: it holds
    // no OS handles, only heap memory, and tearing it down at exit has no value.
    CLANG_WARN_IGNORE_PUSH("-Wexit-time-destructors")
    thread_local BlurScratch scratch;
    CLANG_WARN_IGNORE_POP()
    return scratch;
}

void Box_blur_horizontal(BgraBitmap const &source, int32_t radius,
                         BgraBitmap &destination) {
    destination.width_px = source.width_px;
    destination.height_px = source.height_px;
    destination.row_bytes = source.row_bytes;
    destination.premultiplied_bgra.resize(source.premultiplied_bgra.size());

    if (!source.Is_valid() || radius <= 0) {
        destination = source;
        return;
    }

    BlurScratch &scratch = Get_blur_scratch();
    size_t const required_prefix = static_cast<size_t>(source.width_px) + 1u;
    if (scratch.prefix_b.size() < required_prefix) {
        scratch.prefix_b.resize(required_prefix);
        scratch.prefix_g.resize(required_prefix);
        scratch.prefix_r.resize(required_prefix);
        scratch.prefix_a.resize(required_prefix);
    }

    for (int32_t y = 0; y < source.height_px; ++y) {
        // Index 0 is permanently 0 (set at construction, never written by this loop).
        for (int32_t x = 0; x < source.width_px; ++x) {
            size_t const offset = Pixel_offset(x, y, source.row_bytes);
            size_t const prefix_index = static_cast<size_t>(x) + 1u;
            scratch.prefix_b[prefix_index] =
                scratch.prefix_b[prefix_index - 1u] + source.premultiplied_bgra[offset];
            scratch.prefix_g[prefix_index] = scratch.prefix_g[prefix_index - 1u] +
                                             source.premultiplied_bgra[offset + 1u];
            scratch.prefix_r[prefix_index] = scratch.prefix_r[prefix_index - 1u] +
                                             source.premultiplied_bgra[offset + 2u];
            scratch.prefix_a[prefix_index] =
                scratch.prefix_a[prefix_index - 1u] +
                source.premultiplied_bgra[offset + kAlphaChannelOffset];
        }

        for (int32_t x = 0; x < source.width_px; ++x) {
            int32_t const start_x = std::max(0, x - radius);
            int32_t const end_x = std::min(source.width_px - 1, x + radius);
            uint64_t const sample_count = static_cast<uint64_t>(end_x - start_x) + 1u;
            size_t const start_index = static_cast<size_t>(start_x);
            size_t const end_index = static_cast<size_t>(end_x) + 1u;
            size_t const offset = Pixel_offset(x, y, destination.row_bytes);

            destination.premultiplied_bgra[offset] = static_cast<uint8_t>(
                (scratch.prefix_b[end_index] - scratch.prefix_b[start_index]) /
                sample_count);
            destination.premultiplied_bgra[offset + 1u] = static_cast<uint8_t>(
                (scratch.prefix_g[end_index] - scratch.prefix_g[start_index]) /
                sample_count);
            destination.premultiplied_bgra[offset + 2u] = static_cast<uint8_t>(
                (scratch.prefix_r[end_index] - scratch.prefix_r[start_index]) /
                sample_count);
            destination.premultiplied_bgra[offset + kAlphaChannelOffset] =
                static_cast<uint8_t>(
                    (scratch.prefix_a[end_index] - scratch.prefix_a[start_index]) /
                    sample_count);
        }
    }
}

void Box_blur_vertical(BgraBitmap const &source, int32_t radius,
                       BgraBitmap &destination) {
    destination.width_px = source.width_px;
    destination.height_px = source.height_px;
    destination.row_bytes = source.row_bytes;
    destination.premultiplied_bgra.resize(source.premultiplied_bgra.size());

    if (!source.Is_valid() || radius <= 0) {
        destination = source;
        return;
    }

    BlurScratch &scratch = Get_blur_scratch();
    size_t const required_prefix = static_cast<size_t>(source.height_px) + 1u;
    if (scratch.prefix_b.size() < required_prefix) {
        scratch.prefix_b.resize(required_prefix);
        scratch.prefix_g.resize(required_prefix);
        scratch.prefix_r.resize(required_prefix);
        scratch.prefix_a.resize(required_prefix);
    }

    for (int32_t x = 0; x < source.width_px; ++x) {
        // Index 0 is permanently 0 (set at construction, never written by this loop).
        for (int32_t y = 0; y < source.height_px; ++y) {
            size_t const offset = Pixel_offset(x, y, source.row_bytes);
            size_t const prefix_index = static_cast<size_t>(y) + 1u;
            scratch.prefix_b[prefix_index] =
                scratch.prefix_b[prefix_index - 1u] + source.premultiplied_bgra[offset];
            scratch.prefix_g[prefix_index] = scratch.prefix_g[prefix_index - 1u] +
                                             source.premultiplied_bgra[offset + 1u];
            scratch.prefix_r[prefix_index] = scratch.prefix_r[prefix_index - 1u] +
                                             source.premultiplied_bgra[offset + 2u];
            scratch.prefix_a[prefix_index] =
                scratch.prefix_a[prefix_index - 1u] +
                source.premultiplied_bgra[offset + kAlphaChannelOffset];
        }

        for (int32_t y = 0; y < source.height_px; ++y) {
            int32_t const start_y = std::max(0, y - radius);
            int32_t const end_y = std::min(source.height_px - 1, y + radius);
            uint64_t const sample_count = static_cast<uint64_t>(end_y - start_y) + 1u;
            size_t const start_index = static_cast<size_t>(start_y);
            size_t const end_index = static_cast<size_t>(end_y) + 1u;
            size_t const offset = Pixel_offset(x, y, destination.row_bytes);

            destination.premultiplied_bgra[offset] = static_cast<uint8_t>(
                (scratch.prefix_b[end_index] - scratch.prefix_b[start_index]) /
                sample_count);
            destination.premultiplied_bgra[offset + 1u] = static_cast<uint8_t>(
                (scratch.prefix_g[end_index] - scratch.prefix_g[start_index]) /
                sample_count);
            destination.premultiplied_bgra[offset + 2u] = static_cast<uint8_t>(
                (scratch.prefix_r[end_index] - scratch.prefix_r[start_index]) /
                sample_count);
            destination.premultiplied_bgra[offset + kAlphaChannelOffset] =
                static_cast<uint8_t>(
                    (scratch.prefix_a[end_index] - scratch.prefix_a[start_index]) /
                    sample_count);
        }
    }
}

void Blur_bitmap(BgraBitmap const &source, int32_t radius, BgraBitmap &output) {
    if (!source.Is_valid() || radius <= 0) {
        output = source;
        return;
    }

    BlurScratch &scratch = Get_blur_scratch();
    size_t const pixel_size = source.premultiplied_bgra.size();
    if (scratch.horizontal.premultiplied_bgra.capacity() < pixel_size) {
        scratch.horizontal.premultiplied_bgra.reserve(pixel_size);
    }
    if (scratch.vertical.premultiplied_bgra.capacity() < pixel_size) {
        scratch.vertical.premultiplied_bgra.reserve(pixel_size);
    }

    Box_blur_horizontal(source, radius, scratch.horizontal);
    Box_blur_vertical(scratch.horizontal, radius, scratch.vertical);
    Box_blur_horizontal(scratch.vertical, radius, scratch.horizontal);
    Box_blur_vertical(scratch.horizontal, radius, output);
}

// Pixelates source in blocks of block_size whose grid starts phase_x and phase_y
// pixels before the bitmap's top-left (0 for a grid anchored at it).
[[nodiscard]] BgraBitmap Pixelate_bitmap(BgraBitmap const &source, int32_t block_size,
                                         int32_t phase_x, int32_t phase_y) {
    BgraBitmap result{};
    result.width_px = source.width_px;
    result.height_px = source.height_px;
    result.row_bytes = source.row_bytes;

    if (!source.Is_valid()) {
        return result;
    }

    int32_t const clamped_block_size = Clamp_obfuscate_block_size(block_size);
    if (clamped_block_size == 1) {
        result.premultiplied_bgra.resize(source.premultiplied_bgra.size());
        Blur_bitmap(source, kObfuscateBlurRadiusPx, result);
        return result;
    }

    result.premultiplied_bgra.resize(source.premultiplied_bgra.size());

    for (int32_t cell_y = -phase_y; cell_y < source.height_px;
         cell_y += clamped_block_size) {
        int32_t const cell_top = std::max(cell_y, 0);
        int32_t const cell_bottom =
            std::min(source.height_px, cell_y + clamped_block_size);
        for (int32_t cell_x = -phase_x; cell_x < source.width_px;
             cell_x += clamped_block_size) {
            int32_t const cell_left = std::max(cell_x, 0);
            int32_t const cell_right =
                std::min(source.width_px, cell_x + clamped_block_size);

            uint64_t sum_b = 0;
            uint64_t sum_g = 0;
            uint64_t sum_r = 0;
            uint64_t sum_a = 0;
            uint64_t sample_count = 0;

            for (int32_t y = cell_top; y < cell_bottom; ++y) {
                for (int32_t x = cell_left; x < cell_right; ++x) {
                    size_t const offset = Pixel_offset(x, y, source.row_bytes);
                    sum_b += source.premultiplied_bgra[offset];
                    sum_g += source.premultiplied_bgra[offset + 1u];
                    sum_r += source.premultiplied_bgra[offset + 2u];
                    sum_a += source.premultiplied_bgra[offset + kAlphaChannelOffset];
                    ++sample_count;
                }
            }

            if (sample_count == 0) {
                continue;
            }

            uint8_t const avg_b = static_cast<uint8_t>(sum_b / sample_count);
            uint8_t const avg_g = static_cast<uint8_t>(sum_g / sample_count);
            uint8_t const avg_r = static_cast<uint8_t>(sum_r / sample_count);
            uint8_t const avg_a = static_cast<uint8_t>(sum_a / sample_count);

            for (int32_t y = cell_top; y < cell_bottom; ++y) {
                for (int32_t x = cell_left; x < cell_right; ++x) {
                    size_t const offset = Pixel_offset(x, y, result.row_bytes);
                    result.premultiplied_bgra[offset] = avg_b;
                    result.premultiplied_bgra[offset + 1u] = avg_g;
                    result.premultiplied_bgra[offset + 2u] = avg_r;
                    result.premultiplied_bgra[offset + kAlphaChannelOffset] = avg_a;
                }
            }
        }
    }

    return result;
}

// The pixels of source with any source under them (alpha not 0): a rectangle, since
// only the part of an obfuscate outside the capture has none. nullopt when none has.
[[nodiscard]] std::optional<RectPx> Present_pixel_bounds(BgraBitmap const &source) {
    int32_t left = source.width_px;
    int32_t top = source.height_px;
    int32_t right = 0;
    int32_t bottom = 0;
    for (int32_t y = 0; y < source.height_px; ++y) {
        for (int32_t x = 0; x < source.width_px; ++x) {
            if (source.premultiplied_bgra[Pixel_offset(x, y, source.row_bytes) +
                                          kAlphaChannelOffset] == 0) {
                continue;
            }
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    }
    if (right <= left || bottom <= top) {
        return std::nullopt;
    }
    return RectPx::From_ltrb(left, top, right, bottom);
}

// Copies rect of from into to, or of to into from's place: rows of rect.Width() pixels.
void Copy_rect(BgraBitmap const &from, RectPx from_rect, BgraBitmap &to,
               RectPx to_rect) {
    size_t const row_size = static_cast<size_t>(Bytes_per_row(from_rect.Width()));
    for (int32_t row = 0; row < from_rect.Height(); ++row) {
        size_t const from_at =
            Pixel_offset(from_rect.left, from_rect.top + row, from.row_bytes);
        size_t const to_at =
            Pixel_offset(to_rect.left, to_rect.top + row, to.row_bytes);
        std::copy_n(from.premultiplied_bgra.begin() +
                        static_cast<std::ptrdiff_t>(from_at),
                    row_size,
                    to.premultiplied_bgra.begin() + static_cast<std::ptrdiff_t>(to_at));
    }
}

} // namespace

bool BgraBitmap::Is_valid() const noexcept {
    if (width_px <= 0 || height_px <= 0 || row_bytes < Bytes_per_row(width_px)) {
        return false;
    }
    size_t const required_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(height_px);
    return premultiplied_bgra.size() >= required_size;
}

int32_t Clamp_obfuscate_block_size(int32_t block_size) noexcept {
    return std::clamp(block_size, kObfuscateMinBlockSize, kObfuscateMaxBlockSize);
}

BgraBitmap Rasterize_obfuscate(BgraBitmap const &source, int32_t block_size) {
    if (!source.Is_valid()) {
        return {};
    }
    std::optional<RectPx> const present = Present_pixel_bounds(source);
    RectPx const whole = RectPx::From_ltrb(0, 0, source.width_px, source.height_px);
    if (present == whole) {
        return Pixelate_bitmap(source, block_size, 0, 0);
    }

    // Part of the obfuscate has no source under it: that part stays transparent (it
    // draws nothing), and the rest is pixelated from what exists, on the grid
    // anchored at the obfuscate's top-left. No fill colour is ever averaged in.
    BgraBitmap result{
        .width_px = source.width_px,
        .height_px = source.height_px,
        .row_bytes = Bytes_per_row(source.width_px),
        .premultiplied_bgra =
            std::vector<uint8_t>(static_cast<size_t>(Bytes_per_row(source.width_px)) *
                                     static_cast<size_t>(source.height_px),
                                 0),
    };
    if (!present.has_value()) {
        return result;
    }
    RectPx const part = RectPx::From_ltrb(0, 0, present->Width(), present->Height());
    BgraBitmap inside{
        .width_px = part.Width(),
        .height_px = part.Height(),
        .row_bytes = Bytes_per_row(part.Width()),
        .premultiplied_bgra =
            std::vector<uint8_t>(static_cast<size_t>(Bytes_per_row(part.Width())) *
                                 static_cast<size_t>(part.Height())),
    };
    Copy_rect(source, *present, inside, part);
    int32_t const block = Clamp_obfuscate_block_size(block_size);
    BgraBitmap const raster = Pixelate_bitmap(inside, block_size, present->left % block,
                                              present->top % block);
    Copy_rect(raster, part, result, *present);
    return result;
}

uint64_t Hash_raster_pixels(std::span<const uint8_t> pixels) noexcept {
    // FNV-1a's offset basis, then per 8-byte word a multiply by the 64-bit golden
    // ratio and a fold: cheap next to building the raster it hashes.
    constexpr uint64_t offset_basis = 14695981039346656037ull;
    constexpr uint64_t multiplier = 0x9E3779B97F4A7C15ull;
    constexpr unsigned fold_shift = 32;
    constexpr size_t word_bytes = sizeof(uint64_t);
    uint64_t hash = offset_basis ^ static_cast<uint64_t>(pixels.size());
    size_t at = 0;
    for (; at + word_bytes <= pixels.size(); at += word_bytes) {
        uint64_t word = 0;
        std::span<const uint8_t> const bytes = pixels.subspan(at, word_bytes);
        std::copy_n(bytes.begin(), word_bytes, reinterpret_cast<uint8_t *>(&word));
        hash = (hash ^ word) * multiplier;
        hash ^= hash >> fold_shift;
    }
    for (; at < pixels.size(); ++at) {
        hash = (hash ^ pixels[at]) * multiplier;
        hash ^= hash >> fold_shift;
    }
    return hash == 0 ? 1 : hash;
}

void Set_obfuscate_raster(ObfuscateAnnotation &annotation, BgraBitmap raster) noexcept {
    annotation.bitmap_width_px = raster.width_px;
    annotation.bitmap_height_px = raster.height_px;
    annotation.bitmap_row_bytes = raster.row_bytes;
    annotation.premultiplied_bgra = std::move(raster.premultiplied_bgra);
    annotation.raster_hash = Hash_raster_pixels(annotation.premultiplied_bgra);
}

ObfuscateRasterIdentity
Obfuscate_raster_identity(ObfuscateAnnotation const &annotation) noexcept {
    return ObfuscateRasterIdentity{
        .data = annotation.premultiplied_bgra.data(),
        .size = annotation.premultiplied_bgra.size(),
        .width_px = annotation.bitmap_width_px,
        .height_px = annotation.bitmap_height_px,
        .hash = annotation.raster_hash,
    };
}

bool Is_same_obfuscate_raster(ObfuscateRasterIdentity uploaded,
                              ObfuscateRasterIdentity current) noexcept {
    return current.hash != 0 && uploaded == current;
}

} // namespace greenflame::core
