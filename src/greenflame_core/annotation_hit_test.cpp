#include "greenflame_core/annotation_hit_test.h"

#include "greenflame_core/bubble_annotation_types.h"

namespace greenflame::core {

namespace {

struct PointF final {
    float x = 0.0F;
    float y = 0.0F;
};

constexpr int kLineRasterSamplesPerAxis = 4;
constexpr float kHalf = 0.5F;
constexpr float kFloatEpsilon = 1e-6F;
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

[[nodiscard]] bool Point_inside_axis_aligned_square(float px, float py, PointPx center,
                                                    float half_extent) noexcept {
    return std::abs(px - static_cast<float>(center.x)) <= half_extent &&
           std::abs(py - static_cast<float>(center.y)) <= half_extent;
}

[[nodiscard]] bool Segment_intersects_axis_aligned_rect(PointPx start, PointPx end,
                                                        float left, float top,
                                                        float right,
                                                        float bottom) noexcept {
    float const x0 = static_cast<float>(start.x);
    float const y0 = static_cast<float>(start.y);
    float const x1 = static_cast<float>(end.x);
    float const y1 = static_cast<float>(end.y);
    float const dx = x1 - x0;
    float const dy = y1 - y0;
    float t0 = 0.0F;
    float t1 = 1.0F;

    auto clip = [&](float p, float q) noexcept {
        if (std::abs(p) <= kFloatEpsilon) {
            return q >= 0.0F;
        }

        float const r = q / p;
        if (p < 0.0F) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
            return true;
        }

        if (r < t0) {
            return false;
        }
        if (r < t1) {
            t1 = r;
        }
        return true;
    };

    return clip(-dx, x0 - left) && clip(dx, right - x0) && clip(-dy, y0 - top) &&
           clip(dy, bottom - y0);
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

struct TriangleShape final {
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

[[nodiscard]] bool
Pixel_covered_by_square_capped_polyline(float center_x, float center_y,
                                        std::span<const PointPx> points,
                                        int32_t width_px) noexcept {
    if (points.empty()) {
        return false;
    }

    float const half_extent = std::max(1.0F, static_cast<float>(width_px)) * kHalf;
    if (points.size() == 1) {
        return Point_inside_axis_aligned_square(center_x, center_y, points.front(),
                                                half_extent);
    }

    float const left = center_x - half_extent;
    float const top = center_y - half_extent;
    float const right = center_x + half_extent;
    float const bottom = center_y + half_extent;
    for (size_t i = 1; i < points.size(); ++i) {
        if (Segment_intersects_axis_aligned_rect(points[i - 1], points[i], left, top,
                                                 right, bottom)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] float Triangle_edge_function(PointF a, PointF b, float px,
                                           float py) noexcept {
    return (px - a.x) * (b.y - a.y) - (py - a.y) * (b.x - a.x);
}

[[nodiscard]] bool Point_inside_triangle(float px, float py,
                                         TriangleShape const &triangle) noexcept {
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

struct ArrowGeometry final {
    LineRasterFrame shaft_frame = {};
    bool has_shaft = false;
    TriangleShape head = {};
    PointF head_base_center = {};
    float head_length = 0.0F;
};

[[nodiscard]] ArrowGeometry Build_arrow_geometry(PointF start, PointF end,
                                                 StrokeStyle style) noexcept {
    ArrowGeometry geom{};
    float const dx = end.x - start.x;
    float const dy = end.y - start.y;
    float const line_length = std::sqrt(dx * dx + dy * dy);

    if (line_length <= 0.0F) {
        geom.shaft_frame = Build_line_raster_frame(start, end, style);
        geom.has_shaft = true;
        return geom;
    }

    float const stroke_width = std::max(1.0F, static_cast<float>(style.width_px));
    float const head_base_width =
        kArrowHeadBaseWidthPx + (stroke_width * kArrowHeadWidthPerStrokePx);
    float const raw_head_length =
        kArrowHeadBaseLengthPx + (stroke_width * kArrowHeadLengthPerStrokePx);
    geom.head_length = std::min(
        line_length,
        std::max(stroke_width,
                 raw_head_length - (stroke_width * kArrowHeadShaftOverlapPerStrokePx)));

    PointF const axis_u{dx / line_length, dy / line_length};
    PointF const axis_v{-axis_u.y, axis_u.x};
    PointF const head_tip{end.x + axis_u.x * kHalf, end.y + axis_u.y * kHalf};
    geom.head_base_center = {end.x - axis_u.x * geom.head_length,
                             end.y - axis_u.y * geom.head_length};
    float const half_head_base_width = head_base_width * kHalf;
    geom.head = TriangleShape{std::array<PointF, 3>{
        head_tip,
        PointF{geom.head_base_center.x + axis_v.x * half_head_base_width,
               geom.head_base_center.y + axis_v.y * half_head_base_width},
        PointF{geom.head_base_center.x - axis_v.x * half_head_base_width,
               geom.head_base_center.y - axis_v.y * half_head_base_width},
    }};

    if (geom.head_length < line_length) {
        geom.shaft_frame = Build_line_raster_frame(start, geom.head_base_center, style);
        geom.has_shaft = true;
    }
    return geom;
}

// Test if (px, py) is covered by the given arrow geometry using 4x4 supersampling
// at the specific sample point. Used for hit testing at integer pixel positions.
[[nodiscard]] bool Sample_covered_by_arrow(float px, float py,
                                           ArrowGeometry const &geom) noexcept {
    if (Point_inside_triangle(px, py, geom.head)) {
        return true;
    }
    if (geom.has_shaft && Point_inside_line_shape(px, py, geom.shaft_frame)) {
        return true;
    }
    return false;
}

// Tight pixel AABB from float extents. Pixel i covers [i, i+1) so right/bottom
// are exclusive: right = floor(max_x) + 1.
[[nodiscard]] RectPx Tight_float_aabb_px(float min_x, float min_y, float max_x,
                                         float max_y) noexcept {
    return RectPx::From_ltrb(static_cast<int32_t>(std::floor(min_x)),
                             static_cast<int32_t>(std::floor(min_y)),
                             static_cast<int32_t>(std::floor(max_x)) + 1,
                             static_cast<int32_t>(std::floor(max_y)) + 1);
}

// Tight visual bounds of a line segment drawn with flat end caps.
// No cap extension beyond the endpoints (unlike Line_frame_bounds_px).
[[nodiscard]] RectPx Line_tight_visual_bounds_px(PointF start, PointF end,
                                                 StrokeStyle style) noexcept {
    float const dx = end.x - start.x;
    float const dy = end.y - start.y;
    float const len = std::sqrt(dx * dx + dy * dy);
    float const half_w = std::max(1.0F, static_cast<float>(style.width_px)) * kHalf;
    if (len <= 0.0F) {
        return Tight_float_aabb_px(start.x - half_w, start.y - half_w, start.x + half_w,
                                   start.y + half_w);
    }
    float const ux = dx / len;
    float const uy = dy / len;
    float const vx = -uy;
    float const vy = ux;
    float const c0x = start.x + vx * half_w, c0y = start.y + vy * half_w;
    float const c1x = start.x - vx * half_w, c1y = start.y - vy * half_w;
    float const c2x = end.x + vx * half_w, c2y = end.y + vy * half_w;
    float const c3x = end.x - vx * half_w, c3y = end.y - vy * half_w;
    return Tight_float_aabb_px(
        std::min({c0x, c1x, c2x, c3x}), std::min({c0y, c1y, c2y, c3y}),
        std::max({c0x, c1x, c2x, c3x}), std::max({c0y, c1y, c2y, c3y}));
}

[[nodiscard]] RectPx Triangle_tight_bounds_px(TriangleShape const &tri) noexcept {
    return Tight_float_aabb_px(
        std::min({tri.vertices[0].x, tri.vertices[1].x, tri.vertices[2].x}),
        std::min({tri.vertices[0].y, tri.vertices[1].y, tri.vertices[2].y}),
        std::max({tri.vertices[0].x, tri.vertices[1].x, tri.vertices[2].x}),
        std::max({tri.vertices[0].y, tri.vertices[1].y, tri.vertices[2].y}));
}

// Returns the bounding box for a LineRasterFrame (padded to match raster convention).
[[nodiscard]] RectPx Line_frame_bounds_px(LineRasterFrame const &frame) noexcept {
    float min_x = frame.corners[0].x;
    float min_y = frame.corners[0].y;
    float max_x = frame.corners[0].x;
    float max_y = frame.corners[0].y;
    for (PointF const corner : frame.corners) {
        min_x = std::min(min_x, corner.x);
        min_y = std::min(min_y, corner.y);
        max_x = std::max(max_x, corner.x);
        max_y = std::max(max_y, corner.y);
    }
    return RectPx::From_ltrb(static_cast<int32_t>(std::floor(min_x)) - 1,
                             static_cast<int32_t>(std::floor(min_y)) - 1,
                             static_cast<int32_t>(std::ceil(max_x)) + 2,
                             static_cast<int32_t>(std::ceil(max_y)) + 2);
}

[[nodiscard]] RectPx Triangle_bounds_px(TriangleShape const &tri) noexcept {
    float min_x = tri.vertices[0].x;
    float min_y = tri.vertices[0].y;
    float max_x = tri.vertices[0].x;
    float max_y = tri.vertices[0].y;
    for (PointF const vertex : tri.vertices) {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }
    return RectPx::From_ltrb(static_cast<int32_t>(std::floor(min_x)) - 1,
                             static_cast<int32_t>(std::floor(min_y)) - 1,
                             static_cast<int32_t>(std::ceil(max_x)) + 2,
                             static_cast<int32_t>(std::ceil(max_y)) + 2);
}

[[nodiscard]] RectPx Combine_bounds(RectPx a, RectPx b) noexcept {
    if (a.Is_empty()) {
        return b;
    }
    if (b.Is_empty()) {
        return a;
    }
    return RectPx::From_ltrb(std::min(a.left, b.left), std::min(a.top, b.top),
                             std::max(a.right, b.right), std::max(a.bottom, b.bottom));
}

[[nodiscard]] RectPx Bubble_visual_bounds(BubbleAnnotation const &bubble) noexcept {
    int32_t const r = bubble.diameter_px / 2;
    return RectPx::From_ltrb(bubble.center.x - r, bubble.center.y - r,
                             bubble.center.x - r + bubble.diameter_px,
                             bubble.center.y - r + bubble.diameter_px);
}

[[nodiscard]] bool Point_inside_ellipse(float px, float py,
                                        RectPx outer_bounds) noexcept {
    RectPx const r = outer_bounds.Normalized();
    float const width = static_cast<float>(r.Width());
    float const height = static_cast<float>(r.Height());
    if (width <= 0.0F || height <= 0.0F) {
        return false;
    }

    float const rx = width * kHalf;
    float const ry = height * kHalf;
    if (rx <= 0.0F || ry <= 0.0F) {
        return false;
    }

    float const cx = static_cast<float>(r.left) + rx;
    float const cy = static_cast<float>(r.top) + ry;
    float const dx = (px - cx) / rx;
    float const dy = (py - cy) / ry;
    return (dx * dx + dy * dy) <= 1.0F;
}

[[nodiscard]] bool
Point_inside_ellipse_outline(float px, float py,
                             EllipseAnnotation const &ellipse) noexcept {
    RectPx const r = ellipse.outer_bounds.Normalized();
    if (!Point_inside_ellipse(px, py, r)) {
        return false;
    }
    if (ellipse.filled) {
        return true;
    }

    int32_t const inset =
        std::max<int32_t>(StrokeStyle::kMinWidthPx, ellipse.style.width_px);
    RectPx const inner = RectPx::From_ltrb(r.left + inset, r.top + inset,
                                           r.right - inset, r.bottom - inset);
    if (inner.Is_empty()) {
        return true;
    }
    return !Point_inside_ellipse(px, py, inner);
}

[[nodiscard]] bool Text_bitmap_is_valid(TextAnnotation const &annotation) noexcept {
    if (annotation.bitmap_width_px <= 0 || annotation.bitmap_height_px <= 0 ||
        annotation.bitmap_row_bytes < annotation.bitmap_width_px * 4) {
        return false;
    }
    size_t const required_size = static_cast<size_t>(annotation.bitmap_row_bytes) *
                                 static_cast<size_t>(annotation.bitmap_height_px);
    return annotation.premultiplied_bgra.size() >= required_size;
}

[[nodiscard]] std::optional<size_t>
Text_bitmap_pixel_offset(TextAnnotation const &annotation, PointPx point) noexcept {
    if (!annotation.visual_bounds.Contains(point) ||
        !Text_bitmap_is_valid(annotation)) {
        return std::nullopt;
    }

    int32_t const rel_x = point.x - annotation.visual_bounds.left;
    int32_t const rel_y = point.y - annotation.visual_bounds.top;
    if (rel_x < 0 || rel_y < 0 || rel_x >= annotation.bitmap_width_px ||
        rel_y >= annotation.bitmap_height_px) {
        return std::nullopt;
    }

    size_t const offset =
        static_cast<size_t>(rel_y) * static_cast<size_t>(annotation.bitmap_row_bytes) +
        static_cast<size_t>(rel_x) * 4u;
    if (offset + 3u >= annotation.premultiplied_bgra.size()) {
        return std::nullopt;
    }
    return offset;
}

} // namespace

AnnotationKind Annotation::Kind() const noexcept {
    return std::visit(
        Overloaded{
            [](FreehandStrokeAnnotation const &) noexcept {
                return AnnotationKind::Freehand;
            },
            [](LineAnnotation const &) noexcept { return AnnotationKind::Line; },
            [](RectangleAnnotation const &) noexcept {
                return AnnotationKind::Rectangle;
            },
            [](EllipseAnnotation const &) noexcept { return AnnotationKind::Ellipse; },
            [](ObfuscateAnnotation const &) noexcept {
                return AnnotationKind::Obfuscate;
            },
            [](TextAnnotation const &) noexcept { return AnnotationKind::Text; },
            [](BubbleAnnotation const &) noexcept { return AnnotationKind::Bubble; },
        },
        data);
}

RectPx Annotation_bounds(Annotation const &annotation) noexcept {
    return std::visit(
        Overloaded{
            [](FreehandStrokeAnnotation const &fh) -> RectPx {
                auto const &pts = fh.points;
                if (pts.empty()) {
                    return {};
                }
                int32_t min_x = pts.front().x;
                int32_t min_y = pts.front().y;
                int32_t max_x = pts.front().x;
                int32_t max_y = pts.front().y;
                for (PointPx const &p : pts) {
                    min_x = std::min(min_x, p.x);
                    min_y = std::min(min_y, p.y);
                    max_x = std::max(max_x, p.x);
                    max_y = std::max(max_y, p.y);
                }
                float const half_extent =
                    std::max(1.0F, static_cast<float>(fh.style.width_px)) * kHalf;
                int32_t const outset = static_cast<int32_t>(std::ceil(half_extent));
                return RectPx::From_ltrb(min_x - outset, min_y - outset,
                                         max_x + outset + 1, max_y + outset + 1);
            },
            [](LineAnnotation const &line) -> RectPx {
                PointF const start_f = To_point_f(line.start);
                PointF const end_f = To_point_f(line.end);
                if (line.arrow_head) {
                    ArrowGeometry const geom =
                        Build_arrow_geometry(start_f, end_f, line.style);
                    RectPx const head_bounds = Triangle_bounds_px(geom.head);
                    RectPx const shaft_bounds =
                        geom.has_shaft ? Line_frame_bounds_px(geom.shaft_frame)
                                       : RectPx{};
                    return Combine_bounds(shaft_bounds, head_bounds);
                }
                return Line_frame_bounds_px(
                    Build_line_raster_frame(start_f, end_f, line.style));
            },
            [](RectangleAnnotation const &rect) -> RectPx {
                return rect.outer_bounds.Normalized();
            },
            [](EllipseAnnotation const &ellipse) -> RectPx {
                return ellipse.outer_bounds.Normalized();
            },
            [](ObfuscateAnnotation const &obfuscate) -> RectPx {
                return obfuscate.bounds.Normalized();
            },
            [](TextAnnotation const &text) -> RectPx {
                return text.visual_bounds.Normalized();
            },
            [](BubbleAnnotation const &bubble) -> RectPx {
                return Bubble_visual_bounds(bubble);
            },
        },
        annotation.data);
}

RectPx Annotation_visual_bounds(Annotation const &annotation) noexcept {
    return std::visit(
        Overloaded{
            [&](FreehandStrokeAnnotation const &) -> RectPx {
                return Annotation_bounds(annotation);
            },
            [](LineAnnotation const &line) -> RectPx {
                PointF const start_f = To_point_f(line.start);
                PointF const end_f = To_point_f(line.end);
                if (line.arrow_head) {
                    ArrowGeometry const geom =
                        Build_arrow_geometry(start_f, end_f, line.style);
                    RectPx const head_bounds = Triangle_tight_bounds_px(geom.head);
                    RectPx const shaft_bounds =
                        geom.has_shaft ? Line_tight_visual_bounds_px(
                                             start_f, geom.head_base_center, line.style)
                                       : RectPx{};
                    return Combine_bounds(shaft_bounds, head_bounds);
                }
                return Line_tight_visual_bounds_px(start_f, end_f, line.style);
            },
            [&](RectangleAnnotation const &) -> RectPx {
                return Annotation_bounds(annotation);
            },
            [&](EllipseAnnotation const &) -> RectPx {
                return Annotation_bounds(annotation);
            },
            [&](ObfuscateAnnotation const &) -> RectPx {
                return Annotation_bounds(annotation);
            },
            [](TextAnnotation const &text) -> RectPx {
                return text.visual_bounds.Normalized();
            },
            [](BubbleAnnotation const &bubble) -> RectPx {
                return Bubble_visual_bounds(bubble);
            },
        },
        annotation.data);
}

RectPx Annotation_selection_frame_bounds(Annotation const &annotation) noexcept {
    RectPx const bounds = Annotation_visual_bounds(annotation).Normalized();
    if (bounds.Is_empty()) {
        return bounds;
    }

    switch (annotation.Kind()) {
    case AnnotationKind::Rectangle:
    case AnnotationKind::Ellipse:
    case AnnotationKind::Obfuscate:
        return RectPx::From_ltrb(bounds.left - 1, bounds.top - 1, bounds.right + 1,
                                 bounds.bottom + 1);
    case AnnotationKind::Freehand:
    case AnnotationKind::Line:
    case AnnotationKind::Text:
    case AnnotationKind::Bubble:
        return bounds;
    }
    return bounds;
}

bool Annotation_hits_point(Annotation const &annotation, PointPx point) noexcept {
    return std::visit(
        Overloaded{
            [&](FreehandStrokeAnnotation const &fh) -> bool {
                if (fh.points.empty()) {
                    return false;
                }
                float const cx = static_cast<float>(point.x) + kHalf;
                float const cy = static_cast<float>(point.y) + kHalf;
                if (fh.freehand_tip_shape == FreehandTipShape::Square) {
                    return Pixel_covered_by_square_capped_polyline(cx, cy, fh.points,
                                                                   fh.style.width_px);
                }
                float const radius =
                    std::max(1.0F, static_cast<float>(fh.style.width_px)) * kHalf;
                return Pixel_covered_by_polyline(cx, cy, fh.points, radius * radius);
            },
            [&](LineAnnotation const &line) -> bool {
                // Use 4x4 supersampling at the queried pixel to match raster behavior.
                constexpr int samples = kLineRasterSamplesPerAxis;
                constexpr float step = 1.0F / static_cast<float>(samples);
                PointF const start_f = To_point_f(line.start);
                PointF const end_f = To_point_f(line.end);
                if (line.arrow_head) {
                    ArrowGeometry const geom =
                        Build_arrow_geometry(start_f, end_f, line.style);
                    for (int sy = 0; sy < samples; ++sy) {
                        float const sample_y = static_cast<float>(point.y) +
                                               (static_cast<float>(sy) + kHalf) * step;
                        for (int sx = 0; sx < samples; ++sx) {
                            float const sample_x =
                                static_cast<float>(point.x) +
                                (static_cast<float>(sx) + kHalf) * step;
                            if (Sample_covered_by_arrow(sample_x, sample_y, geom)) {
                                return true;
                            }
                        }
                    }
                    return false;
                }
                LineRasterFrame const frame =
                    Build_line_raster_frame(start_f, end_f, line.style);
                for (int sy = 0; sy < samples; ++sy) {
                    float const sample_y = static_cast<float>(point.y) +
                                           (static_cast<float>(sy) + kHalf) * step;
                    for (int sx = 0; sx < samples; ++sx) {
                        float const sample_x = static_cast<float>(point.x) +
                                               (static_cast<float>(sx) + kHalf) * step;
                        if (Point_inside_line_shape(sample_x, sample_y, frame)) {
                            return true;
                        }
                    }
                }
                return false;
            },
            [&](RectangleAnnotation const &rect) -> bool {
                RectPx const r = rect.outer_bounds.Normalized();
                if (!r.Contains(point)) {
                    return false;
                }
                if (rect.filled) {
                    return true;
                }
                int32_t const inset =
                    std::max<int32_t>(StrokeStyle::kMinWidthPx, rect.style.width_px);
                RectPx const inner = RectPx::From_ltrb(
                    r.left + inset, r.top + inset, r.right - inset, r.bottom - inset);
                return inner.Is_empty() || !inner.Contains(point);
            },
            [&](EllipseAnnotation const &ellipse) -> bool {
                return Point_inside_ellipse_outline(static_cast<float>(point.x) + kHalf,
                                                    static_cast<float>(point.y) + kHalf,
                                                    ellipse);
            },
            [&](ObfuscateAnnotation const &obfuscate) -> bool {
                return obfuscate.bounds.Normalized().Contains(point);
            },
            [&](TextAnnotation const &text) -> bool {
                std::optional<size_t> const offset =
                    Text_bitmap_pixel_offset(text, point);
                return offset.has_value() && text.premultiplied_bgra[*offset + 3u] > 0;
            },
            [&](BubbleAnnotation const &bubble) -> bool {
                if (bubble.diameter_px <= 0) {
                    return false;
                }
                float const cx = static_cast<float>(bubble.center.x);
                float const cy = static_cast<float>(bubble.center.y);
                float const px = static_cast<float>(point.x) + kHalf;
                float const py = static_cast<float>(point.y) + kHalf;
                float const r = static_cast<float>(bubble.diameter_px) * kHalf;
                float const dx = px - cx;
                float const dy = py - cy;
                return (dx * dx + dy * dy) <= (r * r);
            },
        },
        annotation.data);
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
    std::visit(Overloaded{
                   [&](FreehandStrokeAnnotation &fh) noexcept {
                       for (PointPx &point : fh.points) {
                           point.x += delta.x;
                           point.y += delta.y;
                       }
                   },
                   [&](LineAnnotation &line) noexcept {
                       line.start.x += delta.x;
                       line.start.y += delta.y;
                       line.end.x += delta.x;
                       line.end.y += delta.y;
                   },
                   [&](RectangleAnnotation &rect) noexcept {
                       rect.outer_bounds =
                           RectPx::From_ltrb(rect.outer_bounds.left + delta.x,
                                             rect.outer_bounds.top + delta.y,
                                             rect.outer_bounds.right + delta.x,
                                             rect.outer_bounds.bottom + delta.y);
                   },
                   [&](EllipseAnnotation &ellipse) noexcept {
                       ellipse.outer_bounds =
                           RectPx::From_ltrb(ellipse.outer_bounds.left + delta.x,
                                             ellipse.outer_bounds.top + delta.y,
                                             ellipse.outer_bounds.right + delta.x,
                                             ellipse.outer_bounds.bottom + delta.y);
                   },
                   [&](ObfuscateAnnotation &obfuscate) noexcept {
                       obfuscate.bounds =
                           RectPx::From_ltrb(obfuscate.bounds.left + delta.x,
                                             obfuscate.bounds.top + delta.y,
                                             obfuscate.bounds.right + delta.x,
                                             obfuscate.bounds.bottom + delta.y);
                   },
                   [&](TextAnnotation &text) noexcept {
                       text.origin.x += delta.x;
                       text.origin.y += delta.y;
                       text.visual_bounds =
                           RectPx::From_ltrb(text.visual_bounds.left + delta.x,
                                             text.visual_bounds.top + delta.y,
                                             text.visual_bounds.right + delta.x,
                                             text.visual_bounds.bottom + delta.y);
                   },
                   [&](BubbleAnnotation &bubble) noexcept {
                       bubble.center.x += delta.x;
                       bubble.center.y += delta.y;
                   },
               },
               annotation.data);
    return annotation;
}

} // namespace greenflame::core
