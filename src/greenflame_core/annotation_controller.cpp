#include "greenflame_core/annotation_controller.h"

#include "greenflame_core/annotation_commands.h"
#include "greenflame_core/color_wheel.h"
#include "greenflame_core/undo_stack.h"

namespace greenflame::core {

namespace {

[[nodiscard]] int32_t Clamp_brush_width_px(int32_t width_px) noexcept {
    return std::clamp(width_px, StrokeStyle::kMinWidthPx, StrokeStyle::kMaxWidthPx);
}

[[nodiscard]] int32_t Clamp_opacity_percent(int32_t opacity_percent) noexcept {
    return std::clamp(opacity_percent, StrokeStyle::kMinOpacityPercent,
                      StrokeStyle::kMaxOpacityPercent);
}

[[nodiscard]] StrokeStyle Default_highlighter_style() noexcept {
    return StrokeStyle{
        .width_px = StrokeStyle::kDefaultWidthPx,
        .color = kDefaultHighlighterColorPalette[static_cast<size_t>(
            kDefaultHighlighterColorIndex)],
        .opacity_percent = kDefaultHighlighterOpacityPercent,
    };
}

constexpr std::array<int32_t, 24> kAllowedTextPointSizes = {{
    5,  8,  9,  10, 11, 12, 14, 16,  18,  20,  22,  24,
    26, 28, 36, 48, 72, 84, 96, 108, 144, 192, 216, 288,
}};

[[nodiscard]] TextFontChoice
Normalize_text_font_choice(TextFontChoice choice) noexcept {
    switch (choice) {
    case TextFontChoice::Sans:
    case TextFontChoice::Serif:
    case TextFontChoice::Mono:
    case TextFontChoice::Art:
        return choice;
    }
    return TextFontChoice::Sans;
}

[[nodiscard]] bool Text_annotation_has_text(TextAnnotation const &annotation) noexcept {
    for (TextRun const &run : annotation.runs) {
        if (!run.text.empty()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] size_t Find_text_size_index(int32_t point_size) noexcept {
    for (size_t index = 0; index < kAllowedTextPointSizes.size(); ++index) {
        if (kAllowedTextPointSizes[index] == point_size) {
            return index;
        }
    }

    size_t nearest_index = 0;
    int64_t nearest_distance =
        std::abs(static_cast<int64_t>(point_size) -
                 static_cast<int64_t>(kAllowedTextPointSizes.front()));
    for (size_t index = 1; index < kAllowedTextPointSizes.size(); ++index) {
        int64_t const distance =
            std::abs(static_cast<int64_t>(point_size) -
                     static_cast<int64_t>(kAllowedTextPointSizes[index]));
        if (distance < nearest_distance) {
            nearest_index = index;
            nearest_distance = distance;
        }
    }
    return nearest_index;
}

} // namespace

std::vector<PointPx>
PassthroughStrokeSmoother::Smooth(std::span<const PointPx> points) const {
    return {points.begin(), points.end()};
}

AnnotationController::AnnotationController()
    : highlighter_style_(Default_highlighter_style()) {}

void AnnotationController::Reset_for_session() {
    document_ = {};
    active_tool_.reset();
    brush_style_ = {};
    highlighter_style_ = Default_highlighter_style();
    active_edit_interaction_.reset();
    text_layout_engine_ = nullptr;
    text_edit_ctrl_.reset();
    text_size_points_ = kDefaultTextAnnotationPointSize;
    text_current_font_ = TextFontChoice::Sans;
    bubble_counter_ = 1;
    bubble_current_font_ = TextFontChoice::Sans;
    pending_bubble_cursor_.reset();
    registry_.Reset_all();
}

bool AnnotationController::Toggle_tool(AnnotationToolId id) {
    if (registry_.Find_by_id(id) == nullptr || Has_active_gesture()) {
        return false;
    }
    if (active_tool_ == id) {
        active_tool_.reset();
    } else {
        active_tool_ = id;
        document_.selected_annotation_id = std::nullopt;
    }
    return true;
}

bool AnnotationController::Toggle_tool_by_hotkey(wchar_t hotkey) {
    IAnnotationTool *const tool = registry_.Find_by_hotkey(hotkey);
    if (tool == nullptr) {
        return false;
    }
    return Toggle_tool(tool->Descriptor().id);
}

std::vector<AnnotationToolbarButtonView>
AnnotationController::Build_toolbar_button_views() const {
    return registry_.Build_toolbar_button_views(active_tool_);
}

std::optional<AnnotationToolId>
AnnotationController::Tool_id_from_hotkey(wchar_t hotkey) const {
    IAnnotationTool const *const tool = registry_.Find_by_hotkey(hotkey);
    if (tool == nullptr) {
        return std::nullopt;
    }
    return tool->Descriptor().id;
}

bool AnnotationController::Set_brush_width_px(int32_t width_px) noexcept {
    int32_t const clamped_width = Clamp_brush_width_px(width_px);
    // Both widths are always set together and stay in sync; either check suffices.
    if (brush_style_.width_px == clamped_width &&
        highlighter_style_.width_px == clamped_width) {
        return false;
    }
    brush_style_.width_px = clamped_width;
    highlighter_style_.width_px = clamped_width;
    registry_.On_stroke_style_changed();
    return true;
}

bool AnnotationController::Set_annotation_color(COLORREF color) noexcept {
    if (active_tool_ == AnnotationToolId::Highlighter) {
        return Set_highlighter_color(color);
    }
    return Set_brush_annotation_color(color);
}

bool AnnotationController::Set_brush_annotation_color(COLORREF color) noexcept {
    if (brush_style_.color == color) {
        return false;
    }
    brush_style_.color = color;
    registry_.On_stroke_style_changed();
    return true;
}

bool AnnotationController::Set_highlighter_color(COLORREF color) noexcept {
    if (highlighter_style_.color == color) {
        return false;
    }
    highlighter_style_.color = color;
    registry_.On_stroke_style_changed();
    return true;
}

bool AnnotationController::Set_highlighter_opacity_percent(
    int32_t opacity_percent) noexcept {
    int32_t const clamped_opacity = Clamp_opacity_percent(opacity_percent);
    if (highlighter_style_.opacity_percent == clamped_opacity) {
        return false;
    }
    highlighter_style_.opacity_percent = clamped_opacity;
    registry_.On_stroke_style_changed();
    return true;
}

Annotation const *AnnotationController::Draft_annotation() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    return tool == nullptr ? nullptr : tool->Draft_annotation(*this);
}

std::optional<StrokeStyle> AnnotationController::Draft_freehand_style() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return std::nullopt;
    }
    return tool->Draft_freehand_style(*this);
}

std::optional<double> AnnotationController::Draft_line_angle_radians() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return std::nullopt;
    }
    return tool->Draft_line_angle_radians();
}

std::optional<size_t> AnnotationController::Selected_annotation_index() const noexcept {
    if (!document_.selected_annotation_id.has_value()) {
        return std::nullopt;
    }
    return Index_of_annotation_id(document_.annotations,
                                  *document_.selected_annotation_id);
}

Annotation const *AnnotationController::Selected_annotation() const noexcept {
    std::optional<size_t> const index = Selected_annotation_index();
    if (!index.has_value()) {
        return nullptr;
    }
    return &document_.annotations[*index];
}

std::optional<RectPx>
AnnotationController::Selected_annotation_bounds() const noexcept {
    Annotation const *const selected = Selected_annotation();
    if (selected == nullptr) {
        return std::nullopt;
    }
    return Annotation_visual_bounds(*selected);
}

std::optional<AnnotationEditTarget>
AnnotationController::Annotation_edit_target_at(PointPx cursor) const noexcept {
    return Hit_test_annotation_edit_target(Selected_annotation(), document_.annotations,
                                           cursor);
}

bool AnnotationController::Has_active_tool_gesture() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    return tool != nullptr && tool->Has_active_gesture();
}

bool AnnotationController::Has_active_edit_interaction() const noexcept {
    return active_edit_interaction_ != nullptr;
}

std::optional<AnnotationEditHandleKind>
AnnotationController::Active_annotation_edit_handle() const noexcept {
    if (active_edit_interaction_ == nullptr) {
        return std::nullopt;
    }
    return active_edit_interaction_->Active_handle();
}

bool AnnotationController::Has_active_gesture() const noexcept {
    return Has_active_tool_gesture() || Has_active_edit_interaction() ||
           Has_active_text_edit() || pending_bubble_cursor_.has_value();
}

void AnnotationController::Set_text_layout_engine(ITextLayoutEngine *engine) noexcept {
    text_layout_engine_ = engine;
}

bool AnnotationController::Has_active_text_edit() const noexcept {
    return text_edit_ctrl_.has_value();
}

TextEditController *AnnotationController::Active_text_edit() noexcept {
    return text_edit_ctrl_.has_value() ? &*text_edit_ctrl_ : nullptr;
}

TextEditController const *AnnotationController::Active_text_edit() const noexcept {
    return text_edit_ctrl_.has_value() ? &*text_edit_ctrl_ : nullptr;
}

void AnnotationController::Begin_text_draft(PointPx origin) {
    if (!active_tool_.has_value() || *active_tool_ != AnnotationToolId::Text ||
        Has_active_tool_gesture() || Has_active_edit_interaction()) {
        return;
    }

    document_.selected_annotation_id = std::nullopt;
    TextAnnotationBaseStyle const base_style{
        brush_style_.color, Normalize_text_font_choice(text_current_font_),
        kAllowedTextPointSizes[Find_text_size_index(text_size_points_)]};
    text_edit_ctrl_.emplace(origin, base_style, text_layout_engine_);
}

void AnnotationController::Commit_text_annotation(UndoStack &undo_stack,
                                                  TextAnnotation annotation) {
    text_edit_ctrl_.reset();
    if (!Text_annotation_has_text(annotation) || text_layout_engine_ == nullptr) {
        return;
    }

    text_layout_engine_->Rasterize(annotation);
    Annotation committed{};
    committed.id = Next_annotation_id();
    committed.data = std::move(annotation);
    Commit_new_annotation(undo_stack, std::move(committed));
}

void AnnotationController::Cancel_text_draft() { text_edit_ctrl_.reset(); }

int32_t AnnotationController::Text_point_size() const noexcept {
    return text_size_points_;
}

void AnnotationController::Set_text_point_size(int32_t point_size) noexcept {
    text_size_points_ = kAllowedTextPointSizes[Find_text_size_index(point_size)];
}

bool AnnotationController::Step_text_size(int delta_steps) {
    if (delta_steps == 0) {
        return false;
    }

    size_t const current_index = Find_text_size_index(text_size_points_);
    int32_t next_index = static_cast<int32_t>(current_index) + delta_steps;
    next_index = std::clamp(next_index, 0,
                            static_cast<int32_t>(kAllowedTextPointSizes.size()) - 1);
    int32_t const next_size = kAllowedTextPointSizes[static_cast<size_t>(next_index)];
    if (text_size_points_ == next_size) {
        return false;
    }
    text_size_points_ = next_size;
    return true;
}

TextFontChoice AnnotationController::Text_current_font() const noexcept {
    return text_current_font_;
}

void AnnotationController::Set_text_current_font(TextFontChoice choice) noexcept {
    text_current_font_ = Normalize_text_font_choice(choice);
}

TextFontChoice AnnotationController::Bubble_current_font() const noexcept {
    return bubble_current_font_;
}

void AnnotationController::Set_bubble_current_font(TextFontChoice choice) noexcept {
    bubble_current_font_ = Normalize_text_font_choice(choice);
}

bool AnnotationController::On_primary_press(PointPx cursor) {
    if (active_tool_ == AnnotationToolId::Text) {
        if (!text_edit_ctrl_.has_value()) {
            Begin_text_draft(cursor);
            return text_edit_ctrl_.has_value();
        }

        TextDraftView const view = text_edit_ctrl_->Build_view();
        if (!view.Hit_bounds().Contains(cursor)) {
            return false;
        }
        text_edit_ctrl_->On_pointer_press(cursor);
        return true;
    }

    if (active_tool_ == AnnotationToolId::Bubble) {
        pending_bubble_cursor_ = cursor;
        return true;
    }

    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_primary_press(*this, cursor);
}

bool AnnotationController::On_pointer_move(PointPx cursor, bool primary_down) {
    if (active_edit_interaction_ != nullptr) {
        return active_edit_interaction_->Update(*this, cursor);
    }
    if (text_edit_ctrl_.has_value()) {
        text_edit_ctrl_->On_pointer_move(cursor, primary_down);
        return true;
    }
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_pointer_move(*this, cursor);
}

bool AnnotationController::On_primary_release(UndoStack &undo_stack) {
    if (active_edit_interaction_ != nullptr) {
        std::optional<AnnotationEditCommandData> const command =
            active_edit_interaction_->Commit();
        active_edit_interaction_.reset();
        if (!command.has_value()) {
            return false;
        }

        std::optional<uint64_t> const selection = document_.selected_annotation_id;
        undo_stack.Push(std::make_unique<UpdateAnnotationCommand>(
            this, command->index, command->annotation_before, command->annotation_after,
            selection, selection, command->description));
        return true;
    }

    if (pending_bubble_cursor_.has_value() && text_layout_engine_ != nullptr) {
        PointPx const cursor = *pending_bubble_cursor_;
        pending_bubble_cursor_.reset();

        BubbleAnnotation bubble{};
        bubble.center = cursor;
        bubble.diameter_px = brush_style_.width_px;
        bubble.color = brush_style_.color;
        bubble.font_choice = Normalize_text_font_choice(bubble_current_font_);
        bubble.counter_value = bubble_counter_;
        text_layout_engine_->Rasterize_bubble(bubble);

        Annotation committed{};
        committed.id = Next_annotation_id();
        committed.data = std::move(bubble);

        size_t const insert_index = document_.annotations.size();
        undo_stack.Push(std::make_unique<AddBubbleAnnotationCommand>(
            this, insert_index, std::move(committed), document_.selected_annotation_id,
            document_.selected_annotation_id));
        return true;
    }
    if (pending_bubble_cursor_.has_value()) {
        pending_bubble_cursor_.reset();
        return false;
    }

    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_primary_release(*this, undo_stack);
}

bool AnnotationController::On_cancel() {
    if (text_edit_ctrl_.has_value()) {
        text_edit_ctrl_->Cancel();
        text_edit_ctrl_.reset();
        return true;
    }
    if (pending_bubble_cursor_.has_value()) {
        pending_bubble_cursor_.reset();
        return true;
    }
    if (active_edit_interaction_ != nullptr) {
        bool const canceled = active_edit_interaction_->Cancel(*this);
        active_edit_interaction_.reset();
        if (canceled) {
            return true;
        }
    }
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_cancel(*this);
}

bool AnnotationController::Delete_selected_annotation(UndoStack &undo_stack) {
    if (Has_active_edit_interaction() ||
        !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    if (!index.has_value()) {
        document_.selected_annotation_id = std::nullopt;
        return true;
    }

    undo_stack.Push(std::make_unique<DeleteAnnotationCommand>(
        this, *index, document_.annotations[*index], document_.selected_annotation_id,
        std::nullopt));
    return true;
}

void AnnotationController::Clear_annotations() noexcept {
    document_.annotations.clear();
    document_.selected_annotation_id = std::nullopt;
    active_edit_interaction_.reset();
    text_edit_ctrl_.reset();
    pending_bubble_cursor_.reset();
    registry_.Reset_all();
}

void AnnotationController::Reset_for_selection_mode() noexcept {
    active_tool_.reset();
    Clear_annotations();
}

std::optional<uint64_t>
AnnotationController::Annotation_id_at(PointPx cursor) const noexcept {
    std::optional<size_t> const index =
        Index_of_topmost_annotation_at(document_.annotations, cursor);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return document_.annotations[*index].id;
}

bool AnnotationController::Set_selected_annotation(
    std::optional<uint64_t> selected_annotation_id) noexcept {
    if (active_tool_.has_value()) {
        selected_annotation_id = std::nullopt;
    }
    if (document_.selected_annotation_id == selected_annotation_id) {
        return false;
    }
    document_.selected_annotation_id = selected_annotation_id;
    return true;
}

bool AnnotationController::Select_topmost_annotation(PointPx cursor) {
    return Set_selected_annotation(Annotation_id_at(cursor));
}

std::span<const PointPx> AnnotationController::Draft_freehand_points() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return {};
    }
    return tool->Draft_freehand_points();
}

StrokeStyle AnnotationController::Current_stroke_style() const noexcept {
    if (active_tool_ == AnnotationToolId::Highlighter) {
        return highlighter_style_;
    }
    return brush_style_;
}

uint64_t AnnotationController::Next_annotation_id() const noexcept {
    return document_.next_annotation_id;
}

std::vector<PointPx>
AnnotationController::Smooth_points(std::span<const PointPx> points) const {
    return smoother_.Smooth(points);
}

void AnnotationController::Commit_new_annotation(UndoStack &undo_stack,
                                                 Annotation annotation) {
    size_t const insert_index = document_.annotations.size();
    undo_stack.Push(std::make_unique<AddAnnotationCommand>(
        this, insert_index, std::move(annotation), document_.selected_annotation_id,
        document_.selected_annotation_id));
}

bool AnnotationController::Begin_annotation_edit(AnnotationEditTarget target,
                                                 PointPx cursor) {
    if (active_tool_.has_value() || Has_active_tool_gesture() ||
        Has_active_edit_interaction()) {
        return false;
    }
    std::optional<size_t> const index =
        Index_of_annotation_id(document_.annotations, target.annotation_id);
    if (!index.has_value()) {
        return false;
    }
    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(target, *index,
                                           document_.annotations[*index], cursor);
    if (interaction == nullptr) {
        return false;
    }

    document_.selected_annotation_id = target.annotation_id;
    active_edit_interaction_ = std::move(interaction);
    return true;
}

void AnnotationController::Update_annotation_at(
    size_t index, Annotation annotation,
    std::optional<uint64_t> selected_annotation_id) {
    if (active_tool_.has_value()) {
        selected_annotation_id = std::nullopt;
    }
    if (index >= document_.annotations.size()) {
        document_.selected_annotation_id = selected_annotation_id;
        return;
    }
    document_.annotations[index] = std::move(annotation);
    document_.selected_annotation_id = selected_annotation_id;
    document_.next_annotation_id =
        std::max(document_.next_annotation_id, document_.annotations[index].id + 1);
}

void AnnotationController::Insert_annotation_at(
    size_t index, Annotation annotation,
    std::optional<uint64_t> selected_annotation_id) {
    if (active_tool_.has_value()) {
        selected_annotation_id = std::nullopt;
    }
    if (index > document_.annotations.size()) {
        index = document_.annotations.size();
    }
    document_.annotations.insert(document_.annotations.begin() +
                                     static_cast<std::ptrdiff_t>(index),
                                 std::move(annotation));
    document_.selected_annotation_id = selected_annotation_id;
    for (Annotation const &entry : document_.annotations) {
        document_.next_annotation_id =
            std::max(document_.next_annotation_id, entry.id + 1);
    }
}

void AnnotationController::Erase_annotation_at(
    size_t index, std::optional<uint64_t> selected_annotation_id) {
    if (active_tool_.has_value()) {
        selected_annotation_id = std::nullopt;
    }
    if (index >= document_.annotations.size()) {
        document_.selected_annotation_id = selected_annotation_id;
        return;
    }
    document_.annotations.erase(document_.annotations.begin() +
                                static_cast<std::ptrdiff_t>(index));
    document_.selected_annotation_id = selected_annotation_id;
}

Annotation const *AnnotationController::Annotation_at(size_t index) const noexcept {
    if (index >= document_.annotations.size()) {
        return nullptr;
    }
    return &document_.annotations[index];
}

IAnnotationTool *AnnotationController::Active_tool_impl() noexcept {
    if (!active_tool_.has_value()) {
        return nullptr;
    }
    return registry_.Find_by_id(*active_tool_);
}

IAnnotationTool const *AnnotationController::Active_tool_impl() const noexcept {
    if (!active_tool_.has_value()) {
        return nullptr;
    }
    return registry_.Find_by_id(*active_tool_);
}

} // namespace greenflame::core
