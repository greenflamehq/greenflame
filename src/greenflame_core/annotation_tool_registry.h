#pragma once

#include "greenflame_core/annotation_tool.h"

namespace greenflame::core {

class AnnotationToolRegistry final {
  public:
    AnnotationToolRegistry();

    [[nodiscard]] IAnnotationTool const *Find_by_id(AnnotationToolId id) const noexcept;
    [[nodiscard]] IAnnotationTool *Find_by_id(AnnotationToolId id) noexcept;
    [[nodiscard]] IAnnotationTool const *Find_by_hotkey(wchar_t hotkey,
                                                        bool shift) const noexcept;
    [[nodiscard]] IAnnotationTool *Find_by_hotkey(wchar_t hotkey, bool shift) noexcept;
    [[nodiscard]] std::vector<AnnotationToolbarButtonView>
    Build_toolbar_button_views(std::optional<AnnotationToolId> active_tool) const;
    void Reset_all() noexcept;
    void On_stroke_style_changed() noexcept;

  private:
    std::vector<std::unique_ptr<IAnnotationTool>> tools_ = {};
};

} // namespace greenflame::core
