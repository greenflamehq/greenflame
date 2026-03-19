#include "greenflame_core/ellipse_annotation_tool.h"

#include "greenflame_core/annotation_hit_test.h"

namespace greenflame::core {

EllipseAnnotationTool::EllipseAnnotationTool(AnnotationToolDescriptor descriptor,
                                             bool filled)
    : descriptor_(std::move(descriptor)), filled_(filled) {}

AnnotationToolDescriptor const &EllipseAnnotationTool::Descriptor() const noexcept {
    return descriptor_;
}

void EllipseAnnotationTool::Reset() noexcept {
    drawing_ = false;
    start_ = {};
    end_ = {};
    Invalidate_draft();
}

bool EllipseAnnotationTool::Has_active_gesture() const noexcept { return drawing_; }

bool EllipseAnnotationTool::On_primary_press(IAnnotationToolHost &host,
                                             PointPx cursor) {
    (void)host;
    drawing_ = true;
    start_ = cursor;
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool EllipseAnnotationTool::On_pointer_move(IAnnotationToolHost &host, PointPx cursor) {
    (void)host;
    if (!drawing_ || end_ == cursor) {
        return false;
    }
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool EllipseAnnotationTool::On_primary_release(IAnnotationToolHost &host,
                                               UndoStack &undo_stack) {
    if (!drawing_) {
        return false;
    }

    drawing_ = false;
    if (start_ == end_) {
        start_ = {};
        end_ = {};
        Invalidate_draft();
        return false;
    }
    Annotation annotation = Build_annotation(host, start_, end_);
    start_ = {};
    end_ = {};
    Invalidate_draft();
    host.Commit_new_annotation(undo_stack, std::move(annotation));
    return true;
}

bool EllipseAnnotationTool::On_cancel(IAnnotationToolHost &host) {
    (void)host;
    if (!drawing_) {
        return false;
    }
    drawing_ = false;
    start_ = {};
    end_ = {};
    Invalidate_draft();
    return true;
}

Annotation const *EllipseAnnotationTool::Draft_annotation(
    IAnnotationToolHost const &host) const noexcept {
    if (!drawing_) {
        return nullptr;
    }
    if (!draft_annotation_cache_.has_value()) {
        draft_annotation_cache_ = Build_annotation(host, start_, end_);
    }
    return &*draft_annotation_cache_;
}

void EllipseAnnotationTool::On_stroke_style_changed() noexcept { Invalidate_draft(); }

Annotation EllipseAnnotationTool::Build_annotation(IAnnotationToolHost const &host,
                                                   PointPx start, PointPx end) const {
    Annotation annotation{};
    annotation.id = host.Next_annotation_id();
    annotation.data = EllipseAnnotation{
        .outer_bounds = Rectangle_outer_bounds_from_corners(start, end),
        .style = host.Current_stroke_style(),
        .filled = filled_,
    };
    return annotation;
}

void EllipseAnnotationTool::Invalidate_draft() noexcept {
    draft_annotation_cache_.reset();
}

} // namespace greenflame::core
