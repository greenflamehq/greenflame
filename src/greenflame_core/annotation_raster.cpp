#include "greenflame_core/annotation_raster.h"

namespace greenflame::core {

namespace {

struct PointF final {
    float x = 0.0F;
    float y = 0.0F;
};

constexpr COLORREF kByteMask = static_cast<COLORREF>(0xFF);
constexpr uint8_t kFullOpacity = 255;
constexpr int kLineRasterSamplesPerAxis = 4;
constexpr float kHalf = 0.5F;
constexpr float kArrowHeadBaseWidthPx = 10.0F;
constexpr float kArrowHeadBaseLengthPx = 18.0F;
constexpr float kArrowHeadWidthPerStrokePx = 2.0F;
constexpr float kArrowHeadLengthPerStrokePx = 4.0F;
constexpr float kArrowHeadShaftOverlapPerStrokePx = 2.0F;

[[nodiscard]] PointF To_point_f(PointPx point) noexcept {
    return {static_cast<float>(point.x), static_cast<float>(point.y)};
}

[[nodiscard]] float Distance_sq_to_segment(float px, float py, PointPx a,
                                           PointPx b) noexcept {
    float const ax = static_cast<float>(a.x);
    float const ay = static_cast<float>(a.y);
    float const bx = static_cast<float>(b.x);
    float const by = static_cast<float>(b.y);
    float const abx = bx - ax;
    float const aby = by - ay;
    float const apx = px - ax;
    float const apy = py - ay;
    float const ab_len_sq = abx * abx + aby * aby;
    if (ab_len_sq <= 0.0F) {
        return apx * apx + apy * apy;
    }
    float const t = std::clamp((apx * abx + apy * aby) / ab_len_sq, 0.0F, 1.0F);
    float const qx = ax + t * abx;
    float const qy = ay + t * aby;
    float const dx = px - qx;
    float const dy = py - qy;
    return dx * dx + dy * dy;
}

[[nodiscard]] bool Pixel_covered_by_polyline(float center_x, float center_y,
                                             std::span<const PointPx> points,
                                             float radius_sq) noexcept {
    if (points.empty()) {
        return false;
    }
    if (points.size() == 1) {
        float const dx = center_x - static_cast<float>(points.front().x);
        float const dy = center_y - static_cast<float>(points.front().y);
        return dx * dx + dy * dy <= radius_sq;
    }
    for (size_t i = 1; i < points.size(); ++i) {
        if (Distance_sq_to_segment(center_x, center_y, points[i - 1], points[i]) <=
            radius_sq) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] size_t Coverage_index(AnnotationRaster const &r, PointPx point) noexcept {
    int32_t const local_x = point.x - r.bounds.left;
    int32_t const local_y = point.y - r.bounds.top;
    return static_cast<size_t>(local_y) * static_cast<size_t>(r.Width()) +
           static_cast<size_t>(local_x);
}

[[nodiscard]] uint8_t Colorref_red(COLORREF color) noexcept {
    return static_cast<uint8_t>(color & kByteMask);
}

[[nodiscard]] uint8_t Colorref_green(COLORREF color) noexcept {
    return static_cast<uint8_t>((color >> 8) & kByteMask);
}

[[nodiscard]] uint8_t Colorref_blue(COLORREF color) noexcept {
    return static_cast<uint8_t>((color >> 16) & kByteMask);
}

[[nodiscard]] RectPx Centered_square_bounds(PointPx center, int32_t size) noexcept {
    int32_t const left = center.x - (size / 2);
    int32_t const top = center.y - (size / 2);
    return RectPx::From_ltrb(left, top, left + size, top + size);
}

[[nodiscard]] RectPx Endpoint_handle_bounds(PointPx endpoint) noexcept {
    return Centered_square_bounds(endpoint, kAnnotationHandleHitSizePx);
}

[[nodiscard]] RectPx Rectangle_handle_hit_bounds(RectPx outer_bounds,
                                                 SelectionHandle handle) noexcept {
    return Centered_square_bounds(Rectangle_resize_handle_center(outer_bounds, handle),
                                  kAnnotationHandleHitSizePx);
}

[[nodiscard]] RectPx Rectangle_handle_visual_bounds(RectPx outer_bounds,
                                                    SelectionHandle handle) noexcept {
    return Centered_square_bounds(Rectangle_resize_handle_center(outer_bounds, handle),
                                  kAnnotationHandleOuterSizePx);
}

[[nodiscard]] bool Rects_overlap(RectPx a, RectPx b) noexcept {
    return RectPx::Intersect(a, b).has_value();
}

[[nodiscard]] int64_t Distance_sq(PointPx a, PointPx b) noexcept {
    int64_t const dx = static_cast<int64_t>(a.x) - static_cast<int64_t>(b.x);
    int64_t const dy = static_cast<int64_t>(a.y) - static_cast<int64_t>(b.y);
    return dx * dx + dy * dy;
}

struct LineRasterFrame final {
    PointF center = {};
    PointF axis_u = {1.0F, 0.0F};
    PointF axis_v = {0.0F, 1.0F};
    float half_length = 0.0F;
    float half_width = 0.0F;
    std::array<PointF, 4> corners = {};
};

struct TriangleRasterShape final {
    std::array<PointF, 3> vertices = {};
};

[[nodiscard]] LineRasterFrame Build_line_raster_frame(PointF start, PointF end,
                                                      StrokeStyle style) noexcept {
    float const dx = end.x - start.x;
    float const dy = end.y - start.y;
    float const line_length = std::sqrt(dx * dx + dy * dy);

    LineRasterFrame frame{};
    frame.center = {(start.x + end.x) * kHalf, (start.y + end.y) * kHalf};
    frame.half_width = std::max(1.0F, static_cast<float>(style.width_px)) * kHalf;
    frame.half_length = (line_length * kHalf) + frame.half_width;

    if (line_length > 0.0F) {
        frame.axis_u = {dx / line_length, dy / line_length};
        frame.axis_v = {-frame.axis_u.y, frame.axis_u.x};
    }

    PointF const half_u{frame.axis_u.x * frame.half_length,
                        frame.axis_u.y * frame.half_length};
    PointF const half_v{frame.axis_v.x * frame.half_width,
                        frame.axis_v.y * frame.half_width};
    frame.corners = {
        PointF{frame.center.x - half_u.x - half_v.x,
               frame.center.y - half_u.y - half_v.y},
        PointF{frame.center.x - half_u.x + half_v.x,
               frame.center.y - half_u.y + half_v.y},
        PointF{frame.center.x + half_u.x + half_v.x,
               frame.center.y + half_u.y + half_v.y},
        PointF{frame.center.x + half_u.x - half_v.x,
               frame.center.y + half_u.y - half_v.y},
    };
    return frame;
}

[[nodiscard]] bool Point_inside_line_shape(float px, float py,
                                           LineRasterFrame const &frame) noexcept {
    float const rel_x = px - frame.center.x;
    float const rel_y = py - frame.center.y;
    float const major = rel_x * frame.axis_u.x + rel_y * frame.axis_u.y;
    float const minor = rel_x * frame.axis_v.x + rel_y * frame.axis_v.y;
    return std::abs(major) <= frame.half_length && std::abs(minor) <= frame.half_width;
}

[[nodiscard]] AnnotationRaster Rasterize_line_frame(LineRasterFrame const &frame) {
    AnnotationRaster raster{};

    float min_x = frame.corners.front().x;
    float min_y = frame.corners.front().y;
    float max_x = frame.corners.front().x;
    float max_y = frame.corners.front().y;
    for (PointF const corner : frame.corners) {
        min_x = std::min(min_x, corner.x);
        min_y = std::min(min_y, corner.y);
        max_x = std::max(max_x, corner.x);
        max_y = std::max(max_y, corner.y);
    }

    raster.bounds = RectPx::From_ltrb(static_cast<int32_t>(std::floor(min_x)) - 1,
                                      static_cast<int32_t>(std::floor(min_y)) - 1,
                                      static_cast<int32_t>(std::ceil(max_x)) + 2,
                                      static_cast<int32_t>(std::ceil(max_y)) + 2);
    if (raster.bounds.Is_empty()) {
        return raster;
    }

    size_t const width = static_cast<size_t>(raster.Width());
    size_t const height = static_cast<size_t>(raster.Height());
    raster.coverage.assign(width * height, 0);

    int constexpr samples_per_pixel =
        kLineRasterSamplesPerAxis * kLineRasterSamplesPerAxis;
    float constexpr step = 1.0F / static_cast<float>(kLineRasterSamplesPerAxis);
    for (int32_t y = raster.bounds.top; y < raster.bounds.bottom; ++y) {
        for (int32_t x = raster.bounds.left; x < raster.bounds.right; ++x) {
            int covered_samples = 0;
            for (int sy = 0; sy < kLineRasterSamplesPerAxis; ++sy) {
                float const sample_y =
                    static_cast<float>(y) + (static_cast<float>(sy) + kHalf) * step;
                for (int sx = 0; sx < kLineRasterSamplesPerAxis; ++sx) {
                    float const sample_x =
                        static_cast<float>(x) + (static_cast<float>(sx) + kHalf) * step;
                    if (Point_inside_line_shape(sample_x, sample_y, frame)) {
                        ++covered_samples;
                    }
                }
            }

            if (covered_samples == 0) {
                continue;
            }

            uint8_t const coverage =
                static_cast<uint8_t>((covered_samples * static_cast<int>(kFullOpacity) +
                                      (samples_per_pixel / 2)) /
                                     samples_per_pixel);
            raster.coverage[Coverage_index(raster, {x, y})] = coverage;
        }
    }

    return raster;
}

[[nodiscard]] float Triangle_edge_function(PointF a, PointF b, float px,
                                           float py) noexcept {
    return (px - a.x) * (b.y - a.y) - (py - a.y) * (b.x - a.x);
}

[[nodiscard]] bool Point_inside_triangle(float px, float py,
                                         TriangleRasterShape const &triangle) noexcept {
    float const e0 =
        Triangle_edge_function(triangle.vertices[0], triangle.vertices[1], px, py);
    float const e1 =
        Triangle_edge_function(triangle.vertices[1], triangle.vertices[2], px, py);
    float const e2 =
        Triangle_edge_function(triangle.vertices[2], triangle.vertices[0], px, py);
    bool const has_negative = e0 < 0.0F || e1 < 0.0F || e2 < 0.0F;
    bool const has_positive = e0 > 0.0F || e1 > 0.0F || e2 > 0.0F;
    return !(has_negative && has_positive);
}

[[nodiscard]] AnnotationRaster
Rasterize_triangle_shape(TriangleRasterShape const &triangle) {
    AnnotationRaster raster{};

    float min_x = triangle.vertices[0].x;
    float min_y = triangle.vertices[0].y;
    float max_x = triangle.vertices[0].x;
    float max_y = triangle.vertices[0].y;
    for (PointF const vertex : triangle.vertices) {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }

    raster.bounds = RectPx::From_ltrb(static_cast<int32_t>(std::floor(min_x)) - 1,
                                      static_cast<int32_t>(std::floor(min_y)) - 1,
                                      static_cast<int32_t>(std::ceil(max_x)) + 2,
                                      static_cast<int32_t>(std::ceil(max_y)) + 2);
    if (raster.bounds.Is_empty()) {
        return raster;
    }

    size_t const width = static_cast<size_t>(raster.Width());
    size_t const height = static_cast<size_t>(raster.Height());
    raster.coverage.assign(width * height, 0);

    int constexpr samples_per_pixel =
        kLineRasterSamplesPerAxis * kLineRasterSamplesPerAxis;
    float constexpr step = 1.0F / static_cast<float>(kLineRasterSamplesPerAxis);
    for (int32_t y = raster.bounds.top; y < raster.bounds.bottom; ++y) {
        for (int32_t x = raster.bounds.left; x < raster.bounds.right; ++x) {
            int covered_samples = 0;
            for (int sy = 0; sy < kLineRasterSamplesPerAxis; ++sy) {
                float const sample_y =
                    static_cast<float>(y) + (static_cast<float>(sy) + kHalf) * step;
                for (int sx = 0; sx < kLineRasterSamplesPerAxis; ++sx) {
                    float const sample_x =
                        static_cast<float>(x) + (static_cast<float>(sx) + kHalf) * step;
                    if (Point_inside_triangle(sample_x, sample_y, triangle)) {
                        ++covered_samples;
                    }
                }
            }

            if (covered_samples == 0) {
                continue;
            }

            uint8_t const coverage =
                static_cast<uint8_t>((covered_samples * static_cast<int>(kFullOpacity) +
                                      (samples_per_pixel / 2)) /
                                     samples_per_pixel);
            raster.coverage[Coverage_index(raster, {x, y})] = coverage;
        }
    }

    return raster;
}

[[nodiscard]] AnnotationRaster Combine_rasters(AnnotationRaster const &first,
                                               AnnotationRaster const &second) {
    if (first.Is_empty()) {
        return second;
    }
    if (second.Is_empty()) {
        return first;
    }

    AnnotationRaster combined{};
    combined.bounds =
        RectPx::From_ltrb(std::min(first.bounds.left, second.bounds.left),
                          std::min(first.bounds.top, second.bounds.top),
                          std::max(first.bounds.right, second.bounds.right),
                          std::max(first.bounds.bottom, second.bounds.bottom));
    if (combined.bounds.Is_empty()) {
        return combined;
    }

    size_t const width = static_cast<size_t>(combined.Width());
    size_t const height = static_cast<size_t>(combined.Height());
    combined.coverage.assign(width * height, 0);

    auto merge_coverage = [&](AnnotationRaster const &source) noexcept {
        for (int32_t y = source.bounds.top; y < source.bounds.bottom; ++y) {
            for (int32_t x = source.bounds.left; x < source.bounds.right; ++x) {
                size_t const source_index = Coverage_index(source, {x, y});
                if (source_index >= source.coverage.size()) {
                    continue;
                }
                size_t const combined_index = Coverage_index(combined, {x, y});
                combined.coverage[combined_index] = std::max(
                    combined.coverage[combined_index], source.coverage[source_index]);
            }
        }
    };

    merge_coverage(first);
    merge_coverage(second);
    return combined;
}

[[nodiscard]] AnnotationRaster Rasterize_arrow_segment(PointPx start, PointPx end,
                                                       StrokeStyle style) {
    PointF const start_f = To_point_f(start);
    PointF const end_f = To_point_f(end);
    float const dx = end_f.x - start_f.x;
    float const dy = end_f.y - start_f.y;
    float const line_length = std::sqrt(dx * dx + dy * dy);
    if (line_length <= 0.0F) {
        return Rasterize_line_frame(Build_line_raster_frame(start_f, end_f, style));
    }

    float const stroke_width = std::max(1.0F, static_cast<float>(style.width_px));
    float const head_base_width =
        kArrowHeadBaseWidthPx + (stroke_width * kArrowHeadWidthPerStrokePx);
    float const raw_head_length =
        kArrowHeadBaseLengthPx + (stroke_width * kArrowHeadLengthPerStrokePx);
    float const head_length = std::min(
        line_length,
        std::max(stroke_width,
                 raw_head_length - (stroke_width * kArrowHeadShaftOverlapPerStrokePx)));

    PointF const axis_u{dx / line_length, dy / line_length};
    PointF const axis_v{-axis_u.y, axis_u.x};
    PointF const head_tip{end_f.x + axis_u.x * kHalf, end_f.y + axis_u.y * kHalf};
    PointF const head_base_center{end_f.x - axis_u.x * head_length,
                                  end_f.y - axis_u.y * head_length};
    float const half_head_base_width = head_base_width * kHalf;
    TriangleRasterShape const head_triangle{std::array<PointF, 3>{
        head_tip,
        PointF{head_base_center.x + axis_v.x * half_head_base_width,
               head_base_center.y + axis_v.y * half_head_base_width},
        PointF{head_base_center.x - axis_v.x * half_head_base_width,
               head_base_center.y - axis_v.y * half_head_base_width},
    }};

    AnnotationRaster shaft_raster{};
    if (head_length < line_length) {
        shaft_raster = Rasterize_line_frame(
            Build_line_raster_frame(start_f, head_base_center, style));
    }
    AnnotationRaster const head_raster = Rasterize_triangle_shape(head_triangle);
    return Combine_rasters(shaft_raster, head_raster);
}

} // namespace

AnnotationRaster Rasterize_freehand_stroke(std::span<const PointPx> points,
                                           StrokeStyle style) {
    AnnotationRaster raster{};
    if (points.empty()) {
        return raster;
    }

    int32_t min_x = points.front().x;
    int32_t min_y = points.front().y;
    int32_t max_x = points.front().x;
    int32_t max_y = points.front().y;
    for (PointPx const point : points) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    }

    float const radius = std::max(1.0F, static_cast<float>(style.width_px)) / 2.0F;
    int32_t const outset = static_cast<int32_t>(std::ceil(radius));
    raster.bounds = RectPx::From_ltrb(min_x - outset, min_y - outset,
                                      max_x + outset + 1, max_y + outset + 1);
    if (raster.bounds.Is_empty()) {
        return raster;
    }

    size_t const width = static_cast<size_t>(raster.Width());
    size_t const height = static_cast<size_t>(raster.Height());
    raster.coverage.assign(width * height, 0);

    float const radius_sq = radius * radius;
    for (int32_t y = raster.bounds.top; y < raster.bounds.bottom; ++y) {
        for (int32_t x = raster.bounds.left; x < raster.bounds.right; ++x) {
            float const center_x = static_cast<float>(x) + kHalf;
            float const center_y = static_cast<float>(y) + kHalf;
            if (!Pixel_covered_by_polyline(center_x, center_y, points, radius_sq)) {
                continue;
            }
            raster.coverage[Coverage_index(raster, {x, y})] = kFullOpacity;
        }
    }

    return raster;
}

AnnotationRaster Rasterize_line_segment(PointPx start, PointPx end, StrokeStyle style,
                                        bool arrow_head) {
    if (arrow_head) {
        return Rasterize_arrow_segment(start, end, style);
    }
    return Rasterize_line_frame(
        Build_line_raster_frame(To_point_f(start), To_point_f(end), style));
}

AnnotationRaster Rasterize_rectangle(RectPx outer_bounds, StrokeStyle style,
                                     bool filled) noexcept {
    AnnotationRaster raster{};
    raster.bounds = outer_bounds.Normalized();
    if (raster.bounds.Is_empty()) {
        return raster;
    }

    size_t const width = static_cast<size_t>(raster.Width());
    size_t const height = static_cast<size_t>(raster.Height());
    raster.coverage.assign(width * height, 0);

    int32_t const inset = std::max<int32_t>(StrokeStyle::kMinWidthPx, style.width_px);
    RectPx const inner =
        RectPx::From_ltrb(raster.bounds.left + inset, raster.bounds.top + inset,
                          raster.bounds.right - inset, raster.bounds.bottom - inset);

    for (int32_t y = raster.bounds.top; y < raster.bounds.bottom; ++y) {
        for (int32_t x = raster.bounds.left; x < raster.bounds.right; ++x) {
            bool const covered = filled || inner.Is_empty() || x < inner.left ||
                                 x >= inner.right || y < inner.top || y >= inner.bottom;
            if (!covered) {
                continue;
            }
            raster.coverage[Coverage_index(raster, {x, y})] = kFullOpacity;
        }
    }

    return raster;
}

RectPx Annotation_bounds(Annotation const &annotation) noexcept {
    switch (annotation.kind) {
    case AnnotationKind::Freehand:
        return annotation.freehand.raster.bounds;
    case AnnotationKind::Line:
        return annotation.line.raster.bounds;
    case AnnotationKind::Rectangle:
        return annotation.rectangle.outer_bounds.Normalized();
    }
    return {};
}

bool Annotation_hits_point(Annotation const &annotation, PointPx point) noexcept {
    AnnotationRaster const *raster = nullptr;
    switch (annotation.kind) {
    case AnnotationKind::Freehand:
        raster = &annotation.freehand.raster;
        break;
    case AnnotationKind::Line:
        raster = &annotation.line.raster;
        break;
    case AnnotationKind::Rectangle:
        raster = &annotation.rectangle.raster;
        break;
    }
    if (raster == nullptr || !raster->bounds.Contains(point) ||
        raster->coverage.empty()) {
        return false;
    }
    size_t const index = Coverage_index(*raster, point);
    return index < raster->coverage.size() && raster->coverage[index] != 0;
}

std::optional<size_t>
Index_of_topmost_annotation_at(std::span<const Annotation> annotations,
                               PointPx point) noexcept {
    for (size_t i = annotations.size(); i > 0; --i) {
        if (Annotation_hits_point(annotations[i - 1], point)) {
            return i - 1;
        }
    }
    return std::nullopt;
}

std::optional<size_t> Index_of_annotation_id(std::span<const Annotation> annotations,
                                             uint64_t id) noexcept {
    for (size_t i = 0; i < annotations.size(); ++i) {
        if (annotations[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<AnnotationLineEndpoint>
Hit_test_line_endpoint_handles(PointPx start, PointPx end, PointPx cursor) noexcept {
    bool const hit_start = Endpoint_handle_bounds(start).Contains(cursor);
    bool const hit_end = Endpoint_handle_bounds(end).Contains(cursor);
    if (!hit_start && !hit_end) {
        return std::nullopt;
    }
    if (hit_start && hit_end) {
        return Distance_sq(cursor, start) <= Distance_sq(cursor, end)
                   ? std::optional<
                         AnnotationLineEndpoint>{AnnotationLineEndpoint::Start}
                   : std::optional<AnnotationLineEndpoint>{AnnotationLineEndpoint::End};
    }
    return hit_start
               ? std::optional<AnnotationLineEndpoint>{AnnotationLineEndpoint::Start}
               : std::optional<AnnotationLineEndpoint>{AnnotationLineEndpoint::End};
}

RectPx Rectangle_outer_bounds_from_corners(PointPx a, PointPx b) noexcept {
    int32_t const left = std::min(a.x, b.x);
    int32_t const top = std::min(a.y, b.y);
    int32_t const right = std::max(a.x, b.x) + 1;
    int32_t const bottom = std::max(a.y, b.y) + 1;
    return RectPx::From_ltrb(left, top, right, bottom);
}

PointPx Rectangle_resize_handle_center(RectPx outer_bounds,
                                       SelectionHandle handle) noexcept {
    RectPx const r = outer_bounds.Normalized();
    int32_t const right = r.right - 1;
    int32_t const bottom = r.bottom - 1;
    int32_t const center_x = (r.left + right) / 2;
    int32_t const center_y = (r.top + bottom) / 2;

    switch (handle) {
    case SelectionHandle::TopLeft:
        return {r.left, r.top};
    case SelectionHandle::TopRight:
        return {right, r.top};
    case SelectionHandle::BottomRight:
        return {right, bottom};
    case SelectionHandle::BottomLeft:
        return {r.left, bottom};
    case SelectionHandle::Top:
        return {center_x, r.top};
    case SelectionHandle::Right:
        return {right, center_y};
    case SelectionHandle::Bottom:
        return {center_x, bottom};
    case SelectionHandle::Left:
        return {r.left, center_y};
    }
    return {r.left, r.top};
}

std::array<bool, 8> Visible_rectangle_resize_handles(RectPx outer_bounds) noexcept {
    std::array<bool, 8> visible{};
    visible.fill(true);

    RectPx const top_left =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::TopLeft);
    RectPx const top_right =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::TopRight);
    RectPx const bottom_right =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::BottomRight);
    RectPx const bottom_left =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::BottomLeft);

    RectPx const top =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::Top);
    if (Rects_overlap(top, top_left) || Rects_overlap(top, top_right)) {
        visible[static_cast<size_t>(SelectionHandle::Top)] = false;
    }

    RectPx const right =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::Right);
    if (Rects_overlap(right, top_right) || Rects_overlap(right, bottom_right)) {
        visible[static_cast<size_t>(SelectionHandle::Right)] = false;
    }

    RectPx const bottom =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::Bottom);
    if (Rects_overlap(bottom, bottom_left) || Rects_overlap(bottom, bottom_right)) {
        visible[static_cast<size_t>(SelectionHandle::Bottom)] = false;
    }

    RectPx const left =
        Rectangle_handle_visual_bounds(outer_bounds, SelectionHandle::Left);
    if (Rects_overlap(left, top_left) || Rects_overlap(left, bottom_left)) {
        visible[static_cast<size_t>(SelectionHandle::Left)] = false;
    }

    return visible;
}

std::optional<SelectionHandle>
Hit_test_rectangle_resize_handles(RectPx outer_bounds, PointPx cursor) noexcept {
    std::array<bool, 8> const visible = Visible_rectangle_resize_handles(outer_bounds);
    for (size_t i = 0; i < visible.size(); ++i) {
        if (!visible[i]) {
            continue;
        }
        SelectionHandle const handle = static_cast<SelectionHandle>(i);
        if (Rectangle_handle_hit_bounds(outer_bounds, handle).Contains(cursor)) {
            return handle;
        }
    }
    return std::nullopt;
}

RectPx Resize_rectangle_from_handle(RectPx outer_bounds, SelectionHandle handle,
                                    PointPx cursor) noexcept {
    constexpr int32_t min_size_px = 1;

    RectPx r = outer_bounds.Normalized();
    if (r.Is_empty()) {
        return r;
    }

    switch (handle) {
    case SelectionHandle::TopLeft:
        r.left = cursor.x;
        r.top = cursor.y;
        break;
    case SelectionHandle::TopRight:
        r.right = cursor.x + 1;
        r.top = cursor.y;
        break;
    case SelectionHandle::BottomRight:
        r.right = cursor.x + 1;
        r.bottom = cursor.y + 1;
        break;
    case SelectionHandle::BottomLeft:
        r.left = cursor.x;
        r.bottom = cursor.y + 1;
        break;
    case SelectionHandle::Top:
        r.top = cursor.y;
        break;
    case SelectionHandle::Right:
        r.right = cursor.x + 1;
        break;
    case SelectionHandle::Bottom:
        r.bottom = cursor.y + 1;
        break;
    case SelectionHandle::Left:
        r.left = cursor.x;
        break;
    }

    r = r.Normalized();
    if (r.Width() < min_size_px) {
        if (handle == SelectionHandle::Left || handle == SelectionHandle::TopLeft ||
            handle == SelectionHandle::BottomLeft) {
            r.left = r.right - min_size_px;
        } else {
            r.right = r.left + min_size_px;
        }
    }
    if (r.Height() < min_size_px) {
        if (handle == SelectionHandle::Top || handle == SelectionHandle::TopLeft ||
            handle == SelectionHandle::TopRight) {
            r.top = r.bottom - min_size_px;
        } else {
            r.bottom = r.top + min_size_px;
        }
    }
    return r.Normalized();
}

Annotation Translate_annotation(Annotation annotation, PointPx delta) noexcept {
    switch (annotation.kind) {
    case AnnotationKind::Freehand:
        for (PointPx &point : annotation.freehand.points) {
            point.x += delta.x;
            point.y += delta.y;
        }
        annotation.freehand.raster.bounds =
            RectPx::From_ltrb(annotation.freehand.raster.bounds.left + delta.x,
                              annotation.freehand.raster.bounds.top + delta.y,
                              annotation.freehand.raster.bounds.right + delta.x,
                              annotation.freehand.raster.bounds.bottom + delta.y);
        break;
    case AnnotationKind::Line:
        annotation.line.start.x += delta.x;
        annotation.line.start.y += delta.y;
        annotation.line.end.x += delta.x;
        annotation.line.end.y += delta.y;
        annotation.line.raster.bounds =
            RectPx::From_ltrb(annotation.line.raster.bounds.left + delta.x,
                              annotation.line.raster.bounds.top + delta.y,
                              annotation.line.raster.bounds.right + delta.x,
                              annotation.line.raster.bounds.bottom + delta.y);
        break;
    case AnnotationKind::Rectangle:
        annotation.rectangle.outer_bounds =
            RectPx::From_ltrb(annotation.rectangle.outer_bounds.left + delta.x,
                              annotation.rectangle.outer_bounds.top + delta.y,
                              annotation.rectangle.outer_bounds.right + delta.x,
                              annotation.rectangle.outer_bounds.bottom + delta.y);
        annotation.rectangle.raster.bounds =
            RectPx::From_ltrb(annotation.rectangle.raster.bounds.left + delta.x,
                              annotation.rectangle.raster.bounds.top + delta.y,
                              annotation.rectangle.raster.bounds.right + delta.x,
                              annotation.rectangle.raster.bounds.bottom + delta.y);
        break;
    }
    return annotation;
}

void Blend_annotations_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                                   int row_bytes,
                                   std::span<const Annotation> annotations,
                                   RectPx target_bounds) noexcept {
    if (width <= 0 || height <= 0 || row_bytes <= 0) {
        return;
    }

    for (Annotation const &annotation : annotations) {
        Blend_annotation_onto_pixels(pixels, width, height, row_bytes, annotation,
                                     target_bounds);
    }
}

void Blend_annotation_onto_pixels(std::span<uint8_t> pixels, int width, int height,
                                  int row_bytes, Annotation const &annotation,
                                  RectPx target_bounds) noexcept {
    if (width <= 0 || height <= 0 || row_bytes <= 0) {
        return;
    }

    AnnotationRaster const *raster = nullptr;
    StrokeStyle style{};
    switch (annotation.kind) {
    case AnnotationKind::Freehand:
        raster = &annotation.freehand.raster;
        style = annotation.freehand.style;
        break;
    case AnnotationKind::Line:
        raster = &annotation.line.raster;
        style = annotation.line.style;
        break;
    case AnnotationKind::Rectangle:
        raster = &annotation.rectangle.raster;
        style = annotation.rectangle.style;
        break;
    }
    if (raster == nullptr || raster->Is_empty()) {
        return;
    }
    std::optional<RectPx> const clipped =
        RectPx::Intersect(raster->bounds, target_bounds);
    if (!clipped.has_value()) {
        return;
    }

    uint8_t const red = Colorref_red(style.color);
    uint8_t const green = Colorref_green(style.color);
    uint8_t const blue = Colorref_blue(style.color);

    for (int32_t y = clipped->top; y < clipped->bottom; ++y) {
        int32_t const target_y = y - target_bounds.top;
        size_t const row_offset =
            static_cast<size_t>(target_y) * static_cast<size_t>(row_bytes);
        for (int32_t x = clipped->left; x < clipped->right; ++x) {
            size_t const coverage_index = Coverage_index(*raster, {x, y});
            if (coverage_index >= raster->coverage.size()) {
                continue;
            }

            uint8_t const coverage = raster->coverage[coverage_index];
            if (coverage == 0) {
                continue;
            }

            int32_t const target_x = x - target_bounds.left;
            size_t const pixel_offset = row_offset + static_cast<size_t>(target_x) * 4;
            if (pixel_offset + 3 >= pixels.size()) {
                continue;
            }

            uint32_t const inverse = static_cast<uint32_t>(kFullOpacity) - coverage;
            pixels[pixel_offset] = static_cast<uint8_t>(
                (static_cast<uint32_t>(pixels[pixel_offset]) * inverse +
                 static_cast<uint32_t>(blue) * coverage + (kFullOpacity / 2)) /
                kFullOpacity);
            pixels[pixel_offset + 1] = static_cast<uint8_t>(
                (static_cast<uint32_t>(pixels[pixel_offset + 1]) * inverse +
                 static_cast<uint32_t>(green) * coverage + (kFullOpacity / 2)) /
                kFullOpacity);
            pixels[pixel_offset + 2] = static_cast<uint8_t>(
                (static_cast<uint32_t>(pixels[pixel_offset + 2]) * inverse +
                 static_cast<uint32_t>(red) * coverage + (kFullOpacity / 2)) /
                kFullOpacity);
            pixels[pixel_offset + 3] = kFullOpacity;
        }
    }
}

} // namespace greenflame::core
