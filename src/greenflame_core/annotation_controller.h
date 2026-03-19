#pragma once

#include "greenflame_core/annotation_edit_interaction.h"
#include "greenflame_core/annotation_tool_registry.h"
#include "greenflame_core/text_edit_controller.h"

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
    [[nodiscard]] bool Toggle_tool_by_hotkey(wchar_t hotkey, bool shift = false);
    [[nodiscard]] std::vector<AnnotationToolbarButtonView>
    Build_toolbar_button_views() const;
    [[nodiscard]] std::optional<AnnotationToolId>
    Tool_id_from_hotkey(wchar_t hotkey, bool shift = false) const;
    [[nodiscard]] int32_t Tool_size_step(AnnotationToolId tool) const noexcept;
    [[nodiscard]] int32_t Tool_physical_size(AnnotationToolId tool) const noexcept;
    [[nodiscard]] bool Set_tool_size_step(AnnotationToolId tool, int32_t step) noexcept;
    [[nodiscard]] COLORREF Annotation_color() const noexcept {
        return Active_tool() == AnnotationToolId::Highlighter ? highlighter_style_.color
                                                              : freehand_style_.color;
    }
    [[nodiscard]] bool Set_annotation_color(COLORREF color) noexcept;
    [[nodiscard]] COLORREF Brush_annotation_color() const noexcept {
        return freehand_style_.color;
    }
    [[nodiscard]] bool Set_brush_annotation_color(COLORREF color) noexcept;
    [[nodiscard]] COLORREF Highlighter_color() const noexcept {
        return highlighter_style_.color;
    }
    [[nodiscard]] bool Set_highlighter_color(COLORREF color) noexcept;
    [[nodiscard]] int32_t Highlighter_opacity_percent() const noexcept {
        return highlighter_style_.opacity_percent;
    }
    [[nodiscard]] bool
    Set_highlighter_opacity_percent(int32_t opacity_percent) noexcept;

    [[nodiscard]] std::span<const Annotation> Annotations() const noexcept {
        return document_.annotations;
    }
    [[nodiscard]] Annotation const *Draft_annotation() const noexcept;
    [[nodiscard]] std::span<const PointPx> Draft_freehand_points() const noexcept;
    [[nodiscard]] std::optional<StrokeStyle> Draft_freehand_style() const noexcept;
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
    void Set_text_layout_engine(ITextLayoutEngine *engine) noexcept;
    [[nodiscard]] bool Has_active_text_edit() const noexcept;
    [[nodiscard]] TextEditController *Active_text_edit() noexcept;
    [[nodiscard]] TextEditController const *Active_text_edit() const noexcept;
    void Begin_text_draft(PointPx origin);
    void Commit_text_annotation(UndoStack &undo_stack, TextAnnotation annotation);
    void Cancel_text_draft();
    [[nodiscard]] int32_t Text_point_size() const noexcept;
    [[nodiscard]] TextFontChoice Text_current_font() const noexcept;
    void Set_text_current_font(TextFontChoice choice) noexcept;
    [[nodiscard]] TextFontChoice Bubble_current_font() const noexcept;
    void Set_bubble_current_font(TextFontChoice choice) noexcept;
    [[nodiscard]] bool Has_annotations() const noexcept {
        return !document_.annotations.empty();
    }
    [[nodiscard]] bool Is_annotation_dragging() const noexcept {
        return active_edit_interaction_ != nullptr &&
               active_edit_interaction_->Is_move_drag();
    }
    [[nodiscard]] std::optional<AnnotationEditHandleKind>
    Active_annotation_edit_handle() const noexcept;

    [[nodiscard]] bool Straighten_highlighter_stroke() noexcept;

    [[nodiscard]] bool On_primary_press(PointPx cursor);
    [[nodiscard]] bool On_pointer_move(PointPx cursor, bool primary_down = false);
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

    // Called by AddBubbleAnnotationCommand on Undo/Redo.
    [[nodiscard]] int32_t Current_bubble_counter() const noexcept {
        return bubble_counter_;
    }
    void Increment_bubble_counter() noexcept { ++bubble_counter_; }
    void Decrement_bubble_counter() noexcept {
        if (bubble_counter_ > 1) {
            --bubble_counter_;
        }
    }

  private:
    [[nodiscard]] StrokeStyle Current_stroke_style() const noexcept override;
    [[nodiscard]] uint64_t Next_annotation_id() const noexcept override;
    [[nodiscard]] std::vector<PointPx>
    Smooth_points(std::span<const PointPx> points) const override;
    [[nodiscard]] std::optional<Annotation>
    Build_bubble_annotation(PointPx cursor) const override;
    void Commit_new_annotation(UndoStack &undo_stack, Annotation annotation) override;
    [[nodiscard]] Annotation const *Annotation_at(size_t index) const noexcept override;

    [[nodiscard]] IAnnotationTool *Active_tool_impl() noexcept;
    [[nodiscard]] IAnnotationTool const *Active_tool_impl() const noexcept;
    [[nodiscard]] std::optional<size_t> Selected_annotation_index() const noexcept;

    AnnotationDocument document_ = {};
    AnnotationToolRegistry registry_ = {};
    PassthroughStrokeSmoother smoother_ = {};
    std::optional<AnnotationToolId> active_tool_ = std::nullopt;
    StrokeStyle freehand_style_ = {};
    StrokeStyle line_style_ = {};
    StrokeStyle arrow_style_ = {};
    StrokeStyle rect_style_ = {};
    StrokeStyle ellipse_style_ = {};
    StrokeStyle highlighter_style_ = {};
    int32_t bubble_size_step_ = 10;
    int32_t text_size_step_ = 10;
    std::unique_ptr<IAnnotationEditInteraction> active_edit_interaction_ = {};
    ITextLayoutEngine *text_layout_engine_ = nullptr;
    std::optional<TextEditController> text_edit_ctrl_ = std::nullopt;
    TextFontChoice text_current_font_ = TextFontChoice::Sans;
    int32_t bubble_counter_ = 1;
    TextFontChoice bubble_current_font_ = TextFontChoice::Sans;
};

} // namespace greenflame::core
