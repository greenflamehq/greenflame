#include "greenflame_core/annotation_tool_registry.h"

#include "greenflame_core/bubble_annotation_tool.h"
#include "greenflame_core/ellipse_annotation_tool.h"
#include "greenflame_core/freehand_annotation_tool.h"
#include "greenflame_core/line_annotation_tool.h"
#include "greenflame_core/rectangle_annotation_tool.h"
#include "greenflame_core/text_annotation_tool.h"

namespace greenflame::core {

namespace {

[[nodiscard]] wchar_t Normalize_hotkey(wchar_t hotkey) noexcept {
    return static_cast<wchar_t>(std::towupper(hotkey));
}

} // namespace

AnnotationToolRegistry::AnnotationToolRegistry() {
    tools_.push_back(std::make_unique<FreehandAnnotationTool>());
    tools_.push_back(std::make_unique<FreehandAnnotationTool>(
        AnnotationToolDescriptor{AnnotationToolId::Highlighter, L"Highlighter tool",
                                 L'H', L"H", AnnotationToolbarGlyph::Highlighter},
        FreehandTipShape::Square));
    tools_.push_back(std::make_unique<LineAnnotationTool>(
        AnnotationToolDescriptor{AnnotationToolId::Line, L"Line tool", L'L', L"L",
                                 AnnotationToolbarGlyph::Line},
        false));
    tools_.push_back(std::make_unique<LineAnnotationTool>(
        AnnotationToolDescriptor{AnnotationToolId::Arrow, L"Arrow tool", L'A', L"A",
                                 AnnotationToolbarGlyph::Arrow},
        true));
    tools_.push_back(std::make_unique<RectangleAnnotationTool>(
        AnnotationToolDescriptor{AnnotationToolId::Rectangle, L"Rectangle tool", L'R',
                                 L"R", AnnotationToolbarGlyph::Rectangle},
        false));
    tools_.push_back(std::make_unique<RectangleAnnotationTool>(
        AnnotationToolDescriptor{AnnotationToolId::FilledRectangle,
                                 L"Filled rectangle tool", L'R', L"\u21e7R",
                                 AnnotationToolbarGlyph::FilledRectangle, true},
        true));
    tools_.push_back(std::make_unique<EllipseAnnotationTool>(
        AnnotationToolDescriptor{AnnotationToolId::Ellipse, L"Ellipse tool", L'E', L"E",
                                 AnnotationToolbarGlyph::Ellipse},
        false));
    tools_.push_back(std::make_unique<EllipseAnnotationTool>(
        AnnotationToolDescriptor{AnnotationToolId::FilledEllipse,
                                 L"Filled ellipse tool", L'E', L"\u21e7E",
                                 AnnotationToolbarGlyph::FilledEllipse, true},
        true));
    tools_.push_back(std::make_unique<TextAnnotationTool>());
    tools_.push_back(std::make_unique<BubbleAnnotationTool>());
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
AnnotationToolRegistry::Find_by_hotkey(wchar_t hotkey, bool shift) const noexcept {
    wchar_t const normalized = Normalize_hotkey(hotkey);
    for (auto const &tool : tools_) {
        AnnotationToolDescriptor const &desc = tool->Descriptor();
        if (Normalize_hotkey(desc.hotkey) == normalized &&
            desc.requires_shift == shift) {
            return tool.get();
        }
    }
    return nullptr;
}

IAnnotationTool *AnnotationToolRegistry::Find_by_hotkey(wchar_t hotkey,
                                                        bool shift) noexcept {
    return const_cast<IAnnotationTool *>(
        static_cast<AnnotationToolRegistry const *>(this)->Find_by_hotkey(hotkey,
                                                                          shift));
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

void AnnotationToolRegistry::Reset_all() noexcept {
    for (auto &tool : tools_) {
        tool->Reset();
    }
}

void AnnotationToolRegistry::On_stroke_style_changed() noexcept {
    for (auto &tool : tools_) {
        tool->On_stroke_style_changed();
    }
}

} // namespace greenflame::core
