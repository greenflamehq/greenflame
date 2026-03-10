#include "greenflame_core/rectangle_annotation_tool.h"

#include "greenflame_core/annotation_hit_test.h"

namespace greenflame::core {

RectangleAnnotationTool::RectangleAnnotationTool(AnnotationToolDescriptor descriptor,
                                                 bool filled)
    : descriptor_(std::move(descriptor)), filled_(filled) {}

AnnotationToolDescriptor const &RectangleAnnotationTool::Descriptor() const noexcept {
    return descriptor_;
}

void RectangleAnnotationTool::Reset() noexcept {
    drawing_ = false;
    start_ = {};
    end_ = {};
    Invalidate_draft();
}

bool RectangleAnnotationTool::Has_active_gesture() const noexcept { return drawing_; }

bool RectangleAnnotationTool::On_primary_press(IAnnotationToolHost &host,
                                               PointPx cursor) {
    (void)host;
    drawing_ = true;
    start_ = cursor;
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool RectangleAnnotationTool::On_pointer_move(IAnnotationToolHost &host,
                                              PointPx cursor) {
    (void)host;
    if (!drawing_ || end_ == cursor) {
        return false;
    }
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool RectangleAnnotationTool::On_primary_release(IAnnotationToolHost &host,
                                                 UndoStack &undo_stack) {
    if (!drawing_) {
        return false;
    }

    drawing_ = false;
    Annotation annotation = Build_annotation(host, start_, end_);
    start_ = {};
    end_ = {};
    Invalidate_draft();
    host.Commit_new_annotation(undo_stack, std::move(annotation));
    return true;
}

bool RectangleAnnotationTool::On_cancel(IAnnotationToolHost &host) {
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

Annotation const *RectangleAnnotationTool::Draft_annotation(
    IAnnotationToolHost const &host) const noexcept {
    if (!drawing_) {
        return nullptr;
    }
    if (!draft_annotation_cache_.has_value()) {
        draft_annotation_cache_ = Build_annotation(host, start_, end_);
    }
    return &*draft_annotation_cache_;
}

void RectangleAnnotationTool::On_stroke_style_changed() noexcept { Invalidate_draft(); }

Annotation RectangleAnnotationTool::Build_annotation(IAnnotationToolHost const &host,
                                                     PointPx start, PointPx end) const {
    Annotation annotation{};
    annotation.id = host.Next_annotation_id();
    annotation.kind = AnnotationKind::Rectangle;
    annotation.rectangle.outer_bounds = Rectangle_outer_bounds_from_corners(start, end);
    annotation.rectangle.style = host.Current_stroke_style();
    annotation.rectangle.filled = filled_;
    return annotation;
}

void RectangleAnnotationTool::Invalidate_draft() noexcept {
    draft_annotation_cache_.reset();
}

} // namespace greenflame::core
