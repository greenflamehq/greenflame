#include "greenflame_core/annotation_controller.h"

#include "greenflame_core/annotation_commands.h"
#include "greenflame_core/undo_stack.h"

namespace greenflame::core {

namespace {

[[nodiscard]] int32_t Clamp_brush_width_px(int32_t width_px) noexcept {
    return std::clamp(width_px, StrokeStyle::kMinWidthPx, StrokeStyle::kMaxWidthPx);
}

} // namespace

std::vector<PointPx>
PassthroughStrokeSmoother::Smooth(std::span<const PointPx> points) const {
    return {points.begin(), points.end()};
}

AnnotationController::AnnotationController() = default;

void AnnotationController::Reset_for_session() {
    document_ = {};
    active_tool_.reset();
    brush_style_ = {};
    annotation_dragging_ = false;
    line_endpoint_dragging_ = false;
    annotation_drag_start_ = {};
    annotation_drag_before_ = {};
    annotation_edit_before_ = {};
    active_line_endpoint_drag_.reset();
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
    if (brush_style_.width_px == clamped_width) {
        return false;
    }
    brush_style_.width_px = clamped_width;
    registry_.On_stroke_style_changed();
    return true;
}

bool AnnotationController::Set_annotation_color(COLORREF color) noexcept {
    if (brush_style_.color == color) {
        return false;
    }
    brush_style_.color = color;
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
    return Annotation_bounds(*selected);
}

std::optional<AnnotationLineEndpoint>
AnnotationController::Selected_line_handle_at(PointPx cursor) const noexcept {
    Annotation const *const selected = Selected_annotation();
    if (selected == nullptr || selected->kind != AnnotationKind::Line) {
        return std::nullopt;
    }
    return Hit_test_line_endpoint_handles(selected->line.start, selected->line.end,
                                          cursor);
}

bool AnnotationController::Has_active_tool_gesture() const noexcept {
    IAnnotationTool const *const tool = Active_tool_impl();
    return tool != nullptr && tool->Has_active_gesture();
}

bool AnnotationController::Has_active_gesture() const noexcept {
    return Has_active_tool_gesture() || annotation_dragging_ || line_endpoint_dragging_;
}

bool AnnotationController::On_primary_press(PointPx cursor) {
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_primary_press(*this, cursor);
}

bool AnnotationController::On_pointer_move(PointPx cursor) {
    if (line_endpoint_dragging_) {
        return Update_selected_line_endpoint_drag(cursor);
    }
    if (annotation_dragging_) {
        return Update_annotation_drag(cursor);
    }
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_pointer_move(*this, cursor);
}

bool AnnotationController::On_primary_release(UndoStack &undo_stack) {
    if (line_endpoint_dragging_) {
        return Commit_selected_line_endpoint_drag(undo_stack);
    }
    if (annotation_dragging_) {
        return Commit_annotation_drag(undo_stack);
    }
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_primary_release(*this, undo_stack);
}

bool AnnotationController::On_cancel() {
    if (Cancel_selected_line_endpoint_drag()) {
        return true;
    }
    if (Cancel_annotation_drag()) {
        return true;
    }
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_cancel(*this);
}

bool AnnotationController::Delete_selected_annotation(UndoStack &undo_stack) {
    if (annotation_dragging_ || line_endpoint_dragging_ ||
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
    annotation_dragging_ = false;
    line_endpoint_dragging_ = false;
    annotation_drag_start_ = {};
    annotation_drag_before_ = {};
    annotation_edit_before_ = {};
    active_line_endpoint_drag_.reset();
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

bool AnnotationController::Begin_annotation_drag(uint64_t id, PointPx cursor) {
    if (active_tool_.has_value() || Has_active_tool_gesture() || annotation_dragging_ ||
        line_endpoint_dragging_) {
        return false;
    }
    std::optional<size_t> const index =
        Index_of_annotation_id(document_.annotations, id);
    if (!index.has_value()) {
        return false;
    }
    document_.selected_annotation_id = id;
    annotation_dragging_ = true;
    annotation_drag_start_ = cursor;
    annotation_drag_before_ = document_.annotations[*index];
    return true;
}

bool AnnotationController::Update_annotation_drag(PointPx cursor) {
    if (!annotation_dragging_ || !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    if (!index.has_value()) {
        annotation_dragging_ = false;
        return false;
    }

    PointPx const delta{cursor.x - annotation_drag_start_.x,
                        cursor.y - annotation_drag_start_.y};
    Annotation const moved = Translate_annotation(annotation_drag_before_, delta);
    if (document_.annotations[*index] == moved) {
        return false;
    }

    Update_annotation_at(*index, moved, document_.selected_annotation_id);
    return true;
}

bool AnnotationController::Commit_annotation_drag(UndoStack &undo_stack) {
    if (!annotation_dragging_ || !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    annotation_dragging_ = false;
    if (!index.has_value()) {
        return false;
    }

    Annotation const after = document_.annotations[*index];
    if (after == annotation_drag_before_) {
        return false;
    }

    undo_stack.Push(std::make_unique<UpdateAnnotationCommand>(
        this, *index, annotation_drag_before_, after, document_.selected_annotation_id,
        document_.selected_annotation_id, "Move annotation"));
    return true;
}

bool AnnotationController::Cancel_annotation_drag() {
    if (!annotation_dragging_ || !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    annotation_dragging_ = false;
    if (!index.has_value()) {
        return false;
    }

    Update_annotation_at(*index, annotation_drag_before_,
                         document_.selected_annotation_id);
    return true;
}

bool AnnotationController::Begin_selected_line_endpoint_drag(
    AnnotationLineEndpoint endpoint) {
    if (active_tool_.has_value() || Has_active_tool_gesture() || annotation_dragging_ ||
        line_endpoint_dragging_) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    if (!index.has_value()) {
        return false;
    }
    Annotation const &selected = document_.annotations[*index];
    if (selected.kind != AnnotationKind::Line) {
        return false;
    }

    line_endpoint_dragging_ = true;
    active_line_endpoint_drag_ = endpoint;
    annotation_edit_before_ = selected;
    return true;
}

bool AnnotationController::Update_selected_line_endpoint_drag(PointPx cursor) {
    if (!line_endpoint_dragging_ || !active_line_endpoint_drag_.has_value() ||
        !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    if (!index.has_value()) {
        line_endpoint_dragging_ = false;
        active_line_endpoint_drag_.reset();
        return false;
    }

    Annotation edited = annotation_edit_before_;
    if (edited.kind != AnnotationKind::Line) {
        line_endpoint_dragging_ = false;
        active_line_endpoint_drag_.reset();
        return false;
    }

    if (*active_line_endpoint_drag_ == AnnotationLineEndpoint::Start) {
        edited.line.start = cursor;
    } else {
        edited.line.end = cursor;
    }
    edited.line.raster =
        Rasterize_line_segment(edited.line.start, edited.line.end, edited.line.style);
    if (document_.annotations[*index] == edited) {
        return false;
    }

    Update_annotation_at(*index, edited, document_.selected_annotation_id);
    return true;
}

bool AnnotationController::Commit_selected_line_endpoint_drag(UndoStack &undo_stack) {
    if (!line_endpoint_dragging_ || !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    line_endpoint_dragging_ = false;
    active_line_endpoint_drag_.reset();
    if (!index.has_value()) {
        return false;
    }

    Annotation const after = document_.annotations[*index];
    if (after == annotation_edit_before_) {
        return false;
    }

    undo_stack.Push(std::make_unique<UpdateAnnotationCommand>(
        this, *index, annotation_edit_before_, after, document_.selected_annotation_id,
        document_.selected_annotation_id, "Edit line annotation"));
    return true;
}

bool AnnotationController::Cancel_selected_line_endpoint_drag() {
    if (!line_endpoint_dragging_ || !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Selected_annotation_index();
    line_endpoint_dragging_ = false;
    active_line_endpoint_drag_.reset();
    if (!index.has_value()) {
        return false;
    }

    Update_annotation_at(*index, annotation_edit_before_,
                         document_.selected_annotation_id);
    return true;
}

void AnnotationController::Update_annotation_at(
    size_t index, Annotation annotation,
    std::optional<uint64_t> selected_annotation_id) {
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
    if (index >= document_.annotations.size()) {
        document_.selected_annotation_id = selected_annotation_id;
        return;
    }
    document_.annotations.erase(document_.annotations.begin() +
                                static_cast<std::ptrdiff_t>(index));
    document_.selected_annotation_id = selected_annotation_id;
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
