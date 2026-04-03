#pragma once

#include "greenflame_core/annotation_types.h"

namespace greenflame::core {

[[nodiscard]] RectPx Annotation_bounds(Annotation const &annotation) noexcept;
[[nodiscard]] RectPx Annotation_visual_bounds(Annotation const &annotation) noexcept;
[[nodiscard]] RectPx
Annotation_selection_frame_bounds(Annotation const &annotation) noexcept;
[[nodiscard]] bool Annotation_hits_point(Annotation const &annotation,
                                         PointPx point) noexcept;
[[nodiscard]] std::optional<size_t>
Index_of_topmost_annotation_at(std::span<const Annotation> annotations,
                               PointPx point) noexcept;
[[nodiscard]] std::optional<size_t>
Index_of_annotation_id(std::span<const Annotation> annotations, uint64_t id) noexcept;
[[nodiscard]] std::optional<AnnotationLineEndpoint>
Hit_test_line_endpoint_handles(PointPx start, PointPx end, PointPx cursor) noexcept;
[[nodiscard]] RectPx Rectangle_outer_bounds_from_corners(PointPx a, PointPx b) noexcept;
[[nodiscard]] PointPx Rectangle_resize_handle_center(RectPx outer_bounds,
                                                     SelectionHandle handle) noexcept;
[[nodiscard]] std::array<bool, 8>
Visible_rectangle_resize_handles(RectPx outer_bounds) noexcept;
[[nodiscard]] std::optional<SelectionHandle>
Hit_test_rectangle_resize_handles(RectPx outer_bounds, PointPx cursor) noexcept;
[[nodiscard]] RectPx Resize_rectangle_from_handle(RectPx outer_bounds,
                                                  SelectionHandle handle,
                                                  PointPx cursor) noexcept;
[[nodiscard]] Annotation Translate_annotation(Annotation annotation,
                                              PointPx delta) noexcept;

} // namespace greenflame::core
