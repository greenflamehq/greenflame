#pragma once

#include "greenflame_core/command.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace greenflame::core {

class UndoStack {
  public:
    void Push(std::unique_ptr<ICommand> cmd);

    bool CanUndo() const;
    bool CanRedo() const;

    void Undo();
    void Redo();

    void Clear();

    std::size_t Count() const;
    int Index() const;

    void SetUndoLimit(int limit);
    int UndoLimit() const;

  private:
    std::vector<std::unique_ptr<ICommand>> commands_;
    int index_ = 0;
    int undoLimit_ = 0;
};

} // namespace greenflame::core
