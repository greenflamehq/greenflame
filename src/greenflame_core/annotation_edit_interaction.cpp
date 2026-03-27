#include "greenflame_core/annotation_edit_interaction.h"

namespace greenflame::core {

namespace {

[[nodiscard]] AnnotationEditTargetKind
Target_kind_for_rectangle_handle(SelectionHandle handle) noexcept {
    switch (handle) {
    case SelectionHandle::TopLeft:
        return AnnotationEditTargetKind::RectangleTopLeftHandle;
    case SelectionHandle::TopRight:
        return AnnotationEditTargetKind::RectangleTopRightHandle;
    case SelectionHandle::BottomRight:
        return AnnotationEditTargetKind::RectangleBottomRightHandle;
    case SelectionHandle::BottomLeft:
        return AnnotationEditTargetKind::RectangleBottomLeftHandle;
    case SelectionHandle::Top:
        return AnnotationEditTargetKind::RectangleTopHandle;
    case SelectionHandle::Right:
        return AnnotationEditTargetKind::RectangleRightHandle;
    case SelectionHandle::Bottom:
        return AnnotationEditTargetKind::RectangleBottomHandle;
    case SelectionHandle::Left:
        return AnnotationEditTargetKind::RectangleLeftHandle;
    }
    return AnnotationEditTargetKind::RectangleTopLeftHandle;
}

[[nodiscard]] AnnotationEditHandleKind
Handle_kind_for_rectangle_handle(SelectionHandle handle) noexcept {
    switch (handle) {
    case SelectionHandle::TopLeft:
        return AnnotationEditHandleKind::RectangleTopLeft;
    case SelectionHandle::TopRight:
        return AnnotationEditHandleKind::RectangleTopRight;
    case SelectionHandle::BottomRight:
        return AnnotationEditHandleKind::RectangleBottomRight;
    case SelectionHandle::BottomLeft:
        return AnnotationEditHandleKind::RectangleBottomLeft;
    case SelectionHandle::Top:
        return AnnotationEditHandleKind::RectangleTop;
    case SelectionHandle::Right:
        return AnnotationEditHandleKind::RectangleRight;
    case SelectionHandle::Bottom:
        return AnnotationEditHandleKind::RectangleBottom;
    case SelectionHandle::Left:
        return AnnotationEditHandleKind::RectangleLeft;
    }
    return AnnotationEditHandleKind::RectangleTopLeft;
}

[[nodiscard]] std::optional<SelectionHandle>
Rectangle_handle_from_target_kind(AnnotationEditTargetKind kind) noexcept {
    switch (kind) {
    case AnnotationEditTargetKind::RectangleTopLeftHandle:
        return SelectionHandle::TopLeft;
    case AnnotationEditTargetKind::RectangleTopRightHandle:
        return SelectionHandle::TopRight;
    case AnnotationEditTargetKind::RectangleBottomRightHandle:
        return SelectionHandle::BottomRight;
    case AnnotationEditTargetKind::RectangleBottomLeftHandle:
        return SelectionHandle::BottomLeft;
    case AnnotationEditTargetKind::RectangleTopHandle:
        return SelectionHandle::Top;
    case AnnotationEditTargetKind::RectangleRightHandle:
        return SelectionHandle::Right;
    case AnnotationEditTargetKind::RectangleBottomHandle:
        return SelectionHandle::Bottom;
    case AnnotationEditTargetKind::RectangleLeftHandle:
        return SelectionHandle::Left;
    case AnnotationEditTargetKind::Body:
    case AnnotationEditTargetKind::LineStartHandle:
    case AnnotationEditTargetKind::LineEndHandle:
    case AnnotationEditTargetKind::FreehandStrokeStartHandle:
    case AnnotationEditTargetKind::FreehandStrokeEndHandle:
        return std::nullopt;
    }
    return std::nullopt;
}

class MoveAnnotationEditInteraction final : public IAnnotationEditInteraction {
  public:
    MoveAnnotationEditInteraction(uint64_t annotation_id, size_t index,
                                  Annotation annotation_before, PointPx drag_start)
        : annotation_id_(annotation_id), index_(index),
          annotation_before_(std::move(annotation_before)),
          annotation_after_(annotation_before_), drag_start_(drag_start) {}

    [[nodiscard]] bool Update(IAnnotationEditInteractionHost &host,
                              PointPx cursor) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_) {
            return false;
        }

        PointPx const delta{cursor.x - drag_start_.x, cursor.y - drag_start_.y};
        Annotation const moved = Translate_annotation(annotation_before_, delta);
        if (*current == moved) {
            return false;
        }

        annotation_after_ = moved;
        host.Update_annotation_at(index_, moved, annotation_id_);
        return true;
    }

    [[nodiscard]] std::optional<AnnotationEditCommandData> Commit() noexcept override {
        if (annotation_after_ == annotation_before_) {
            return std::nullopt;
        }
        return AnnotationEditCommandData{
            index_,         annotation_before_, annotation_after_,
            annotation_id_, annotation_id_,     "Move annotation"};
    }

    [[nodiscard]] bool Cancel(IAnnotationEditInteractionHost &host) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_) {
            return false;
        }

        annotation_after_ = annotation_before_;
        host.Update_annotation_at(index_, annotation_before_, annotation_id_);
        return true;
    }

    [[nodiscard]] bool Is_move_drag() const noexcept override { return true; }

    [[nodiscard]] std::optional<AnnotationEditPreview>
    Preview() const noexcept override {
        return AnnotationEditPreview{index_, annotation_before_, annotation_after_};
    }

  private:
    uint64_t annotation_id_ = 0;
    size_t index_ = 0;
    Annotation annotation_before_ = {};
    Annotation annotation_after_ = {};
    PointPx drag_start_ = {};
};

// Base class for endpoint-drag interactions (line endpoints, straight highlighter
// endpoints). Handles the shared Update/Commit/Cancel/Active_handle logic.
// Subclasses override Move_endpoint to perform the type-specific point mutation.
class EndpointEditInteractionBase : public IAnnotationEditInteraction {
  public:
    EndpointEditInteractionBase(uint64_t annotation_id, size_t index,
                                Annotation annotation_before,
                                AnnotationEditHandleKind active_handle,
                                std::string_view description)
        : annotation_id_(annotation_id), index_(index),
          annotation_before_(std::move(annotation_before)),
          annotation_after_(annotation_before_), active_handle_(active_handle),
          description_(description) {}

    [[nodiscard]] bool Update(IAnnotationEditInteractionHost &host,
                              PointPx cursor) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_) {
            return false;
        }
        Annotation edited = annotation_before_;
        if (!Move_endpoint(edited, active_handle_, cursor)) {
            return false;
        }
        if (*current == edited) {
            return false;
        }
        annotation_after_ = edited;
        host.Update_annotation_at(index_, edited, annotation_id_);
        return true;
    }

    [[nodiscard]] std::optional<AnnotationEditCommandData> Commit() noexcept override {
        if (annotation_after_ == annotation_before_) {
            return std::nullopt;
        }
        return AnnotationEditCommandData{
            index_,         annotation_before_, annotation_after_,
            annotation_id_, annotation_id_,     description_};
    }

    [[nodiscard]] bool Cancel(IAnnotationEditInteractionHost &host) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_) {
            return false;
        }
        annotation_after_ = annotation_before_;
        host.Update_annotation_at(index_, annotation_before_, annotation_id_);
        return true;
    }

    [[nodiscard]] std::optional<AnnotationEditHandleKind>
    Active_handle() const noexcept override {
        return active_handle_;
    }

    [[nodiscard]] std::optional<AnnotationEditPreview>
    Preview() const noexcept override {
        return AnnotationEditPreview{index_, annotation_before_, annotation_after_};
    }

  protected:
    // Apply cursor to the appropriate endpoint of edited. Return false if the
    // annotation type is wrong or the data is otherwise invalid.
    [[nodiscard]] virtual bool Move_endpoint(Annotation &edited,
                                             AnnotationEditHandleKind handle,
                                             PointPx cursor) = 0;

  private:
    uint64_t annotation_id_ = 0;
    size_t index_ = 0;
    Annotation annotation_before_ = {};
    Annotation annotation_after_ = {};
    AnnotationEditHandleKind active_handle_ = AnnotationEditHandleKind::LineStart;
    std::string_view description_;
};

class LineEndpointEditInteraction final : public EndpointEditInteractionBase {
  public:
    LineEndpointEditInteraction(uint64_t annotation_id, size_t index,
                                Annotation annotation_before,
                                AnnotationEditHandleKind active_handle)
        : EndpointEditInteractionBase(annotation_id, index,
                                      std::move(annotation_before), active_handle,
                                      "Edit line annotation") {}

  protected:
    [[nodiscard]] bool Move_endpoint(Annotation &edited,
                                     AnnotationEditHandleKind handle,
                                     PointPx cursor) override {
        LineAnnotation *const line = std::get_if<LineAnnotation>(&edited.data);
        if (line == nullptr) {
            return false;
        }
        if (handle == AnnotationEditHandleKind::LineStart) {
            line->start = cursor;
        } else {
            line->end = cursor;
        }
        return true;
    }
};

class FreehandStrokeEndpointEditInteraction final : public EndpointEditInteractionBase {
  public:
    FreehandStrokeEndpointEditInteraction(uint64_t annotation_id, size_t index,
                                          Annotation annotation_before,
                                          AnnotationEditHandleKind active_handle)
        : EndpointEditInteractionBase(annotation_id, index,
                                      std::move(annotation_before), active_handle,
                                      "Edit highlighter annotation") {}

  protected:
    [[nodiscard]] bool Move_endpoint(Annotation &edited,
                                     AnnotationEditHandleKind handle,
                                     PointPx cursor) override {
        FreehandStrokeAnnotation *const fh =
            std::get_if<FreehandStrokeAnnotation>(&edited.data);
        if (fh == nullptr || fh->points.size() < 2) {
            return false;
        }
        if (handle == AnnotationEditHandleKind::FreehandStrokeStart) {
            fh->points[0] = cursor;
        } else {
            fh->points[1] = cursor;
        }
        return true;
    }
};

class BoundsResizeEditInteraction final : public IAnnotationEditInteraction {
  public:
    BoundsResizeEditInteraction(uint64_t annotation_id, size_t index,
                                Annotation annotation_before, SelectionHandle handle)
        : annotation_id_(annotation_id), index_(index),
          annotation_before_(std::move(annotation_before)),
          annotation_after_(annotation_before_), handle_(handle) {}

    [[nodiscard]] bool Update(IAnnotationEditInteractionHost &host,
                              PointPx cursor) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_) {
            return false;
        }

        Annotation edited = annotation_before_;
        bool updated = false;
        std::visit(Overloaded{
                       [&](RectangleAnnotation &rect) noexcept {
                           rect.outer_bounds = Resize_rectangle_from_handle(
                               rect.outer_bounds, handle_, cursor);
                           updated = true;
                       },
                       [&](EllipseAnnotation &ellipse) noexcept {
                           ellipse.outer_bounds = Resize_rectangle_from_handle(
                               ellipse.outer_bounds, handle_, cursor);
                           updated = true;
                       },
                       [&](ObfuscateAnnotation &obfuscate) noexcept {
                           obfuscate.bounds = Resize_rectangle_from_handle(
                               obfuscate.bounds, handle_, cursor);
                           updated = true;
                       },
                       [&](auto &) noexcept {},
                   },
                   edited.data);
        if (!updated) {
            return false;
        }
        if (*current == edited) {
            return false;
        }

        annotation_after_ = edited;
        host.Update_annotation_at(index_, edited, annotation_id_);
        return true;
    }

    [[nodiscard]] std::optional<AnnotationEditCommandData> Commit() noexcept override {
        if (annotation_after_ == annotation_before_) {
            return std::nullopt;
        }
        std::string_view const description =
            std::holds_alternative<EllipseAnnotation>(annotation_before_.data)
                ? "Resize ellipse annotation"
                : (std::holds_alternative<ObfuscateAnnotation>(annotation_before_.data)
                       ? "Resize obfuscate annotation"
                       : "Resize rectangle annotation");
        return AnnotationEditCommandData{
            index_,         annotation_before_, annotation_after_,
            annotation_id_, annotation_id_,     description};
    }

    [[nodiscard]] bool Cancel(IAnnotationEditInteractionHost &host) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_) {
            return false;
        }

        annotation_after_ = annotation_before_;
        host.Update_annotation_at(index_, annotation_before_, annotation_id_);
        return true;
    }

    [[nodiscard]] std::optional<AnnotationEditHandleKind>
    Active_handle() const noexcept override {
        return Handle_kind_for_rectangle_handle(handle_);
    }

    [[nodiscard]] std::optional<AnnotationEditPreview>
    Preview() const noexcept override {
        return AnnotationEditPreview{index_, annotation_before_, annotation_after_};
    }

  private:
    uint64_t annotation_id_ = 0;
    size_t index_ = 0;
    Annotation annotation_before_ = {};
    Annotation annotation_after_ = {};
    SelectionHandle handle_ = SelectionHandle::TopLeft;
};

} // namespace

std::optional<AnnotationEditTarget>
Hit_test_annotation_edit_target(Annotation const *selected_annotation,
                                std::span<const Annotation> annotations,
                                PointPx cursor) noexcept {
    if (LineAnnotation const *const sel_line =
            selected_annotation
                ? std::get_if<LineAnnotation>(&selected_annotation->data)
                : nullptr) {
        if (std::optional<AnnotationLineEndpoint> const endpoint =
                Hit_test_line_endpoint_handles(sel_line->start, sel_line->end, cursor);
            endpoint.has_value()) {
            return AnnotationEditTarget{selected_annotation->id,
                                        *endpoint == AnnotationLineEndpoint::Start
                                            ? AnnotationEditTargetKind::LineStartHandle
                                            : AnnotationEditTargetKind::LineEndHandle};
        }
    } else if (FreehandStrokeAnnotation const *const sel_fh =
                   selected_annotation ? std::get_if<FreehandStrokeAnnotation>(
                                             &selected_annotation->data)
                                       : nullptr;
               sel_fh != nullptr &&
               sel_fh->freehand_tip_shape == FreehandTipShape::Square &&
               sel_fh->points.size() == 2) {
        if (std::optional<AnnotationLineEndpoint> const endpoint =
                Hit_test_line_endpoint_handles(sel_fh->points[0], sel_fh->points[1],
                                               cursor);
            endpoint.has_value()) {
            return AnnotationEditTarget{
                selected_annotation->id,
                *endpoint == AnnotationLineEndpoint::Start
                    ? AnnotationEditTargetKind::FreehandStrokeStartHandle
                    : AnnotationEditTargetKind::FreehandStrokeEndHandle};
        }
    } else if (RectangleAnnotation const *const sel_rect =
                   selected_annotation
                       ? std::get_if<RectangleAnnotation>(&selected_annotation->data)
                       : nullptr) {
        if (std::optional<SelectionHandle> const handle =
                Hit_test_rectangle_resize_handles(sel_rect->outer_bounds, cursor);
            handle.has_value()) {
            return AnnotationEditTarget{selected_annotation->id,
                                        Target_kind_for_rectangle_handle(*handle)};
        }
    } else if (EllipseAnnotation const *const sel_ellipse =
                   selected_annotation
                       ? std::get_if<EllipseAnnotation>(&selected_annotation->data)
                       : nullptr) {
        if (std::optional<SelectionHandle> const handle =
                Hit_test_rectangle_resize_handles(sel_ellipse->outer_bounds, cursor);
            handle.has_value()) {
            return AnnotationEditTarget{selected_annotation->id,
                                        Target_kind_for_rectangle_handle(*handle)};
        }
    } else if (ObfuscateAnnotation const *const sel_obfuscate =
                   selected_annotation
                       ? std::get_if<ObfuscateAnnotation>(&selected_annotation->data)
                       : nullptr) {
        if (std::optional<SelectionHandle> const handle =
                Hit_test_rectangle_resize_handles(sel_obfuscate->bounds, cursor);
            handle.has_value()) {
            return AnnotationEditTarget{selected_annotation->id,
                                        Target_kind_for_rectangle_handle(*handle)};
        }
    }

    std::optional<size_t> const index =
        Index_of_topmost_annotation_at(annotations, cursor);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return AnnotationEditTarget{annotations[*index].id, AnnotationEditTargetKind::Body};
}

std::unique_ptr<IAnnotationEditInteraction>
Create_annotation_edit_interaction(AnnotationEditTarget target, size_t index,
                                   Annotation annotation_before, PointPx cursor) {
    switch (target.kind) {
    case AnnotationEditTargetKind::Body:
        return std::make_unique<MoveAnnotationEditInteraction>(
            target.annotation_id, index, std::move(annotation_before), cursor);
    case AnnotationEditTargetKind::LineStartHandle:
        if (!std::holds_alternative<LineAnnotation>(annotation_before.data)) {
            return {};
        }
        return std::make_unique<LineEndpointEditInteraction>(
            target.annotation_id, index, std::move(annotation_before),
            AnnotationEditHandleKind::LineStart);
    case AnnotationEditTargetKind::LineEndHandle:
        if (!std::holds_alternative<LineAnnotation>(annotation_before.data)) {
            return {};
        }
        return std::make_unique<LineEndpointEditInteraction>(
            target.annotation_id, index, std::move(annotation_before),
            AnnotationEditHandleKind::LineEnd);
    case AnnotationEditTargetKind::FreehandStrokeStartHandle:
        if (!std::holds_alternative<FreehandStrokeAnnotation>(annotation_before.data)) {
            return {};
        }
        return std::make_unique<FreehandStrokeEndpointEditInteraction>(
            target.annotation_id, index, std::move(annotation_before),
            AnnotationEditHandleKind::FreehandStrokeStart);
    case AnnotationEditTargetKind::FreehandStrokeEndHandle:
        if (!std::holds_alternative<FreehandStrokeAnnotation>(annotation_before.data)) {
            return {};
        }
        return std::make_unique<FreehandStrokeEndpointEditInteraction>(
            target.annotation_id, index, std::move(annotation_before),
            AnnotationEditHandleKind::FreehandStrokeEnd);
    case AnnotationEditTargetKind::RectangleTopLeftHandle:
    case AnnotationEditTargetKind::RectangleTopRightHandle:
    case AnnotationEditTargetKind::RectangleBottomRightHandle:
    case AnnotationEditTargetKind::RectangleBottomLeftHandle:
    case AnnotationEditTargetKind::RectangleTopHandle:
    case AnnotationEditTargetKind::RectangleRightHandle:
    case AnnotationEditTargetKind::RectangleBottomHandle:
    case AnnotationEditTargetKind::RectangleLeftHandle:
        if (!std::holds_alternative<RectangleAnnotation>(annotation_before.data) &&
            !std::holds_alternative<EllipseAnnotation>(annotation_before.data) &&
            !std::holds_alternative<ObfuscateAnnotation>(annotation_before.data)) {
            return {};
        }
        if (std::optional<SelectionHandle> const handle =
                Rectangle_handle_from_target_kind(target.kind);
            handle.has_value()) {
            return std::make_unique<BoundsResizeEditInteraction>(
                target.annotation_id, index, std::move(annotation_before), *handle);
        }
        return {};
    }
    return {};
}

} // namespace greenflame::core
