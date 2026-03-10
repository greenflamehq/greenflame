#include "greenflame_core/undo_stack.h"

namespace greenflame::core {

void UndoStack::Push(std::unique_ptr<ICommand> cmd) {
    // Discard redo branch (commands from index to end)
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(index_),
                    commands_.end());

    cmd->Redo();
    commands_.push_back(std::move(cmd));
    index_ = static_cast<int>(commands_.size());

    // Enforce undo limit
    if (undoLimit_ > 0) {
        while (commands_.size() > static_cast<std::size_t>(undoLimit_)) {
            commands_.erase(commands_.begin());
            --index_;
        }
    }
}

bool UndoStack::Can_undo() const { return index_ > 0; }

bool UndoStack::Can_redo() const { return index_ < static_cast<int>(commands_.size()); }

void UndoStack::Undo() {
    if (!Can_undo()) return;
    --index_;
    commands_[static_cast<std::size_t>(index_)]->Undo();
}

void UndoStack::Redo() {
    if (!Can_redo()) return;
    commands_[static_cast<std::size_t>(index_)]->Redo();
    ++index_;
}

void UndoStack::Clear() {
    commands_.clear();
    index_ = 0;
}

std::size_t UndoStack::Count() const { return commands_.size(); }

int UndoStack::Index() const { return index_; }

void UndoStack::Set_undo_limit(int limit) { undoLimit_ = limit; }

int UndoStack::Undo_limit() const { return undoLimit_; }

} // namespace greenflame::core
