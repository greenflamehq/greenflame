#include "greenflame_core/freehand_annotation_tool.h"

namespace greenflame::core {

FreehandAnnotationTool::FreehandAnnotationTool()
    : descriptor_{AnnotationToolId::Freehand, L"Brush tool", L'B', L"B",
                  AnnotationToolbarGlyph::Brush} {}

AnnotationToolDescriptor const &FreehandAnnotationTool::Descriptor() const noexcept {
    return descriptor_;
}

void FreehandAnnotationTool::Reset() noexcept {
    drawing_ = false;
    points_.clear();
    Invalidate_draft();
}

bool FreehandAnnotationTool::Has_active_gesture() const noexcept { return drawing_; }

bool FreehandAnnotationTool::On_primary_press(IAnnotationToolHost &host,
                                              PointPx cursor) {
    (void)host;
    drawing_ = true;
    points_.clear();
    points_.push_back(cursor);
    Invalidate_draft();
    return true;
}

bool FreehandAnnotationTool::On_pointer_move(IAnnotationToolHost &host,
                                             PointPx cursor) {
    (void)host;
    if (!drawing_) {
        return false;
    }
    if (!points_.empty() && points_.back() == cursor) {
        return false;
    }
    points_.push_back(cursor);
    Invalidate_draft();
    return true;
}

bool FreehandAnnotationTool::On_primary_release(IAnnotationToolHost &host,
                                                UndoStack &undo_stack) {
    if (!drawing_) {
        return false;
    }

    drawing_ = false;
    if (points_.empty()) {
        Invalidate_draft();
        return true;
    }

    Annotation annotation = Build_annotation(host, points_);
    points_.clear();
    Invalidate_draft();
    host.Commit_new_annotation(undo_stack, std::move(annotation));
    return true;
}

bool FreehandAnnotationTool::On_cancel(IAnnotationToolHost &host) {
    (void)host;
    if (!drawing_) {
        return false;
    }
    drawing_ = false;
    points_.clear();
    Invalidate_draft();
    return true;
}

Annotation const *FreehandAnnotationTool::Draft_annotation(
    IAnnotationToolHost const &host) const noexcept {
    if (!drawing_ || points_.empty()) {
        return nullptr;
    }
    if (!draft_annotation_cache_.has_value()) {
        draft_annotation_cache_ = Build_annotation(host, points_);
    }
    return &*draft_annotation_cache_;
}

std::span<const PointPx>
FreehandAnnotationTool::Draft_freehand_points() const noexcept {
    return points_;
}

std::optional<StrokeStyle> FreehandAnnotationTool::Draft_freehand_style(
    IAnnotationToolHost const &host) const noexcept {
    if (!drawing_ || points_.empty()) {
        return std::nullopt;
    }
    return host.Current_stroke_style();
}

void FreehandAnnotationTool::On_stroke_style_changed() noexcept { Invalidate_draft(); }

Annotation
FreehandAnnotationTool::Build_annotation(IAnnotationToolHost const &host,
                                         std::span<const PointPx> raw_points) const {
    Annotation annotation{};
    annotation.id = host.Next_annotation_id();
    annotation.kind = AnnotationKind::Freehand;
    annotation.freehand.style = host.Current_stroke_style();
    annotation.freehand.points = host.Smooth_points(raw_points);
    annotation.freehand.raster = Rasterize_freehand_stroke(annotation.freehand.points,
                                                           annotation.freehand.style);
    return annotation;
}

void FreehandAnnotationTool::Invalidate_draft() noexcept {
    draft_annotation_cache_.reset();
}

} // namespace greenflame::core
