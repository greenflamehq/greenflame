#include "greenflame_core/annotation_controller.h"

#include "greenflame_core/annotation_commands.h"
#include "greenflame_core/freehand_annotation_tool.h"
#include "greenflame_core/selection_wheel.h"
#include "greenflame_core/undo_stack.h"

namespace greenflame::core {

namespace {

// Highlighter: width_px = size step + this offset (default step 10 → 20 px).
constexpr int32_t kHighlighterStrokeWidthStepOffsetPx = 10;
constexpr int32_t kDefaultHighlighterSizeStep = 10;

// Bubble: diameter_px = size step + this offset for new bubbles and toolbar physical
// size.
constexpr int32_t kBubbleDiameterStepOffsetPx = 20;

[[nodiscard]] int32_t Clamp_opacity_percent(int32_t opacity_percent) noexcept {
    return std::clamp(opacity_percent, StrokeStyle::kMinOpacityPercent,
                      StrokeStyle::kMaxOpacityPercent);
}

[[nodiscard]] StrokeStyle Default_highlighter_style() noexcept {
    return StrokeStyle{
        .width_px = kDefaultHighlighterSizeStep + kHighlighterStrokeWidthStepOffsetPx,
        .color = kDefaultHighlighterColorPalette[static_cast<size_t>(
            kDefaultHighlighterColorIndex)],
        .opacity_percent = kDefaultHighlighterOpacityPercent,
    };
}

[[nodiscard]] bool Text_annotation_has_text(TextAnnotation const &annotation) noexcept {
    for (TextRun const &run : annotation.runs) {
        if (!run.text.empty()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool Bounds_intersect(std::optional<RectPx> a,
                                    std::optional<RectPx> b) noexcept {
    return a.has_value() && b.has_value() && RectPx::Intersect(*a, *b).has_value();
}

[[nodiscard]] bool Is_obfuscate_annotation(Annotation const &annotation) noexcept {
    return std::holds_alternative<ObfuscateAnnotation>(annotation.data);
}

} // namespace

AnnotationController::AnnotationController()
    : highlighter_style_(Default_highlighter_style()) {}

void AnnotationController::Reset_for_session() {
    document_ = {};
    active_tool_.reset();
    freehand_style_ = {};
    freehand_smoothing_mode_ = FreehandSmoothingMode::Smooth;
    line_style_ = {};
    arrow_style_ = {};
    rect_style_ = {};
    ellipse_style_ = {};
    highlighter_style_ = Default_highlighter_style();
    highlighter_smoothing_mode_ = FreehandSmoothingMode::Smooth;
    bubble_size_step_ = 10;
    text_size_step_ = 10;
    obfuscate_block_size_ = kObfuscateDefaultBlockSize;
    active_edit_interaction_.reset();
    text_layout_engine_ = nullptr;
    obfuscate_source_provider_ = nullptr;
    text_edit_ctrl_.reset();
    editing_annotation_id_.reset();
    text_current_font_ = TextFontChoice::Sans;
    bubble_counter_ = 1;
    bubble_current_font_ = TextFontChoice::Sans;
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
        document_.selected_annotation_ids.clear();
    }
    return true;
}

bool AnnotationController::Toggle_tool_by_hotkey(wchar_t hotkey, bool shift) {
    IAnnotationTool *const tool = registry_.Find_by_hotkey(hotkey, shift);
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
AnnotationController::Tool_id_from_hotkey(wchar_t hotkey, bool shift) const {
    IAnnotationTool const *const tool = registry_.Find_by_hotkey(hotkey, shift);
    if (tool == nullptr) {
        return std::nullopt;
    }
    return tool->Descriptor().id;
}

int32_t AnnotationController::Tool_size_step(AnnotationToolId tool) const noexcept {
    switch (tool) {
    case AnnotationToolId::Freehand:
        return freehand_style_.width_px;
    case AnnotationToolId::Line:
        return line_style_.width_px;
    case AnnotationToolId::Arrow:
        return arrow_style_.width_px;
    case AnnotationToolId::Rectangle:
        return rect_style_.width_px;
    case AnnotationToolId::Ellipse:
        return ellipse_style_.width_px;
    case AnnotationToolId::Highlighter:
        return highlighter_style_.width_px - kHighlighterStrokeWidthStepOffsetPx;
    case AnnotationToolId::Bubble:
        return bubble_size_step_;
    case AnnotationToolId::Obfuscate:
        return obfuscate_block_size_;
    case AnnotationToolId::Text:
        return text_size_step_;
    case AnnotationToolId::FilledRectangle:
    case AnnotationToolId::FilledEllipse:
        return 0;
    }
    return 0;
}

int32_t AnnotationController::Tool_physical_size(AnnotationToolId tool) const noexcept {
    switch (tool) {
    case AnnotationToolId::Freehand:
        return freehand_style_.width_px;
    case AnnotationToolId::Line:
        return line_style_.width_px;
    case AnnotationToolId::Arrow:
        return arrow_style_.width_px;
    case AnnotationToolId::Rectangle:
        return rect_style_.width_px;
    case AnnotationToolId::Ellipse:
        return ellipse_style_.width_px;
    case AnnotationToolId::Highlighter:
        return highlighter_style_.width_px;
    case AnnotationToolId::Bubble:
        return bubble_size_step_ + kBubbleDiameterStepOffsetPx;
    case AnnotationToolId::Obfuscate:
        return obfuscate_block_size_;
    case AnnotationToolId::Text:
        return kTextSizePtTable[static_cast<size_t>(text_size_step_ - 1)];
    case AnnotationToolId::FilledRectangle:
    case AnnotationToolId::FilledEllipse:
        return 0;
    }
    return 0;
}

bool AnnotationController::Set_tool_size_step(AnnotationToolId tool,
                                              int32_t step) noexcept {
    int32_t const clamped = Clamp_tool_size_step(step);
    switch (tool) {
    case AnnotationToolId::Freehand:
        if (freehand_style_.width_px == clamped) {
            return false;
        }
        freehand_style_.width_px = clamped;
        break;
    case AnnotationToolId::Line:
        if (line_style_.width_px == clamped) {
            return false;
        }
        line_style_.width_px = clamped;
        break;
    case AnnotationToolId::Arrow:
        if (arrow_style_.width_px == clamped) {
            return false;
        }
        arrow_style_.width_px = clamped;
        break;
    case AnnotationToolId::Rectangle:
        if (rect_style_.width_px == clamped) {
            return false;
        }
        rect_style_.width_px = clamped;
        break;
    case AnnotationToolId::Ellipse:
        if (ellipse_style_.width_px == clamped) {
            return false;
        }
        ellipse_style_.width_px = clamped;
        break;
    case AnnotationToolId::Highlighter: {
        int32_t const new_width = clamped + kHighlighterStrokeWidthStepOffsetPx;
        if (highlighter_style_.width_px == new_width) {
            return false;
        }
        highlighter_style_.width_px = new_width;
        break;
    }
    case AnnotationToolId::Bubble:
        if (bubble_size_step_ == clamped) {
            return false;
        }
        bubble_size_step_ = clamped;
        break;
    case AnnotationToolId::Obfuscate:
        if (obfuscate_block_size_ == clamped) {
            return false;
        }
        obfuscate_block_size_ = clamped;
        break;
    case AnnotationToolId::Text:
        if (text_size_step_ == clamped) {
            return false;
        }
        text_size_step_ = clamped;
        break;
    case AnnotationToolId::FilledRectangle:
    case AnnotationToolId::FilledEllipse:
        return false;
    }
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
    if (freehand_style_.color == color) {
        return false;
    }
    freehand_style_.color = color;
    line_style_.color = color;
    arrow_style_.color = color;
    rect_style_.color = color;
    ellipse_style_.color = color;
    registry_.On_stroke_style_changed();
    return true;
}

bool AnnotationController::Set_brush_smoothing_mode(
    FreehandSmoothingMode mode) noexcept {
    if (freehand_smoothing_mode_ == mode) {
        return false;
    }
    freehand_smoothing_mode_ = mode;
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

bool AnnotationController::Set_highlighter_smoothing_mode(
    FreehandSmoothingMode mode) noexcept {
    if (highlighter_smoothing_mode_ == mode) {
        return false;
    }
    highlighter_smoothing_mode_ = mode;
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

FreehandSmoothingMode
AnnotationController::Draft_freehand_smoothing_mode() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    if (tool == nullptr || tool->Draft_freehand_points().empty()) {
        return FreehandSmoothingMode::Off;
    }
    return Current_freehand_smoothing_mode();
}

std::optional<size_t> AnnotationController::Selected_annotation_index() const noexcept {
    if (document_.selected_annotation_ids.size() != 1) {
        return std::nullopt;
    }
    return Index_of_annotation_id(document_.annotations,
                                  document_.selected_annotation_ids.front());
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
    return Annotation_selection_bounds(document_.annotations,
                                       document_.selected_annotation_ids);
}

std::optional<AnnotationEditTarget>
AnnotationController::Annotation_edit_target_at(PointPx cursor) const noexcept {
    return Hit_test_annotation_edit_target(document_.selected_annotation_ids,
                                           document_.annotations,
                                           Selected_annotation_bounds(), cursor);
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

std::optional<AnnotationEditPreview>
AnnotationController::Active_annotation_edit_preview() const noexcept {
    if (active_edit_interaction_ == nullptr) {
        return std::nullopt;
    }
    std::vector<AnnotationEditPreview> const previews =
        active_edit_interaction_->Previews();
    return previews.size() == 1 ? std::optional<AnnotationEditPreview>{previews.front()}
                                : std::nullopt;
}

std::vector<AnnotationEditPreview>
AnnotationController::Active_annotation_edit_previews() const {
    return active_edit_interaction_ == nullptr ? std::vector<AnnotationEditPreview>{}
                                               : active_edit_interaction_->Previews();
}

std::vector<size_t> AnnotationController::Active_obfuscate_preview_indices() const {
    std::vector<size_t> indices = {};
    std::vector<AnnotationEditPreview> const previews =
        Active_annotation_edit_previews();
    if (previews.empty()) {
        return indices;
    }

    for (size_t index = 0; index < document_.annotations.size(); ++index) {
        Annotation const &annotation = document_.annotations[index];
        if (!Is_obfuscate_annotation(annotation)) {
            continue;
        }

        bool include_index = false;
        for (AnnotationEditPreview const &preview : previews) {
            if (preview.index >= document_.annotations.size()) {
                continue;
            }
            std::optional<RectPx> const old_bounds =
                Annotation_bounds(preview.annotation_before);
            std::optional<RectPx> const new_bounds =
                Annotation_bounds(preview.annotation_after);
            if (index == preview.index ||
                Bounds_intersect(Annotation_bounds(annotation), old_bounds) ||
                Bounds_intersect(Annotation_bounds(annotation), new_bounds)) {
                include_index = true;
                break;
            }
        }

        if (include_index) {
            indices.push_back(index);
        }
    }
    return indices;
}

bool AnnotationController::Has_active_gesture() const noexcept {
    return Has_active_tool_gesture() || Has_active_edit_interaction() ||
           Has_active_text_edit();
}

void AnnotationController::Set_text_layout_engine(ITextLayoutEngine *engine) noexcept {
    text_layout_engine_ = engine;
}

void AnnotationController::Set_obfuscate_source_provider(
    IObfuscateSourceProvider *provider) noexcept {
    obfuscate_source_provider_ = provider;
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

    document_.selected_annotation_ids.clear();
    TextAnnotationBaseStyle const base_style{
        .color = freehand_style_.color,
        .font_choice = Normalize_text_font_choice(text_current_font_),
        .point_size = Text_point_size_from_step(text_size_step_),
    };
    // Place text so the baseline lands at the click point, adjusted by the same
    // visual offset applied to the cursor preview in Draw_text_cursor_preview
    // (x_offset / y_offset constants there must match kTextPreviewXOffsetPx /
    // kTextPreviewYOffsetPx here).
    constexpr int32_t x_offset_px = 11;
    constexpr int32_t y_offset_px = 10;
    int32_t const ascent =
        text_layout_engine_ ? text_layout_engine_->Line_ascent(base_style) : 0;
    PointPx const adjusted_origin{origin.x + x_offset_px,
                                  origin.y - ascent + y_offset_px};
    text_edit_ctrl_.emplace(adjusted_origin, base_style, text_layout_engine_);
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

void AnnotationController::Cancel_text_draft() {
    text_edit_ctrl_.reset();
    editing_annotation_id_.reset();
}

std::optional<uint64_t> AnnotationController::Editing_annotation_id() const noexcept {
    return editing_annotation_id_;
}

bool AnnotationController::Begin_text_edit_on_annotation(uint64_t annotation_id,
                                                         PointPx cursor) {
    if (Has_active_tool_gesture() || Has_active_edit_interaction() ||
        text_edit_ctrl_.has_value() || text_layout_engine_ == nullptr) {
        return false;
    }

    std::optional<size_t> const index =
        Index_of_annotation_id(document_.annotations, annotation_id);
    if (!index.has_value()) {
        return false;
    }
    TextAnnotation const *const text_ann =
        std::get_if<TextAnnotation>(&document_.annotations[*index].data);
    if (text_ann == nullptr) {
        return false;
    }

    editing_annotation_id_ = annotation_id;
    text_edit_ctrl_.emplace(text_ann->origin, text_ann->base_style, text_ann->runs,
                            text_layout_engine_);
    text_edit_ctrl_->On_pointer_press(cursor);
    return true;
}

void AnnotationController::Commit_text_edit_annotation(UndoStack &undo_stack,
                                                       TextAnnotation annotation) {
    if (!editing_annotation_id_.has_value()) {
        return;
    }
    uint64_t const editing_id = *editing_annotation_id_;
    editing_annotation_id_.reset();
    text_edit_ctrl_.reset();

    if (!Text_annotation_has_text(annotation) || text_layout_engine_ == nullptr) {
        return;
    }

    text_layout_engine_->Rasterize(annotation);

    std::optional<size_t> const index =
        Index_of_annotation_id(document_.annotations, editing_id);
    if (!index.has_value()) {
        return;
    }

    Annotation annotation_before = document_.annotations[*index];
    Annotation annotation_after = document_.annotations[*index];
    annotation_after.data = std::move(annotation);

    AnnotationSelection const selection = {editing_id};
    auto cmd = std::make_unique<UpdateAnnotationCommand>(
        this, *index, std::move(annotation_before), std::move(annotation_after),
        selection, selection, "Edit text annotation");
    Push_annotation_command(undo_stack, std::move(cmd), {});
}

int32_t AnnotationController::Text_point_size() const noexcept {
    return Text_point_size_from_step(text_size_step_);
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
    TextFontChoice const normalized = Normalize_text_font_choice(choice);
    if (bubble_current_font_ == normalized) {
        return;
    }
    bubble_current_font_ = normalized;
    registry_.On_stroke_style_changed();
}

bool AnnotationController::Straighten_highlighter_stroke() noexcept {
    if (active_tool_ != AnnotationToolId::Highlighter) {
        return false;
    }
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr || !tool->Has_active_gesture()) {
        return false;
    }
    static_cast<FreehandAnnotationTool *>(tool)->Straighten();
    return true;
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
        std::vector<Annotation> const annotations_before = document_.annotations;
        std::vector<AnnotationEditCommandData> commands =
            active_edit_interaction_->Commit_all();
        active_edit_interaction_.reset();
        if (commands.empty()) {
            return false;
        }

        std::vector<std::unique_ptr<ICommand>> primary_commands = {};
        primary_commands.reserve(commands.size());
        for (AnnotationEditCommandData &command : commands) {
            Annotation annotation_after = command.annotation_after;
            if (std::holds_alternative<ObfuscateAnnotation>(annotation_after.data)) {
                std::optional<Annotation> const rebuilt = Rebuild_obfuscate_annotation(
                    annotations_before, command.index, annotation_after);
                if (!rebuilt.has_value()) {
                    return false;
                }
                annotation_after = *rebuilt;
                if (command.index < document_.annotations.size()) {
                    document_.annotations[command.index] = annotation_after;
                }
                command.annotation_after = annotation_after;
            }
            primary_commands.push_back(std::make_unique<UpdateAnnotationCommand>(
                this, command.index, command.annotation_before,
                command.annotation_after, command.selection_before,
                command.selection_after, command.description));
        }

        std::vector<std::unique_ptr<ICommand>> reactive_commands =
            Build_reactive_obfuscate_update_commands(
                annotations_before, document_.annotations,
                document_.selected_annotation_ids, document_.selected_annotation_ids);
        Push_annotation_commands(
            undo_stack, std::move(primary_commands), std::move(reactive_commands),
            commands.size() > 1 ? "Move annotations" : "Compound annotation change");
        return true;
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
        editing_annotation_id_.reset();
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
    if (Has_active_edit_interaction() || document_.selected_annotation_ids.empty()) {
        return false;
    }
    AnnotationSelection const selection_before = document_.selected_annotation_ids;
    std::vector<Annotation> const annotations_before = document_.annotations;
    std::vector<Annotation> annotations_after = document_.annotations;
    std::vector<size_t> selected_indices = {};
    selected_indices.reserve(selection_before.size());
    for (size_t index = 0; index < annotations_after.size(); ++index) {
        if (Selection_contains_annotation_id(selection_before,
                                             annotations_after[index].id)) {
            selected_indices.push_back(index);
        }
    }
    if (selected_indices.empty()) {
        document_.selected_annotation_ids.clear();
        return true;
    }

    std::vector<std::unique_ptr<ICommand>> primary_commands = {};
    primary_commands.reserve(selected_indices.size());
    for (size_t i = selected_indices.size(); i > 0; --i) {
        size_t const index = selected_indices[i - 1];
        Annotation const deleted_annotation = annotations_after[index];
        annotations_after.erase(annotations_after.begin() +
                                static_cast<std::ptrdiff_t>(index));
        primary_commands.push_back(std::make_unique<DeleteAnnotationCommand>(
            this, index, deleted_annotation, selection_before, AnnotationSelection{}));
    }

    std::vector<std::unique_ptr<ICommand>> reactive_commands =
        Build_reactive_obfuscate_update_commands(annotations_before, annotations_after,
                                                 selection_before,
                                                 AnnotationSelection{});
    Push_annotation_commands(undo_stack, std::move(primary_commands),
                             std::move(reactive_commands), "Delete annotations");
    return true;
}

void AnnotationController::Clear_annotations() noexcept {
    document_.annotations.clear();
    document_.selected_annotation_ids.clear();
    active_edit_interaction_.reset();
    text_edit_ctrl_.reset();
    editing_annotation_id_.reset();
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
    if (!selected_annotation_id.has_value()) {
        return Set_selected_annotations({});
    }
    std::array<uint64_t, 1> const selection = {*selected_annotation_id};
    return Set_selected_annotations(selection);
}

bool AnnotationController::Set_selected_annotations(
    std::span<const uint64_t> selected_annotation_ids) noexcept {
    AnnotationSelection selection = active_tool_.has_value()
                                        ? AnnotationSelection{}
                                        : Normalized_selection(selected_annotation_ids);
    if (document_.selected_annotation_ids == selection) {
        return false;
    }
    document_.selected_annotation_ids = std::move(selection);
    return true;
}

bool AnnotationController::Toggle_selected_annotation(uint64_t annotation_id) noexcept {
    AnnotationSelection selection = document_.selected_annotation_ids;
    auto const it = std::ranges::find(selection, annotation_id);
    if (it != selection.end()) {
        selection.erase(it);
    } else {
        selection.push_back(annotation_id);
    }
    return Set_selected_annotations(selection);
}

bool AnnotationController::Add_selected_annotations(
    std::span<const uint64_t> selected_annotation_ids) noexcept {
    if (selected_annotation_ids.empty()) {
        return false;
    }
    AnnotationSelection selection = document_.selected_annotation_ids;
    selection.insert(selection.end(), selected_annotation_ids.begin(),
                     selected_annotation_ids.end());
    return Set_selected_annotations(selection);
}

bool AnnotationController::Select_topmost_annotation(PointPx cursor) {
    return Set_selected_annotation(Annotation_id_at(cursor));
}

AnnotationSelection AnnotationController::Annotation_ids_intersecting_selection_rect(
    RectPx selection_rect) const noexcept {
    return greenflame::core::Annotation_ids_intersecting_selection_rect(
        document_.annotations, selection_rect);
}

std::span<const PointPx> AnnotationController::Draft_freehand_points() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return {};
    }
    return tool->Draft_freehand_points();
}

StrokeStyle AnnotationController::Current_stroke_style() const noexcept {
    switch (active_tool_.value_or(AnnotationToolId::Freehand)) {
    case AnnotationToolId::Highlighter:
        return highlighter_style_;
    case AnnotationToolId::Line:
        return line_style_;
    case AnnotationToolId::Arrow:
        return arrow_style_;
    case AnnotationToolId::Rectangle:
    case AnnotationToolId::FilledRectangle:
        return rect_style_;
    case AnnotationToolId::Ellipse:
    case AnnotationToolId::FilledEllipse:
        return ellipse_style_;
    case AnnotationToolId::Freehand:
    case AnnotationToolId::Obfuscate:
    case AnnotationToolId::Bubble:
    case AnnotationToolId::Text:
        return freehand_style_;
    }
    return freehand_style_;
}

FreehandSmoothingMode
AnnotationController::Current_freehand_smoothing_mode() const noexcept {
    switch (active_tool_.value_or(AnnotationToolId::Freehand)) {
    case AnnotationToolId::Highlighter:
        return highlighter_smoothing_mode_;
    case AnnotationToolId::Freehand:
        return freehand_smoothing_mode_;
    case AnnotationToolId::Line:
    case AnnotationToolId::Arrow:
    case AnnotationToolId::Rectangle:
    case AnnotationToolId::FilledRectangle:
    case AnnotationToolId::Ellipse:
    case AnnotationToolId::FilledEllipse:
    case AnnotationToolId::Obfuscate:
    case AnnotationToolId::Bubble:
    case AnnotationToolId::Text:
        return FreehandSmoothingMode::Off;
    }
    return FreehandSmoothingMode::Off;
}

uint64_t AnnotationController::Next_annotation_id() const noexcept {
    return document_.next_annotation_id;
}

std::vector<PointPx>
AnnotationController::Smooth_points(std::span<const PointPx> points) const {
    FreehandSmoothingMode const mode = Current_freehand_smoothing_mode();
    if (mode == FreehandSmoothingMode::Off) {
        return {points.begin(), points.end()};
    }
    return Smooth_freehand_points(points, mode, Current_stroke_style().width_px);
}

int32_t AnnotationController::Current_obfuscate_block_size() const noexcept {
    return obfuscate_block_size_;
}

std::optional<Annotation>
AnnotationController::Build_bubble_annotation(PointPx cursor) const {
    if (text_layout_engine_ == nullptr) {
        return std::nullopt;
    }

    BubbleAnnotation bubble{};
    bubble.center = cursor;
    bubble.diameter_px = bubble_size_step_ + kBubbleDiameterStepOffsetPx;
    bubble.color = freehand_style_.color;
    bubble.font_choice = Normalize_text_font_choice(bubble_current_font_);
    bubble.counter_value = bubble_counter_;
    text_layout_engine_->Rasterize_bubble(bubble);

    Annotation annotation{};
    annotation.id = Next_annotation_id();
    annotation.data = std::move(bubble);
    return annotation;
}

std::optional<Annotation>
AnnotationController::Build_obfuscate_annotation(RectPx bounds) const {
    Annotation annotation{};
    annotation.id = Next_annotation_id();
    annotation.data = ObfuscateAnnotation{
        .bounds = bounds.Normalized(),
        .block_size = Current_obfuscate_block_size(),
    };
    return Rebuild_obfuscate_annotation(
        document_.annotations, document_.annotations.size(), std::move(annotation));
}

void AnnotationController::Commit_new_annotation(UndoStack &undo_stack,
                                                 Annotation annotation) {
    size_t const insert_index = document_.annotations.size();
    AnnotationSelection const selection = document_.selected_annotation_ids;
    if (annotation.Kind() == AnnotationKind::Bubble) {
        Push_annotation_command(
            undo_stack,
            std::make_unique<AddBubbleAnnotationCommand>(
                this, insert_index, std::move(annotation), selection, selection),
            {});
        return;
    }
    Push_annotation_command(
        undo_stack,
        std::make_unique<AddAnnotationCommand>(
            this, insert_index, std::move(annotation), selection, selection),
        {});
}

bool AnnotationController::Begin_annotation_edit(AnnotationEditTarget target,
                                                 PointPx cursor) {
    if (active_tool_.has_value() || Has_active_tool_gesture() ||
        Has_active_edit_interaction()) {
        return false;
    }
    std::unique_ptr<IAnnotationEditInteraction> interaction = {};
    if (target.kind == AnnotationEditTargetKind::SelectionBody) {
        interaction = Create_selection_move_edit_interaction(
            document_.annotations, document_.selected_annotation_ids, cursor);
    } else {
        std::optional<size_t> const index =
            Index_of_annotation_id(document_.annotations, target.annotation_id);
        if (!index.has_value()) {
            return false;
        }
        interaction = Create_annotation_edit_interaction(
            target, *index, document_.annotations[*index], cursor);
    }
    if (interaction == nullptr) {
        return false;
    }

    if (target.kind != AnnotationEditTargetKind::SelectionBody) {
        AnnotationSelection const selection = {target.annotation_id};
        document_.selected_annotation_ids = Normalized_selection(selection);
    }
    active_edit_interaction_ = std::move(interaction);
    return true;
}

AnnotationSelection AnnotationController::Normalized_selection(
    std::span<const uint64_t> selected_annotation_ids) const noexcept {
    return Normalize_annotation_selection(document_.annotations,
                                          selected_annotation_ids);
}

std::optional<Annotation> AnnotationController::Rebuild_obfuscate_annotation(
    std::span<const Annotation> annotations, size_t index,
    Annotation annotation) const {
    ObfuscateAnnotation *const obfuscate =
        std::get_if<ObfuscateAnnotation>(&annotation.data);
    if (obfuscate == nullptr || obfuscate_source_provider_ == nullptr) {
        return std::nullopt;
    }

    RectPx const normalized_bounds = obfuscate->bounds.Normalized();
    if (normalized_bounds.Is_empty()) {
        return std::nullopt;
    }

    std::optional<BgraBitmap> const source =
        obfuscate_source_provider_->Build_composited_source(normalized_bounds,
                                                            annotations.first(index));
    if (!source.has_value() || !source->Is_valid()) {
        return std::nullopt;
    }

    BgraBitmap const raster = Rasterize_obfuscate(*source, obfuscate->block_size);
    if (!raster.Is_valid()) {
        return std::nullopt;
    }

    obfuscate->bounds = normalized_bounds;
    obfuscate->block_size = Clamp_obfuscate_block_size(obfuscate->block_size);
    obfuscate->bitmap_width_px = raster.width_px;
    obfuscate->bitmap_height_px = raster.height_px;
    obfuscate->bitmap_row_bytes = raster.row_bytes;
    obfuscate->premultiplied_bgra = raster.premultiplied_bgra;
    return annotation;
}

std::vector<std::unique_ptr<ICommand>>
AnnotationController::Build_reactive_obfuscate_update_commands(
    std::vector<Annotation> const &before_annotations,
    std::vector<Annotation> after_annotations,
    AnnotationSelection const &selection_before,
    AnnotationSelection const &selection_after) {
    std::vector<std::unique_ptr<ICommand>> commands = {};
    for (size_t index = 0; index < after_annotations.size(); ++index) {
        if (!Is_obfuscate_annotation(after_annotations[index])) {
            continue;
        }

        std::optional<Annotation> const rebuilt = Rebuild_obfuscate_annotation(
            after_annotations, index, after_annotations[index]);
        if (!rebuilt.has_value()) {
            continue;
        }
        std::optional<size_t> const before_index =
            Index_of_annotation_id(before_annotations, after_annotations[index].id);
        if (!before_index.has_value()) {
            after_annotations[index] = *rebuilt;
            continue;
        }
        if (before_annotations[*before_index] == *rebuilt) {
            after_annotations[index] = *rebuilt;
            continue;
        }

        commands.push_back(std::make_unique<UpdateAnnotationCommand>(
            this, index, before_annotations[*before_index], *rebuilt, selection_before,
            selection_after, "Recompute obfuscate annotation"));
        after_annotations[index] = *rebuilt;
    }

    return commands;
}

void AnnotationController::Push_annotation_command(
    UndoStack &undo_stack, std::unique_ptr<ICommand> primary_command,
    std::vector<std::unique_ptr<ICommand>> reactive_commands) const {
    if (primary_command == nullptr) {
        return;
    }

    std::vector<std::unique_ptr<ICommand>> primary_commands = {};
    primary_commands.push_back(std::move(primary_command));
    Push_annotation_commands(undo_stack, std::move(primary_commands),
                             std::move(reactive_commands));
}

void AnnotationController::Push_annotation_commands(
    UndoStack &undo_stack, std::vector<std::unique_ptr<ICommand>> primary_commands,
    std::vector<std::unique_ptr<ICommand>> reactive_commands,
    std::string_view description) const {
    if (primary_commands.empty()) {
        return;
    }

    if (primary_commands.size() == 1 && reactive_commands.empty()) {
        undo_stack.Push(std::move(primary_commands.front()));
        return;
    }

    std::vector<std::unique_ptr<ICommand>> commands = {};
    commands.reserve(primary_commands.size() + reactive_commands.size());
    for (auto &command : primary_commands) {
        commands.push_back(std::move(command));
    }
    if (reactive_commands.empty()) {
        undo_stack.Push(
            std::make_unique<CompoundCommand>(std::move(commands), description));
        return;
    }
    for (auto &command : reactive_commands) {
        commands.push_back(std::move(command));
    }
    undo_stack.Push(
        std::make_unique<CompoundCommand>(std::move(commands), description));
}

void AnnotationController::Update_annotation_at(
    size_t index, Annotation annotation,
    std::span<const uint64_t> selected_annotation_ids) {
    AnnotationSelection selection = active_tool_.has_value()
                                        ? AnnotationSelection{}
                                        : Normalized_selection(selected_annotation_ids);
    if (index >= document_.annotations.size()) {
        document_.selected_annotation_ids = std::move(selection);
        return;
    }
    document_.annotations[index] = std::move(annotation);
    document_.selected_annotation_ids = std::move(selection);
    document_.next_annotation_id =
        std::max(document_.next_annotation_id, document_.annotations[index].id + 1);
}

void AnnotationController::Update_annotation_at(
    size_t index, Annotation annotation,
    std::optional<uint64_t> selected_annotation_id) {
    if (!selected_annotation_id.has_value()) {
        Update_annotation_at(index, std::move(annotation), std::span<const uint64_t>{});
        return;
    }
    std::array<uint64_t, 1> const selection = {*selected_annotation_id};
    Update_annotation_at(index, std::move(annotation), selection);
}

void AnnotationController::Insert_annotation_at(
    size_t index, Annotation annotation,
    std::span<const uint64_t> selected_annotation_ids) {
    AnnotationSelection selection =
        active_tool_.has_value() ? AnnotationSelection{}
                                 : AnnotationSelection(selected_annotation_ids.begin(),
                                                       selected_annotation_ids.end());
    if (index > document_.annotations.size()) {
        index = document_.annotations.size();
    }
    document_.annotations.insert(document_.annotations.begin() +
                                     static_cast<std::ptrdiff_t>(index),
                                 std::move(annotation));
    document_.selected_annotation_ids = Normalized_selection(selection);
    for (Annotation const &entry : document_.annotations) {
        document_.next_annotation_id =
            std::max(document_.next_annotation_id, entry.id + 1);
    }
}

void AnnotationController::Insert_annotation_at(
    size_t index, Annotation annotation,
    std::optional<uint64_t> selected_annotation_id) {
    if (!selected_annotation_id.has_value()) {
        Insert_annotation_at(index, std::move(annotation), std::span<const uint64_t>{});
        return;
    }
    std::array<uint64_t, 1> const selection = {*selected_annotation_id};
    Insert_annotation_at(index, std::move(annotation), selection);
}

void AnnotationController::Erase_annotation_at(
    size_t index, std::span<const uint64_t> selected_annotation_ids) {
    AnnotationSelection selection =
        active_tool_.has_value() ? AnnotationSelection{}
                                 : AnnotationSelection(selected_annotation_ids.begin(),
                                                       selected_annotation_ids.end());
    if (index >= document_.annotations.size()) {
        document_.selected_annotation_ids = Normalized_selection(selection);
        return;
    }
    document_.annotations.erase(document_.annotations.begin() +
                                static_cast<std::ptrdiff_t>(index));
    document_.selected_annotation_ids = Normalized_selection(selection);
}

void AnnotationController::Erase_annotation_at(
    size_t index, std::optional<uint64_t> selected_annotation_id) {
    if (!selected_annotation_id.has_value()) {
        Erase_annotation_at(index, std::span<const uint64_t>{});
        return;
    }
    std::array<uint64_t, 1> const selection = {*selected_annotation_id};
    Erase_annotation_at(index, selection);
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
