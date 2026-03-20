#pragma once

namespace greenflame::core {
// Physical pixels (the only internal truth).

struct PointPx final {
    int32_t x{0};
    int32_t y{0};

    constexpr bool operator==(const PointPx &) const noexcept = default;
};

struct SizePx final {
    int32_t w{0};
    int32_t h{0};

    [[nodiscard]] constexpr bool Is_empty() const noexcept { return w <= 0 || h <= 0; }

    constexpr bool operator==(const SizePx &) const noexcept = default;
};

struct InsetsPx final {
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};

    [[nodiscard]] constexpr bool Is_zero() const noexcept {
        return left == 0 && top == 0 && right == 0 && bottom == 0;
    }

    // Expands (source_w, source_h) by these insets and writes the result to (out_w,
    // out_h). Returns false if any inset is negative, the source size is invalid, or
    // the result overflows int32_t.
    [[nodiscard]] bool Try_expand_size(int32_t source_w, int32_t source_h,
                                       int32_t &out_w, int32_t &out_h) const noexcept;

    constexpr bool operator==(const InsetsPx &) const noexcept = default;
};

struct RectPx final {
    // Stored as left/top/right/bottom in *physical pixels*.
    // Invariant expected by most operations: left <= right, top <= bottom.
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};

    [[nodiscard]] constexpr int32_t Width() const noexcept { return right - left; }
    [[nodiscard]] constexpr int32_t Height() const noexcept { return bottom - top; }
    [[nodiscard]] constexpr bool Is_empty() const noexcept {
        return Width() <= 0 || Height() <= 0;
    }

    // Computes width and height with overflow detection (rects near INT32 extremes).
    // Returns false if either dimension is non-positive or overflows int32_t.
    [[nodiscard]] bool Try_get_size(int32_t &w, int32_t &h) const noexcept;

    [[nodiscard]] constexpr PointPx Top_left() const noexcept { return {left, top}; }
    [[nodiscard]] constexpr PointPx Bottom_right() const noexcept {
        return {right, bottom};
    }

    // Returns a rect with swapped edges if needed so invariants hold.
    [[nodiscard]] RectPx Normalized() const noexcept;

    // Contains is inclusive on left/top and exclusive on right/bottom (common
    // pixel-rect convention).
    [[nodiscard]] bool Contains(PointPx p) const noexcept;

    // Returns intersection if non-empty. Empty intersection -> nullopt.
    [[nodiscard]] static std::optional<RectPx> Intersect(RectPx a, RectPx b) noexcept;

    // Returns the smallest rect containing both (even if one is empty).
    [[nodiscard]] static RectPx Union(RectPx a, RectPx b) noexcept;

    // Clips rect to bounds. If result is empty, returns nullopt.
    [[nodiscard]] static std::optional<RectPx> Clip(RectPx r, RectPx bounds) noexcept;

    constexpr bool operator==(const RectPx &) const noexcept = default;

    // Build from left/top/right/bottom (any order; use Normalized() if invariant
    // needed).
    [[nodiscard]] static constexpr RectPx From_ltrb(int32_t l, int32_t t, int32_t r,
                                                    int32_t b) noexcept {
        return RectPx{l, t, r, b};
    }

    // Normalized rectangle spanning both points (drag-from-any-corner).
    // Width/height may be zero.
    [[nodiscard]] static RectPx From_points(PointPx a, PointPx b) noexcept;
};

// Convenience: build from origin + size.
[[nodiscard]] constexpr RectPx Make_rect_px(PointPx origin, SizePx size) noexcept {
    return RectPx{origin.x, origin.y, origin.x + size.w, origin.y + size.h};
}

// Virtual desktop: build RectPx from virtual-screen metrics (left, top, width,
// height). Used by overlay; virtual desktop may have negative left/top on
// multi-monitor. No Win32; fully unit-testable.
[[nodiscard]] constexpr RectPx
Rect_px_from_virtual_screen_metrics(int32_t left, int32_t top, int32_t width,
                                    int32_t height) noexcept {
    return Make_rect_px(PointPx{left, top}, SizePx{width, height});
}
} // namespace greenflame::core
