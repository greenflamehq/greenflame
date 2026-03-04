#pragma once

#include "greenflame_core/rect_px.h"
#include <span>
#include <vector>

namespace greenflame::core {

struct ToolbarPlacementParams {
    RectPx selection;                  // Committed selection, client-space physical px
    std::span<const RectPx> available; // Monitor rects, client-space physical px
    int button_size;                   // Diameter / side length (px)
    int separator;                     // Gap between buttons (px)
    int button_count;
};

struct ToolbarPlacementResult {
    std::vector<PointPx> positions; // Top-left corner per button
    bool buttons_inside = false;    // True if fell back to inside-selection placement
};

[[nodiscard]] ToolbarPlacementResult
Compute_toolbar_placement(ToolbarPlacementParams const &p);

} // namespace greenflame::core
