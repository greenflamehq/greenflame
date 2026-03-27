#include "greenflame_core/annotation_commands.h"

#include "greenflame_core/annotation_controller.h"

namespace greenflame::core {

CompoundCommand::CompoundCommand(std::vector<std::unique_ptr<ICommand>> commands,
                                 std::string_view description)
    : commands_(std::move(commands)), description_(description) {}

void CompoundCommand::Undo() {
    for (size_t index = commands_.size(); index > 0; --index) {
        commands_[index - 1u]->Undo();
    }
}

void CompoundCommand::Redo() {
    for (auto &command : commands_) {
        command->Redo();
    }
}

AddAnnotationCommand::AddAnnotationCommand(AnnotationController *controller,
                                           size_t index, Annotation annotation,
                                           std::optional<uint64_t> selection_before,
                                           std::optional<uint64_t> selection_after)
    : controller_(controller), index_(index), annotation_(std::move(annotation)),
      selection_before_(selection_before), selection_after_(selection_after) {}

void AddAnnotationCommand::Undo() {
    if (controller_ != nullptr) {
        controller_->Erase_annotation_at(index_, selection_before_);
    }
}

void AddAnnotationCommand::Redo() {
    if (controller_ != nullptr) {
        controller_->Insert_annotation_at(index_, annotation_, selection_after_);
    }
}

DeleteAnnotationCommand::DeleteAnnotationCommand(
    AnnotationController *controller, size_t index, Annotation annotation,
    std::optional<uint64_t> selection_before, std::optional<uint64_t> selection_after)
    : controller_(controller), index_(index), annotation_(std::move(annotation)),
      selection_before_(selection_before), selection_after_(selection_after) {}

void DeleteAnnotationCommand::Undo() {
    if (controller_ != nullptr) {
        controller_->Insert_annotation_at(index_, annotation_, selection_before_);
    }
}

void DeleteAnnotationCommand::Redo() {
    if (controller_ != nullptr) {
        controller_->Erase_annotation_at(index_, selection_after_);
    }
}

UpdateAnnotationCommand::UpdateAnnotationCommand(
    AnnotationController *controller, size_t index, Annotation annotation_before,
    Annotation annotation_after, std::optional<uint64_t> selection_before,
    std::optional<uint64_t> selection_after, std::string_view description)
    : controller_(controller), index_(index),
      annotation_before_(std::move(annotation_before)),
      annotation_after_(std::move(annotation_after)),
      selection_before_(selection_before), selection_after_(selection_after),
      description_(description) {}

void UpdateAnnotationCommand::Undo() {
    if (controller_ != nullptr) {
        controller_->Update_annotation_at(index_, annotation_before_,
                                          selection_before_);
    }
}

void UpdateAnnotationCommand::Redo() {
    if (controller_ != nullptr) {
        controller_->Update_annotation_at(index_, annotation_after_, selection_after_);
    }
}

AddBubbleAnnotationCommand::AddBubbleAnnotationCommand(
    AnnotationController *controller, size_t index, Annotation annotation,
    std::optional<uint64_t> selection_before, std::optional<uint64_t> selection_after)
    : controller_(controller), index_(index), annotation_(std::move(annotation)),
      selection_before_(selection_before), selection_after_(selection_after) {}

void AddBubbleAnnotationCommand::Undo() {
    if (controller_ != nullptr) {
        controller_->Erase_annotation_at(index_, selection_before_);
        controller_->Decrement_bubble_counter();
    }
}

void AddBubbleAnnotationCommand::Redo() {
    if (controller_ != nullptr) {
        controller_->Insert_annotation_at(index_, annotation_, selection_after_);
        controller_->Increment_bubble_counter();
    }
}

} // namespace greenflame::core
