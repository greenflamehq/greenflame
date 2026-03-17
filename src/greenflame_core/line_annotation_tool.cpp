#include "greenflame_core/line_annotation_tool.h"

namespace greenflame::core {

LineAnnotationTool::LineAnnotationTool(AnnotationToolDescriptor descriptor,
                                       bool arrow_head)
    : descriptor_(std::move(descriptor)), arrow_head_(arrow_head) {}

AnnotationToolDescriptor const &LineAnnotationTool::Descriptor() const noexcept {
    return descriptor_;
}

void LineAnnotationTool::Reset() noexcept {
    drawing_ = false;
    start_ = {};
    end_ = {};
    Invalidate_draft();
}

bool LineAnnotationTool::Has_active_gesture() const noexcept { return drawing_; }

bool LineAnnotationTool::On_primary_press(IAnnotationToolHost &host, PointPx cursor) {
    (void)host;
    drawing_ = true;
    start_ = cursor;
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool LineAnnotationTool::On_pointer_move(IAnnotationToolHost &host, PointPx cursor) {
    (void)host;
    if (!drawing_ || end_ == cursor) {
        return false;
    }
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool LineAnnotationTool::On_primary_release(IAnnotationToolHost &host,
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

bool LineAnnotationTool::On_cancel(IAnnotationToolHost &host) {
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

Annotation const *
LineAnnotationTool::Draft_annotation(IAnnotationToolHost const &host) const noexcept {
    if (!drawing_) {
        return nullptr;
    }
    if (!draft_annotation_cache_.has_value()) {
        draft_annotation_cache_ = Build_annotation(host, start_, end_);
    }
    return &*draft_annotation_cache_;
}

void LineAnnotationTool::On_stroke_style_changed() noexcept { Invalidate_draft(); }

Annotation LineAnnotationTool::Build_annotation(IAnnotationToolHost const &host,
                                                PointPx start, PointPx end) const {
    Annotation annotation{};
    annotation.id = host.Next_annotation_id();
    annotation.data = LineAnnotation{
        .start = start,
        .end = end,
        .style = host.Current_stroke_style(),
        .arrow_head = arrow_head_,
    };
    return annotation;
}

void LineAnnotationTool::Invalidate_draft() noexcept {
    draft_annotation_cache_.reset();
}

} // namespace greenflame::core
