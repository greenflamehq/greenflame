#include "greenflame_core/obfuscate_annotation_tool.h"

#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/obfuscate_annotation_types.h"

namespace greenflame::core {

namespace {

[[nodiscard]] AnnotationToolDescriptor Obfuscate_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::Obfuscate, L"Obfuscate tool",
                                    L'O', L"O", AnnotationToolbarGlyph::Obfuscate};
}

} // namespace

ObfuscateAnnotationTool::ObfuscateAnnotationTool()
    : descriptor_(Obfuscate_tool_descriptor()) {}

AnnotationToolDescriptor const &ObfuscateAnnotationTool::Descriptor() const noexcept {
    return descriptor_;
}

void ObfuscateAnnotationTool::Reset() noexcept {
    drawing_ = false;
    start_ = {};
    end_ = {};
    Invalidate_draft();
}

bool ObfuscateAnnotationTool::Has_active_gesture() const noexcept { return drawing_; }

bool ObfuscateAnnotationTool::On_primary_press(IAnnotationToolHost &host,
                                               PointPx cursor) {
    (void)host;
    drawing_ = true;
    start_ = cursor;
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool ObfuscateAnnotationTool::On_pointer_move(IAnnotationToolHost &host,
                                              PointPx cursor) {
    (void)host;
    if (!drawing_ || end_ == cursor) {
        return false;
    }
    end_ = cursor;
    Invalidate_draft();
    return true;
}

bool ObfuscateAnnotationTool::On_primary_release(IAnnotationToolHost &host,
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

    RectPx const bounds = Rectangle_outer_bounds_from_corners(start_, end_);
    std::optional<Annotation> annotation = host.Build_obfuscate_annotation(bounds);
    start_ = {};
    end_ = {};
    Invalidate_draft();
    if (!annotation.has_value()) {
        return false;
    }
    host.Commit_new_annotation(undo_stack, std::move(*annotation));
    return true;
}

bool ObfuscateAnnotationTool::On_cancel(IAnnotationToolHost &host) {
    (void)host;
    if (!drawing_) {
        return false;
    }
    Reset();
    return true;
}

Annotation const *ObfuscateAnnotationTool::Draft_annotation(
    IAnnotationToolHost const &host) const noexcept {
    if (!drawing_) {
        return nullptr;
    }
    if (!draft_annotation_cache_.has_value()) {
        draft_annotation_cache_ = Build_draft_annotation(host);
    }
    return &*draft_annotation_cache_;
}

void ObfuscateAnnotationTool::On_stroke_style_changed() noexcept { Invalidate_draft(); }

Annotation
ObfuscateAnnotationTool::Build_draft_annotation(IAnnotationToolHost const &host) const {
    Annotation annotation{};
    annotation.id = host.Next_annotation_id();
    annotation.data = ObfuscateAnnotation{
        .bounds = Rectangle_outer_bounds_from_corners(start_, end_),
        .block_size = host.Current_obfuscate_block_size(),
    };
    return annotation;
}

void ObfuscateAnnotationTool::Invalidate_draft() noexcept {
    draft_annotation_cache_.reset();
}

} // namespace greenflame::core
