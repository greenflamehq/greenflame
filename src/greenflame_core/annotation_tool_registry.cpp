#include "greenflame_core/annotation_tool_registry.h"

#include "greenflame_core/annotation_controller.h"
#include "greenflame_core/undo_stack.h"

namespace greenflame::core {

namespace {

[[nodiscard]] wchar_t Normalize_hotkey(wchar_t hotkey) noexcept {
    return static_cast<wchar_t>(std::towupper(hotkey));
}

class BrushTool final : public IAnnotationTool {
  public:
    BrushTool()
        : descriptor_{AnnotationToolId::Freehand, L"Brush tool", L'B', L"B",
                      AnnotationToolbarGlyph::Brush} {}

    [[nodiscard]] AnnotationToolDescriptor const &Descriptor() const noexcept override {
        return descriptor_;
    }

    [[nodiscard]] bool On_primary_press(AnnotationController &controller,
                                        PointPx cursor) override {
        controller.Begin_freehand_stroke(cursor);
        return true;
    }

    [[nodiscard]] bool On_pointer_move(AnnotationController &controller,
                                       PointPx cursor) override {
        return controller.Append_freehand_point(cursor);
    }

    [[nodiscard]] bool On_primary_release(AnnotationController &controller,
                                          UndoStack &undo_stack) override {
        return controller.Commit_freehand_stroke(undo_stack);
    }

    [[nodiscard]] bool On_cancel(AnnotationController &controller) override {
        return controller.Cancel_freehand_stroke();
    }

  private:
    AnnotationToolDescriptor descriptor_;
};

} // namespace

AnnotationToolRegistry::AnnotationToolRegistry() {
    tools_.push_back(std::make_unique<BrushTool>());
}

IAnnotationTool const *
AnnotationToolRegistry::Find_by_id(AnnotationToolId id) const noexcept {
    for (auto const &tool : tools_) {
        if (tool->Descriptor().id == id) {
            return tool.get();
        }
    }
    return nullptr;
}

IAnnotationTool *AnnotationToolRegistry::Find_by_id(AnnotationToolId id) noexcept {
    return const_cast<IAnnotationTool *>(
        static_cast<AnnotationToolRegistry const *>(this)->Find_by_id(id));
}

IAnnotationTool const *
AnnotationToolRegistry::Find_by_hotkey(wchar_t hotkey) const noexcept {
    wchar_t const normalized = Normalize_hotkey(hotkey);
    for (auto const &tool : tools_) {
        if (Normalize_hotkey(tool->Descriptor().hotkey) == normalized) {
            return tool.get();
        }
    }
    return nullptr;
}

IAnnotationTool *AnnotationToolRegistry::Find_by_hotkey(wchar_t hotkey) noexcept {
    return const_cast<IAnnotationTool *>(
        static_cast<AnnotationToolRegistry const *>(this)->Find_by_hotkey(hotkey));
}

std::vector<AnnotationToolbarButtonView>
AnnotationToolRegistry::Build_toolbar_button_views(
    std::optional<AnnotationToolId> active_tool) const {
    std::vector<AnnotationToolbarButtonView> views;
    views.reserve(tools_.size());
    for (auto const &tool : tools_) {
        AnnotationToolDescriptor const &descriptor = tool->Descriptor();
        views.push_back(AnnotationToolbarButtonView{
            descriptor.id, descriptor.toolbar_label, descriptor.name,
            descriptor.toolbar_glyph,
            active_tool.has_value() && descriptor.id == *active_tool});
    }
    return views;
}

} // namespace greenflame::core
