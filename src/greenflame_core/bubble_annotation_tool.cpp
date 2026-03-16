#include "greenflame_core/bubble_annotation_tool.h"

namespace greenflame::core {

namespace {

[[nodiscard]] AnnotationToolDescriptor Bubble_tool_descriptor() {
    return AnnotationToolDescriptor{AnnotationToolId::Bubble, L"Bubble tool", L'N',
                                    L"N", AnnotationToolbarGlyph::Bubble};
}

} // namespace

BubbleAnnotationTool::BubbleAnnotationTool() : descriptor_(Bubble_tool_descriptor()) {}

AnnotationToolDescriptor const &BubbleAnnotationTool::Descriptor() const noexcept {
    return descriptor_;
}

void BubbleAnnotationTool::Reset() noexcept {}

bool BubbleAnnotationTool::Has_active_gesture() const noexcept { return false; }

// Bubble placement is handled in AnnotationController::On_primary_press /
// On_primary_release; the tool object itself is a minimal stub.
bool BubbleAnnotationTool::On_primary_press(IAnnotationToolHost &host, PointPx cursor) {
    (void)host;
    (void)cursor;
    return false;
}

bool BubbleAnnotationTool::On_pointer_move(IAnnotationToolHost &host, PointPx cursor) {
    (void)host;
    (void)cursor;
    return false;
}

bool BubbleAnnotationTool::On_primary_release(IAnnotationToolHost &host,
                                              UndoStack &undo_stack) {
    (void)host;
    (void)undo_stack;
    return false;
}

bool BubbleAnnotationTool::On_cancel(IAnnotationToolHost &host) {
    (void)host;
    return false;
}

} // namespace greenflame::core
