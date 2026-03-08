#include "greenflame_core/line_annotation_tool.h"

namespace greenflame::core {

LineAnnotationTool::LineAnnotationTool()
    : descriptor_{AnnotationToolId::Line, L"Line tool", L'L', L"L",
                  AnnotationToolbarGlyph::Line} {}

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

std::optional<double> LineAnnotationTool::Draft_line_angle_radians() const noexcept {
    if (!drawing_) {
        return std::nullopt;
    }
    int32_t const dx = end_.x - start_.x;
    int32_t const dy = end_.y - start_.y;
    if (dx == 0 && dy == 0) {
        return std::nullopt;
    }
    return std::atan2(static_cast<double>(dy), static_cast<double>(dx));
}

void LineAnnotationTool::On_stroke_style_changed() noexcept { Invalidate_draft(); }

Annotation LineAnnotationTool::Build_annotation(IAnnotationToolHost const &host,
                                                PointPx start, PointPx end) const {
    Annotation annotation{};
    annotation.id = host.Next_annotation_id();
    annotation.kind = AnnotationKind::Line;
    annotation.line.start = start;
    annotation.line.end = end;
    annotation.line.style = host.Current_stroke_style();
    annotation.line.raster = Rasterize_line_segment(
        annotation.line.start, annotation.line.end, annotation.line.style);
    return annotation;
}

void LineAnnotationTool::Invalidate_draft() noexcept {
    draft_annotation_cache_.reset();
}

} // namespace greenflame::core
