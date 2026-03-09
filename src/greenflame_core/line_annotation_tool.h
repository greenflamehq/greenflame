#pragma once

#include "greenflame_core/annotation_tool.h"

namespace greenflame::core {

class LineAnnotationTool final : public IAnnotationTool {
  public:
    LineAnnotationTool(AnnotationToolDescriptor descriptor, bool arrow_head);

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
    [[nodiscard]] Annotation const *
    Draft_annotation(IAnnotationToolHost const &host) const noexcept override;
    [[nodiscard]] std::optional<double>
    Draft_line_angle_radians() const noexcept override;
    void On_stroke_style_changed() noexcept override;

  private:
    [[nodiscard]] Annotation Build_annotation(IAnnotationToolHost const &host,
                                              PointPx start, PointPx end) const;
    void Invalidate_draft() noexcept;

    AnnotationToolDescriptor descriptor_ = {};
    bool arrow_head_ = false;
    bool drawing_ = false;
    PointPx start_ = {};
    PointPx end_ = {};
    mutable std::optional<Annotation> draft_annotation_cache_ = std::nullopt;
};

} // namespace greenflame::core
