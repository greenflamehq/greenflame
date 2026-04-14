#include "greenflame_core/freehand_smoothing.h"
#include "greenflame_core/profiling.h"

namespace greenflame::core {

namespace {

constexpr int32_t kMinDecimationSpacingPx = 1;
constexpr int32_t kDecimationSpacingDivisor = 3;
constexpr float kSmoothResampleSpacingDivisor = 4.0F;
constexpr float kCornerAnchorCosThreshold = 0.5F;
constexpr float kMinSplineSegmentLength = 0.001F;
constexpr float kCentripetalAlpha = 0.5F;
constexpr int32_t kMinPreviewTailLengthPx = 12;
constexpr int32_t kPreviewTailLengthPerStrokePx = 3;
constexpr int32_t kMaxPreviewTailLengthPx = 48;

struct PointF final {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] PointF To_pointf(PointPx point) noexcept {
    return PointF{static_cast<float>(point.x), static_cast<float>(point.y)};
}

[[nodiscard]] PointPx To_point_px(PointF point) noexcept {
    return PointPx{static_cast<int32_t>(std::lround(point.x)),
                   static_cast<int32_t>(std::lround(point.y))};
}

[[nodiscard]] PointF Lerp(PointF a, PointF b, float t) noexcept {
    return PointF{a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t)};
}

[[nodiscard]] float Distance_between(PointPx a, PointPx b) noexcept {
    float const dx = static_cast<float>(b.x - a.x);
    float const dy = static_cast<float>(b.y - a.y);
    return std::sqrt((dx * dx) + (dy * dy));
}

[[nodiscard]] int64_t Distance_squared(PointPx a, PointPx b) noexcept {
    int64_t const dx = static_cast<int64_t>(b.x) - static_cast<int64_t>(a.x);
    int64_t const dy = static_cast<int64_t>(b.y) - static_cast<int64_t>(a.y);
    return (dx * dx) + (dy * dy);
}

[[nodiscard]] std::vector<PointPx> Deduplicate_points(std::span<const PointPx> points) {
    std::vector<PointPx> deduplicated = {};
    deduplicated.reserve(points.size());
    for (PointPx point : points) {
        if (!deduplicated.empty() && deduplicated.back() == point) {
            continue;
        }
        deduplicated.push_back(point);
    }
    return deduplicated;
}

[[nodiscard]] bool Is_corner_anchor(PointPx prev, PointPx current,
                                    PointPx next) noexcept {
    float const in_x = static_cast<float>(current.x - prev.x);
    float const in_y = static_cast<float>(current.y - prev.y);
    float const out_x = static_cast<float>(next.x - current.x);
    float const out_y = static_cast<float>(next.y - current.y);
    float const in_length = std::sqrt((in_x * in_x) + (in_y * in_y));
    float const out_length = std::sqrt((out_x * out_x) + (out_y * out_y));
    if (in_length < kMinSplineSegmentLength || out_length < kMinSplineSegmentLength) {
        return false;
    }

    float const cosine = ((in_x * out_x) + (in_y * out_y)) / (in_length * out_length);
    return cosine <= kCornerAnchorCosThreshold;
}

[[nodiscard]] std::vector<uint8_t> Build_anchor_flags(std::span<const PointPx> points) {
    std::vector<uint8_t> anchors(points.size(), 0);
    if (points.empty()) {
        return anchors;
    }

    anchors.front() = 1;
    anchors.back() = 1;
    for (size_t index = 1; index + 1 < points.size(); ++index) {
        if (Is_corner_anchor(points[index - 1], points[index], points[index + 1])) {
            anchors[index] = 1;
        }
    }
    return anchors;
}

[[nodiscard]] std::vector<PointPx> Decimate_points(std::span<const PointPx> points,
                                                   int32_t stroke_width_px) {
    if (points.size() <= 2) {
        return {points.begin(), points.end()};
    }

    int32_t const min_spacing_px =
        std::max(kMinDecimationSpacingPx, stroke_width_px / kDecimationSpacingDivisor);
    int64_t const min_spacing_sq =
        static_cast<int64_t>(min_spacing_px) * static_cast<int64_t>(min_spacing_px);
    std::vector<uint8_t> const anchors = Build_anchor_flags(points);

    std::vector<PointPx> decimated = {};
    decimated.reserve(points.size());
    decimated.push_back(points.front());
    size_t last_kept_index = 0;
    for (size_t index = 1; index + 1 < points.size(); ++index) {
        if (anchors[index] != 0 || Distance_squared(points[last_kept_index],
                                                    points[index]) >= min_spacing_sq) {
            decimated.push_back(points[index]);
            last_kept_index = index;
        }
    }
    if (decimated.back() != points.back()) {
        decimated.push_back(points.back());
    }
    return decimated;
}

[[nodiscard]] PointF Mirror_endpoint(PointPx current, PointPx neighbor) noexcept {
    return PointF{
        (2.0F * static_cast<float>(current.x)) - static_cast<float>(neighbor.x),
        (2.0F * static_cast<float>(current.y)) - static_cast<float>(neighbor.y),
    };
}

[[nodiscard]] PointF Catmull_rom_point(PointF p0, PointF p1, PointF p2, PointF p3,
                                       float t) noexcept {
    auto const advance = [](float t_prev, PointF a, PointF b) noexcept {
        float const dx = b.x - a.x;
        float const dy = b.y - a.y;
        float const distance = std::sqrt((dx * dx) + (dy * dy));
        return t_prev +
               std::pow(std::max(distance, kMinSplineSegmentLength), kCentripetalAlpha);
    };

    float const t0 = 0.0F;
    float const t1 = advance(t0, p0, p1);
    float const t2 = advance(t1, p1, p2);
    float const t3 = advance(t2, p2, p3);
    if ((t1 - t0) < kMinSplineSegmentLength || (t2 - t1) < kMinSplineSegmentLength ||
        (t3 - t2) < kMinSplineSegmentLength) {
        return Lerp(p1, p2, t);
    }

    float const sample_t = t1 + ((t2 - t1) * t);
    PointF const a1 = Lerp(p0, p1, (sample_t - t0) / (t1 - t0));
    PointF const a2 = Lerp(p1, p2, (sample_t - t1) / (t2 - t1));
    PointF const a3 = Lerp(p2, p3, (sample_t - t2) / (t3 - t2));
    PointF const b1 = Lerp(a1, a2, (sample_t - t0) / (t2 - t0));
    PointF const b2 = Lerp(a2, a3, (sample_t - t1) / (t3 - t1));
    return Lerp(b1, b2, (sample_t - t1) / (t2 - t1));
}

void Append_polyline(std::span<const PointPx> points, std::vector<PointPx> &out) {
    for (PointPx point : points) {
        if (!out.empty() && out.back() == point) {
            continue;
        }
        out.push_back(point);
    }
}

void Append_catmull_subpath(std::span<const PointPx> points, float spacing_px,
                            std::vector<PointPx> &out) {
    if (points.empty()) {
        return;
    }
    if (points.size() < 3) {
        Append_polyline(points, out);
        return;
    }

    if (out.empty() || out.back() != points.front()) {
        out.push_back(points.front());
    }

    for (size_t index = 0; index + 1 < points.size(); ++index) {
        PointF const p0 = (index == 0)
                              ? Mirror_endpoint(points[index], points[index + 1])
                              : To_pointf(points[index - 1]);
        PointF const p1 = To_pointf(points[index]);
        PointF const p2 = To_pointf(points[index + 1]);
        PointF const p3 = (index + 2 < points.size())
                              ? To_pointf(points[index + 2])
                              : Mirror_endpoint(points[index + 1], points[index]);
        float const segment_length = Distance_between(points[index], points[index + 1]);
        int32_t const subdivision_count =
            std::max(1, static_cast<int32_t>(std::ceil(segment_length / spacing_px)));

        for (int32_t step = 1; step < subdivision_count; ++step) {
            float const t =
                static_cast<float>(step) / static_cast<float>(subdivision_count);
            PointPx const point = To_point_px(Catmull_rom_point(p0, p1, p2, p3, t));
            if (out.back() != point) {
                out.push_back(point);
            }
        }
        if (out.back() != points[index + 1]) {
            out.push_back(points[index + 1]);
        }
    }
}

[[nodiscard]] std::vector<PointPx> Apply_catmull_rom(std::span<const PointPx> points,
                                                     int32_t stroke_width_px) {
    if (points.size() <= 2) {
        return {points.begin(), points.end()};
    }

    float const spacing_px = std::max(1.0F, static_cast<float>(stroke_width_px) /
                                                kSmoothResampleSpacingDivisor);
    std::vector<uint8_t> const anchors = Build_anchor_flags(points);
    std::vector<PointPx> smoothed = {};
    smoothed.reserve(points.size() * 2);

    size_t subpath_start = 0;
    for (size_t index = 1; index < points.size(); ++index) {
        if (anchors[index] == 0) {
            continue;
        }
        Append_catmull_subpath(
            points.subspan(subpath_start, (index - subpath_start) + 1), spacing_px,
            smoothed);
        subpath_start = index;
    }

    return Deduplicate_points(smoothed);
}

[[nodiscard]] int32_t Preview_tail_length_px(int32_t stroke_width_px) noexcept {
    return std::clamp(stroke_width_px * kPreviewTailLengthPerStrokePx,
                      kMinPreviewTailLengthPx, kMaxPreviewTailLengthPx);
}

[[nodiscard]] size_t Find_preview_tail_start_index(std::span<const PointPx> points,
                                                   int32_t stroke_width_px) {
    if (points.empty()) {
        return 0;
    }

    int32_t const tail_length_px = Preview_tail_length_px(stroke_width_px);
    size_t tail_start = points.size() - 1;
    float accumulated_length = 0.0F;
    while (tail_start > 0) {
        accumulated_length +=
            Distance_between(points[tail_start - 1], points[tail_start]);
        --tail_start;
        if (accumulated_length >= static_cast<float>(tail_length_px)) {
            break;
        }
    }
    return tail_start;
}

} // namespace

std::optional<FreehandSmoothingMode>
Freehand_smoothing_mode_from_token(std::string_view token) noexcept {
    if (token == "off") {
        return FreehandSmoothingMode::Off;
    }
    if (token == "smooth") {
        return FreehandSmoothingMode::Smooth;
    }
    return std::nullopt;
}

std::string_view Freehand_smoothing_mode_token(FreehandSmoothingMode mode) noexcept {
    switch (mode) {
    case FreehandSmoothingMode::Off:
        return "off";
    case FreehandSmoothingMode::Smooth:
        return "smooth";
    }
    return "off";
}

std::vector<PointPx> Smooth_freehand_points(std::span<const PointPx> points,
                                            FreehandSmoothingMode mode,
                                            int32_t stroke_width_px) {
    GREENFLAME_PROFILE_FUNCTION();

    if (mode == FreehandSmoothingMode::Off || points.size() <= 2) {
        return {points.begin(), points.end()};
    }

    std::vector<PointPx> deduplicated = Deduplicate_points(points);
    if (deduplicated.size() <= 2) {
        return deduplicated;
    }

    std::vector<PointPx> const decimated =
        Decimate_points(deduplicated, stroke_width_px);
    return Apply_catmull_rom(decimated, stroke_width_px);
}

FreehandPreviewPlan Build_freehand_preview_plan(std::span<const PointPx> points,
                                                FreehandSmoothingMode mode,
                                                int32_t stroke_width_px) {
    GREENFLAME_PROFILE_FUNCTION();

    if (mode == FreehandSmoothingMode::Off || points.size() <= 2) {
        return FreehandPreviewPlan{
            .stable_raw_point_count = 0,
            .tail_start_index = 0,
            .tail_points = {points.begin(), points.end()},
        };
    }

    size_t const tail_start = Find_preview_tail_start_index(points, stroke_width_px);
    if (tail_start == 0) {
        return FreehandPreviewPlan{
            .stable_raw_point_count = 0,
            .tail_start_index = 0,
            .tail_points = {points.begin(), points.end()},
        };
    }

    return FreehandPreviewPlan{
        .stable_raw_point_count = tail_start + 1,
        .tail_start_index = tail_start,
        .tail_points = std::vector<PointPx>(
            points.begin() + static_cast<std::ptrdiff_t>(tail_start), points.end()),
    };
}

FreehandPreviewSegments Build_freehand_preview_segments(std::span<const PointPx> points,
                                                        FreehandSmoothingMode mode,
                                                        int32_t stroke_width_px) {
    FreehandPreviewPlan plan =
        Build_freehand_preview_plan(points, mode, stroke_width_px);
    if (plan.stable_raw_point_count == 0) {
        return FreehandPreviewSegments{
            .stable_points = {},
            .tail_points = std::move(plan.tail_points),
        };
    }

    return FreehandPreviewSegments{
        .stable_points = Smooth_freehand_points(
            points.first(plan.stable_raw_point_count), mode, stroke_width_px),
        .tail_points = std::move(plan.tail_points),
    };
}

} // namespace greenflame::core
