#pragma once

#include "greenflame_core/command.h"

#include <functional>
#include <string>
#include <string_view>

namespace greenflame::core {

template <typename T> class ModificationCommand final : public ICommand {
  public:
    ModificationCommand(std::string_view description,
                        std::function<void(T const &)> apply, T before, T after)
        : description_(description), apply_(std::move(apply)),
          before_(std::move(before)), after_(std::move(after)) {}

    void Undo() override { apply_(before_); }
    void Redo() override { apply_(after_); }
    std::string_view Description() const override { return description_; }

  private:
    std::string description_;
    std::function<void(T const &)> apply_;
    T before_;
    T after_;
};

} // namespace greenflame::core
