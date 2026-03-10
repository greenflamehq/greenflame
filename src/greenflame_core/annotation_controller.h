#pragma once

#include "greenflame_core/annotation_edit_interaction.h"
#include "greenflame_core/annotation_tool_registry.h"

namespace greenflame::core {

class UndoStack;

class IStrokeSmoother {
  public:
    IStrokeSmoother() = default;
    IStrokeSmoother(IStrokeSmoother const &) = default;
    IStrokeSmoother &operator=(IStrokeSmoother const &) = default;
    IStrokeSmoother(IStrokeSmoother &&) = default;
    IStrokeSmoother &operator=(IStrokeSmoother &&) = default;
    virtual ~IStrokeSmoother() = default;
    [[nodiscard]] virtual std::vector<PointPx>
    Smooth(std::span<const PointPx> points) const = 0;
};

class PassthroughStrokeSmoother final : public IStrokeSmoother {
  public:
    [[nodiscard]] std::vector<PointPx>
    Smooth(std::span<const PointPx> points) const override;
};

class AnnotationController final : public IAnnotationToolHost,
                                   public IAnnotationEditInteractionHost {
  public:
    AnnotationController();
    AnnotationController(AnnotationController const &) = delete;
    AnnotationController &operator=(AnnotationController const &) = delete;
    AnnotationController(AnnotationController &&) = default;
    AnnotationController &operator=(AnnotationController &&) = default;

    void Reset_for_session();

    [[nodiscard]] std::optional<AnnotationToolId> Active_tool() const noexcept {
        return active_tool_;
    }
    [[nodiscard]] bool Has_active_tool() const noexcept {
        return active_tool_.has_value();
    }
    [[nodiscard]] bool Toggle_tool(AnnotationToolId id);
    [[nodiscard]] bool Toggle_tool_by_hotkey(wchar_t hotkey);
    [[nodiscard]] std::vector<AnnotationToolbarButtonView>
    Build_toolbar_button_views() const;
    [[nodiscard]] std::optional<AnnotationToolId>
    Tool_id_from_hotkey(wchar_t hotkey) const;
    [[nodiscard]] int32_t Brush_width_px() const noexcept {
        return brush_style_.width_px;
    }
    [[nodiscard]] bool Set_brush_width_px(int32_t width_px) noexcept;
    [[nodiscard]] COLORREF Annotation_color() const noexcept {
        return brush_style_.color;
    }
    [[nodiscard]] bool Set_annotation_color(COLORREF color) noexcept;

    [[nodiscard]] std::span<const Annotation> Annotations() const noexcept {
        return document_.annotations;
    }
    [[nodiscard]] Annotation const *Draft_annotation() const noexcept;
    [[nodiscard]] std::span<const PointPx> Draft_freehand_points() const noexcept;
    [[nodiscard]] std::optional<StrokeStyle> Draft_freehand_style() const noexcept;
    [[nodiscard]] std::optional<double> Draft_line_angle_radians() const noexcept;
    [[nodiscard]] std::optional<uint64_t> Selected_annotation_id() const noexcept {
        return document_.selected_annotation_id;
    }
    [[nodiscard]] Annotation const *Selected_annotation() const noexcept;
    [[nodiscard]] std::optional<RectPx> Selected_annotation_bounds() const noexcept;
    [[nodiscard]] std::optional<AnnotationEditTarget>
    Annotation_edit_target_at(PointPx cursor) const noexcept;
    [[nodiscard]] bool Has_active_tool_gesture() const noexcept;
    [[nodiscard]] bool Has_active_edit_interaction() const noexcept;
    [[nodiscard]] bool Has_active_gesture() const noexcept;
    [[nodiscard]] bool Has_annotations() const noexcept {
        return !document_.annotations.empty();
    }
    [[nodiscard]] bool Is_annotation_dragging() const noexcept {
        return active_edit_interaction_ != nullptr &&
               active_edit_interaction_->Is_move_drag();
    }
    [[nodiscard]] std::optional<AnnotationEditHandleKind>
    Active_annotation_edit_handle() const noexcept;

    [[nodiscard]] bool On_primary_press(PointPx cursor);
    [[nodiscard]] bool On_pointer_move(PointPx cursor);
    [[nodiscard]] bool On_primary_release(UndoStack &undo_stack);
    [[nodiscard]] bool On_cancel();
    [[nodiscard]] bool Delete_selected_annotation(UndoStack &undo_stack);
    void Clear_annotations() noexcept;
    void Reset_for_selection_mode() noexcept;

    // Tool-support API
    [[nodiscard]] std::optional<uint64_t>
    Annotation_id_at(PointPx cursor) const noexcept;
    [[nodiscard]] bool
    Set_selected_annotation(std::optional<uint64_t> selected_annotation_id) noexcept;
    [[nodiscard]] bool Select_topmost_annotation(PointPx cursor);
    [[nodiscard]] bool Begin_annotation_edit(AnnotationEditTarget target,
                                             PointPx cursor);

    void Update_annotation_at(size_t index, Annotation annotation,
                              std::optional<uint64_t> selected_annotation_id) override;
    void Insert_annotation_at(size_t index, Annotation annotation,
                              std::optional<uint64_t> selected_annotation_id);
    void Erase_annotation_at(size_t index,
                             std::optional<uint64_t> selected_annotation_id);

  private:
    [[nodiscard]] StrokeStyle Current_stroke_style() const noexcept override;
    [[nodiscard]] uint64_t Next_annotation_id() const noexcept override;
    [[nodiscard]] std::vector<PointPx>
    Smooth_points(std::span<const PointPx> points) const override;
    void Commit_new_annotation(UndoStack &undo_stack, Annotation annotation) override;
    [[nodiscard]] Annotation const *Annotation_at(size_t index) const noexcept override;

    [[nodiscard]] IAnnotationTool *Active_tool_impl() noexcept;
    [[nodiscard]] IAnnotationTool const *Active_tool_impl() const noexcept;
    [[nodiscard]] std::optional<size_t> Selected_annotation_index() const noexcept;

    AnnotationDocument document_ = {};
    AnnotationToolRegistry registry_ = {};
    PassthroughStrokeSmoother smoother_ = {};
    std::optional<AnnotationToolId> active_tool_ = std::nullopt;
    StrokeStyle brush_style_ = {};
    std::unique_ptr<IAnnotationEditInteraction> active_edit_interaction_ = {};
};

} // namespace greenflame::core
