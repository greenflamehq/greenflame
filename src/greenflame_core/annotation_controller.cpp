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
    freehand_drawing_ = false;
    annotation_dragging_ = false;
    freehand_points_.clear();
    annotation_drag_start_ = {};
    annotation_drag_before_ = {};
    freehand_preview_.reset();
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
    freehand_preview_.reset();
    return true;
}

Annotation const *AnnotationController::Draft_annotation() const noexcept {
    if (!freehand_drawing_ || freehand_points_.empty()) {
        return nullptr;
    }
    if (!freehand_preview_.has_value()) {
        freehand_preview_ = Build_freehand_annotation(freehand_points_);
    }
    return &*freehand_preview_;
}

std::optional<StrokeStyle> AnnotationController::Draft_freehand_style() const noexcept {
    if (!freehand_drawing_ || freehand_points_.empty()) {
        return std::nullopt;
    }
    return brush_style_;
}

std::optional<RectPx>
AnnotationController::Selected_annotation_bounds() const noexcept {
    if (!document_.selected_annotation_id.has_value()) {
        return std::nullopt;
    }
    std::optional<size_t> const index = Index_of_annotation_id(
        document_.annotations, *document_.selected_annotation_id);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return Annotation_bounds(document_.annotations[*index]);
}

bool AnnotationController::Has_active_tool_gesture() const noexcept {
    return freehand_drawing_;
}

bool AnnotationController::Has_active_gesture() const noexcept {
    return freehand_drawing_ || annotation_dragging_;
}

bool AnnotationController::On_primary_press(PointPx cursor) {
    IAnnotationTool *const tool = Active_tool_impl();
    if (tool == nullptr) {
        return false;
    }
    return tool->On_primary_press(*this, cursor);
}

bool AnnotationController::On_pointer_move(PointPx cursor) {
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
    if (annotation_dragging_ || !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Index_of_annotation_id(
        document_.annotations, *document_.selected_annotation_id);
    if (!index.has_value()) {
        // The selection became stale relative to the document; clear it and let the
        // caller repaint even though there is no delete command to record.
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
    freehand_drawing_ = false;
    annotation_dragging_ = false;
    freehand_points_.clear();
    annotation_drag_start_ = {};
    annotation_drag_before_ = {};
    freehand_preview_.reset();
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

void AnnotationController::Begin_freehand_stroke(PointPx start) {
    freehand_drawing_ = true;
    freehand_points_.clear();
    freehand_points_.push_back(start);
    freehand_preview_.reset();
}

bool AnnotationController::Append_freehand_point(PointPx point) {
    if (!freehand_drawing_) {
        return false;
    }
    if (!freehand_points_.empty() && freehand_points_.back() == point) {
        return false;
    }
    freehand_points_.push_back(point);
    freehand_preview_.reset();
    return true;
}

bool AnnotationController::Commit_freehand_stroke(UndoStack &undo_stack) {
    if (!freehand_drawing_) {
        return false;
    }
    freehand_drawing_ = false;
    freehand_preview_.reset();
    if (freehand_points_.empty()) {
        return true;
    }

    Annotation annotation = Build_freehand_annotation(freehand_points_);
    freehand_points_.clear();
    size_t const insert_index = document_.annotations.size();
    // Drawing a stroke does not change selection. Preserve the pre-existing
    // selection across both undo and redo.
    undo_stack.Push(std::make_unique<AddAnnotationCommand>(
        this, insert_index, std::move(annotation), document_.selected_annotation_id,
        document_.selected_annotation_id));
    return true;
}

bool AnnotationController::Cancel_freehand_stroke() {
    if (!freehand_drawing_) {
        return false;
    }
    freehand_drawing_ = false;
    freehand_points_.clear();
    freehand_preview_.reset();
    return true;
}

bool AnnotationController::Begin_annotation_drag(uint64_t id, PointPx cursor) {
    if (active_tool_.has_value() || freehand_drawing_ || annotation_dragging_) {
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
    std::optional<size_t> const index = Index_of_annotation_id(
        document_.annotations, *document_.selected_annotation_id);
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
    std::optional<size_t> const index = Index_of_annotation_id(
        document_.annotations, *document_.selected_annotation_id);
    annotation_dragging_ = false;
    if (!index.has_value()) {
        return false;
    }

    Annotation const after = document_.annotations[*index];
    if (after == annotation_drag_before_) {
        return false;
    }

    undo_stack.Push(std::make_unique<MoveAnnotationCommand>(
        this, *index, annotation_drag_before_, after, document_.selected_annotation_id,
        document_.selected_annotation_id));
    return true;
}

bool AnnotationController::Cancel_annotation_drag() {
    if (!annotation_dragging_ || !document_.selected_annotation_id.has_value()) {
        return false;
    }
    std::optional<size_t> const index = Index_of_annotation_id(
        document_.annotations, *document_.selected_annotation_id);
    annotation_dragging_ = false;
    if (!index.has_value()) {
        return false;
    }

    Update_annotation_at(*index, annotation_drag_before_,
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

Annotation AnnotationController::Build_freehand_annotation(
    std::span<const PointPx> raw_points) const {
    Annotation annotation{};
    annotation.id = document_.next_annotation_id;
    annotation.kind = AnnotationKind::Freehand;
    annotation.freehand.style = brush_style_;
    annotation.freehand.points = smoother_.Smooth(raw_points);
    annotation.freehand.raster = Rasterize_freehand_stroke(annotation.freehand.points,
                                                           annotation.freehand.style);
    return annotation;
}

} // namespace greenflame::core
