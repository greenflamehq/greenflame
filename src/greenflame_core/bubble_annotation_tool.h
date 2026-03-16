#pragma once

#include "greenflame_core/annotation_tool.h"

namespace greenflame::core {

class BubbleAnnotationTool final : public IAnnotationTool {
  public:
    BubbleAnnotationTool();

    [[nodiscard]] AnnotationToolDescriptor const &Descriptor() const noexcept override;

    void Reset() noexcept override;
    [[nodiscard]] bool Has_active_gesture() const noexcept override;
    [[nodiscard]] bool On_primary_press(IAnnotationToolHost &host,
                                        PointPx cursor) override;
    [[nodiscard]] bool On_pointer_move(IAnnotationToolHost &host,
                                       PointPx cursor) override;
    [[nodiscard]] bool On_primary_release(IAnnotationToolHost &host,
                                          UndoStack &undo_stack) override;
    [[nodiscard]] bool On_cancel(IAnnotationToolHost &host) override;

  private:
    AnnotationToolDescriptor descriptor_ = {};
};

} // namespace greenflame::core
