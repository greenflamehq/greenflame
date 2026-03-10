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

  private:
    uint64_t annotation_id_ = 0;
    size_t index_ = 0;
    Annotation annotation_before_ = {};
    Annotation annotation_after_ = {};
    PointPx drag_start_ = {};
};

class LineEndpointEditInteraction final : public IAnnotationEditInteraction {
  public:
    LineEndpointEditInteraction(uint64_t annotation_id, size_t index,
                                Annotation annotation_before,
                                AnnotationEditHandleKind active_handle)
        : annotation_id_(annotation_id), index_(index),
          annotation_before_(std::move(annotation_before)),
          annotation_after_(annotation_before_), active_handle_(active_handle) {}

    [[nodiscard]] bool Update(IAnnotationEditInteractionHost &host,
                              PointPx cursor) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_ ||
            annotation_before_.kind != AnnotationKind::Line) {
            return false;
        }

        Annotation edited = annotation_before_;
        if (active_handle_ == AnnotationEditHandleKind::LineStart) {
            edited.line.start = cursor;
        } else {
            edited.line.end = cursor;
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
            annotation_id_, annotation_id_,     "Edit line annotation"};
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

  private:
    uint64_t annotation_id_ = 0;
    size_t index_ = 0;
    Annotation annotation_before_ = {};
    Annotation annotation_after_ = {};
    AnnotationEditHandleKind active_handle_ = AnnotationEditHandleKind::LineStart;
};

class RectangleResizeEditInteraction final : public IAnnotationEditInteraction {
  public:
    RectangleResizeEditInteraction(uint64_t annotation_id, size_t index,
                                   Annotation annotation_before, SelectionHandle handle)
        : annotation_id_(annotation_id), index_(index),
          annotation_before_(std::move(annotation_before)),
          annotation_after_(annotation_before_), handle_(handle) {}

    [[nodiscard]] bool Update(IAnnotationEditInteractionHost &host,
                              PointPx cursor) override {
        Annotation const *const current = host.Annotation_at(index_);
        if (current == nullptr || current->id != annotation_id_ ||
            annotation_before_.kind != AnnotationKind::Rectangle) {
            return false;
        }

        Annotation edited = annotation_before_;
        edited.rectangle.outer_bounds = Resize_rectangle_from_handle(
            annotation_before_.rectangle.outer_bounds, handle_, cursor);
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
            annotation_id_, annotation_id_,     "Resize rectangle annotation"};
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
    if (selected_annotation != nullptr &&
        selected_annotation->kind == AnnotationKind::Line) {
        if (std::optional<AnnotationLineEndpoint> const endpoint =
                Hit_test_line_endpoint_handles(selected_annotation->line.start,
                                               selected_annotation->line.end, cursor);
            endpoint.has_value()) {
            return AnnotationEditTarget{selected_annotation->id,
                                        *endpoint == AnnotationLineEndpoint::Start
                                            ? AnnotationEditTargetKind::LineStartHandle
                                            : AnnotationEditTargetKind::LineEndHandle};
        }
    } else if (selected_annotation != nullptr &&
               selected_annotation->kind == AnnotationKind::Rectangle) {
        if (std::optional<SelectionHandle> const handle =
                Hit_test_rectangle_resize_handles(
                    selected_annotation->rectangle.outer_bounds, cursor);
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
        if (annotation_before.kind != AnnotationKind::Line) {
            return {};
        }
        return std::make_unique<LineEndpointEditInteraction>(
            target.annotation_id, index, std::move(annotation_before),
            AnnotationEditHandleKind::LineStart);
    case AnnotationEditTargetKind::LineEndHandle:
        if (annotation_before.kind != AnnotationKind::Line) {
            return {};
        }
        return std::make_unique<LineEndpointEditInteraction>(
            target.annotation_id, index, std::move(annotation_before),
            AnnotationEditHandleKind::LineEnd);
    case AnnotationEditTargetKind::RectangleTopLeftHandle:
    case AnnotationEditTargetKind::RectangleTopRightHandle:
    case AnnotationEditTargetKind::RectangleBottomRightHandle:
    case AnnotationEditTargetKind::RectangleBottomLeftHandle:
    case AnnotationEditTargetKind::RectangleTopHandle:
    case AnnotationEditTargetKind::RectangleRightHandle:
    case AnnotationEditTargetKind::RectangleBottomHandle:
    case AnnotationEditTargetKind::RectangleLeftHandle:
        if (annotation_before.kind != AnnotationKind::Rectangle) {
            return {};
        }
        if (std::optional<SelectionHandle> const handle =
                Rectangle_handle_from_target_kind(target.kind);
            handle.has_value()) {
            return std::make_unique<RectangleResizeEditInteraction>(
                target.annotation_id, index, std::move(annotation_before), *handle);
        }
        return {};
    }
    return {};
}

} // namespace greenflame::core
