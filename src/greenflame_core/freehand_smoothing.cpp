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

[[nodiscard]] float Smooth_resample_spacing_px(int32_t stroke_width_px) noexcept {
    return std::max(1.0F, static_cast<float>(stroke_width_px) /
                              kSmoothResampleSpacingDivisor);
}

[[nodiscard]] int64_t Min_decimation_spacing_sq(int32_t stroke_width_px) noexcept {
    int32_t const min_spacing_px =
        std::max(kMinDecimationSpacingPx, stroke_width_px / kDecimationSpacingDivisor);
    return static_cast<int64_t>(min_spacing_px) * static_cast<int64_t>(min_spacing_px);
}

[[nodiscard]] std::vector<PointPx> Decimate_points(std::span<const PointPx> points,
                                                   int32_t stroke_width_px) {
    if (points.size() <= 2) {
        return {points.begin(), points.end()};
    }

    int64_t const min_spacing_sq = Min_decimation_spacing_sq(stroke_width_px);
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

// Appends points while suppressing adjacent duplicates. `last` is the point the
// output currently ends with, which may live in another vector (the incremental
// smoother's final points precede its provisional points).
struct PointSink final {
    std::vector<PointPx> *out = nullptr;
    std::optional<PointPx> last = std::nullopt;

    void Push(PointPx point) {
        if (last.has_value() && *last == point) {
            return;
        }
        out->push_back(point);
        last = point;
    }
};

[[nodiscard]] std::optional<PointPx> Last_point(std::span<const PointPx> points) {
    if (points.empty()) {
        return std::nullopt;
    }
    return points.back();
}

// Emits one segment a->b of a Catmull-Rom subpath. `before` is the point ahead of
// `a` (nullopt when a starts the subpath: its tangent mirrors b); `after` is the
// point past `b` (nullopt when b ends the subpath). A subpath of two points is a
// plain polyline.
void Emit_catmull_segment(std::optional<PointPx> before, PointPx a, PointPx b,
                          std::optional<PointPx> after, float spacing_px,
                          PointSink &sink) {
    if (!before.has_value() && !after.has_value()) {
        sink.Push(a);
        sink.Push(b);
        return;
    }
    if (!before.has_value()) {
        sink.Push(a);
    }

    PointF const p0 = before.has_value() ? To_pointf(*before) : Mirror_endpoint(a, b);
    PointF const p1 = To_pointf(a);
    PointF const p2 = To_pointf(b);
    PointF const p3 = after.has_value() ? To_pointf(*after) : Mirror_endpoint(b, a);
    float const segment_length = Distance_between(a, b);
    int32_t const subdivision_count =
        std::max(1, static_cast<int32_t>(std::ceil(segment_length / spacing_px)));

    if (subdivision_count > 1) {
        // Knot distances depend on the segment, not on its resampled points.
        auto const advance = [](float t_prev, PointF from, PointF to) noexcept {
            float const dx = to.x - from.x;
            float const dy = to.y - from.y;
            float const distance = std::sqrt((dx * dx) + (dy * dy));
            return t_prev + std::pow(std::max(distance, kMinSplineSegmentLength),
                                     kCentripetalAlpha);
        };
        float const t0 = 0.0F;
        float const t1 = advance(t0, p0, p1);
        float const t2 = advance(t1, p1, p2);
        float const t3 = advance(t2, p2, p3);
        auto const sample_point = [=](float t) noexcept {
            if ((t1 - t0) < kMinSplineSegmentLength ||
                (t2 - t1) < kMinSplineSegmentLength ||
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
        };

        for (int32_t step = 1; step < subdivision_count; ++step) {
            float const t =
                static_cast<float>(step) / static_cast<float>(subdivision_count);
            sink.Push(To_point_px(sample_point(t)));
        }
    }
    sink.Push(b);
}

// Emits segment `index` of `points`, splitting subpaths at corner anchors. Corner
// flags only depend on a point's two neighbors, so a segment is fixed once the
// points two past it are.
void Emit_catmull_segment_at(std::span<const PointPx> points, size_t index,
                             float spacing_px, PointSink &sink) {
    size_t const last = points.size() - 1;
    bool const starts_subpath =
        index == 0 ||
        Is_corner_anchor(points[index - 1], points[index], points[index + 1]);
    bool const ends_subpath =
        index + 1 == last ||
        Is_corner_anchor(points[index], points[index + 1], points[index + 2]);
    Emit_catmull_segment(
        starts_subpath ? std::nullopt : std::optional<PointPx>(points[index - 1]),
        points[index], points[index + 1],
        ends_subpath ? std::nullopt : std::optional<PointPx>(points[index + 2]),
        spacing_px, sink);
}

[[nodiscard]] std::vector<PointPx> Apply_catmull_rom(std::span<const PointPx> points,
                                                     int32_t stroke_width_px) {
    if (points.size() <= 2) {
        return {points.begin(), points.end()};
    }

    float const spacing_px = Smooth_resample_spacing_px(stroke_width_px);
    std::vector<PointPx> smoothed = {};
    smoothed.reserve(points.size() * 2);
    PointSink sink{&smoothed};
    for (size_t index = 0; index + 1 < points.size(); ++index) {
        Emit_catmull_segment_at(points, index, spacing_px, sink);
    }
    return smoothed;
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

void FreehandIncrementalSmoother::Reset(FreehandSmoothingMode mode,
                                        int32_t stroke_width_px) {
    mode_ = mode;
    stroke_width_px_ = stroke_width_px;
    raw_count_ = 0;
    raw_head_.clear();
    deduplicated_.clear();
    decimated_.clear();
    last_kept_index_ = 0;
    next_decimation_index_ = 1;
    next_segment_index_ = 0;
    final_.clear();
    provisional_.clear();
}

void FreehandIncrementalSmoother::Append(std::span<const PointPx> points) {
    if (points.empty()) {
        return;
    }
    for (PointPx const point : points) {
        Append_point(point);
    }
    Update_provisional();
}

void FreehandIncrementalSmoother::Append_point(PointPx point) {
    ++raw_count_;
    if (raw_head_.size() < 2) {
        raw_head_.push_back(point);
    }
    if (mode_ == FreehandSmoothingMode::Off) {
        final_.push_back(point);
        return;
    }
    if (!deduplicated_.empty() && deduplicated_.back() == point) {
        return;
    }
    deduplicated_.push_back(point);
    if (deduplicated_.size() == 1) {
        decimated_.push_back(point);
        return;
    }

    // Same rule as Decimate_points. A point is decided once its next neighbor is
    // known; the newest point is always provisional.
    int64_t const min_spacing_sq = Min_decimation_spacing_sq(stroke_width_px_);
    while (next_decimation_index_ + 1 < deduplicated_.size()) {
        size_t const index = next_decimation_index_++;
        if (Is_corner_anchor(deduplicated_[index - 1], deduplicated_[index],
                             deduplicated_[index + 1]) ||
            Distance_squared(deduplicated_[last_kept_index_], deduplicated_[index]) >=
                min_spacing_sq) {
            decimated_.push_back(deduplicated_[index]);
            last_kept_index_ = index;
        }
    }

    // A segment is fixed once the decimated points two past its end are fixed.
    float const spacing_px = Smooth_resample_spacing_px(stroke_width_px_);
    PointSink sink{&final_, Last_point(final_)};
    while (next_segment_index_ + 3 <= decimated_.size()) {
        Emit_catmull_segment_at(decimated_, next_segment_index_, spacing_px, sink);
        ++next_segment_index_;
    }
}

void FreehandIncrementalSmoother::Update_provisional() {
    provisional_.clear();
    if (mode_ == FreehandSmoothingMode::Off) {
        return;
    }
    // The early returns of Smooth_freehand_points and Apply_catmull_rom. None of them
    // can follow a non-empty final_, which needs at least three fixed decimated points.
    if (raw_count_ <= 2) {
        provisional_ = raw_head_;
        return;
    }
    if (deduplicated_.size() <= 2) {
        provisional_ = deduplicated_;
        return;
    }

    // The decimated stroke ends with the newest point unless the fixed points already
    // end there. Only its last few points feed the segments not yet fixed.
    size_t const window_begin = next_segment_index_ > 0 ? next_segment_index_ - 1 : 0;
    std::vector<PointPx> window(decimated_.begin() +
                                    static_cast<std::ptrdiff_t>(window_begin),
                                decimated_.end());
    if (window.back() != deduplicated_.back()) {
        window.push_back(deduplicated_.back());
    }
    if (window_begin == 0 && window.size() <= 2) {
        provisional_ = std::move(window);
        return;
    }

    float const spacing_px = Smooth_resample_spacing_px(stroke_width_px_);
    PointSink sink{&provisional_, Last_point(final_)};
    for (size_t index = next_segment_index_ - window_begin; index + 1 < window.size();
         ++index) {
        Emit_catmull_segment_at(window, index, spacing_px, sink);
    }
}

} // namespace greenflame::core
