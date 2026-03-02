#pragma once

#include <string_view>

namespace greenflame::core {

class ICommand {
  public:
    virtual ~ICommand() = default;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
    virtual std::string_view Description() const { return ""; }
};

} // namespace greenflame::core
