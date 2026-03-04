// Toolbar icon placement algorithm: port of Flameshot ButtonHandler::updatePosition.
// Pure logic with no Win32 or UI dependencies.

#include "toolbar_placement.h"

namespace greenflame::core {

namespace {

struct BlockedSides {
    bool top = false;
    bool bottom = false;
    bool left = false;
    bool right = false;
};

// True if (px, py) is inside any available rect (exclusive right/bottom).
[[nodiscard]] bool Point_in_available(int px, int py,
                                      std::span<const RectPx> available) noexcept {
    for (auto const &r : available) {
        if (r.left <= px && px < r.right && r.top <= py && py < r.bottom) {
            return true;
        }
    }
    return false;
}

// Largest-intersection-area available rect vs rect. Falls back to rect if none overlap.
[[nodiscard]] RectPx
Intersect_with_available(RectPx const &rect,
                         std::span<const RectPx> available) noexcept {
    RectPx best{};
    int64_t best_area = -1;
    for (auto const &avail : available) {
        auto const opt = RectPx::Intersect(rect, avail);
        if (!opt.has_value()) {
            continue;
        }
        int64_t const area =
            static_cast<int64_t>(opt->Width()) * static_cast<int64_t>(opt->Height());
        if (area > best_area) {
            best_area = area;
            best = *opt;
        }
    }
    return (best_area > 0) ? best : rect;
}

[[nodiscard]] BlockedSides Compute_blocked_sides(RectPx const &working,
                                                 std::span<const RectPx> available,
                                                 int button_size,
                                                 int separator) noexcept {
    int const extension = separator * 2 + button_size;
    // Inclusive edges (rightmost/bottommost pixel of the working rect).
    int const incl_right = working.right - 1;
    int const incl_bottom = working.bottom - 1;

    BlockedSides b{};
    b.right = !(Point_in_available(incl_right + extension, incl_bottom, available) &&
                Point_in_available(incl_right + extension, working.top, available));
    b.left = !(Point_in_available(working.left - extension, incl_bottom, available) &&
               Point_in_available(working.left - extension, working.top, available));
    b.bottom = !(Point_in_available(working.left, incl_bottom + extension, available) &&
                 Point_in_available(incl_right, incl_bottom + extension, available));
    b.top = !(Point_in_available(working.left, working.top - extension, available) &&
              Point_in_available(incl_right, working.top - extension, available));
    return b;
}

void Ensure_min_size(RectPx &working, int button_size,
                     BlockedSides const &blocked) noexcept {
    if (working.Width() < button_size) {
        if (!blocked.left) {
            working.left -= (button_size - working.Width()) / 2;
        }
        working.right = working.left + button_size;
    }
    if (working.Height() < button_size) {
        if (!blocked.top) {
            working.top -= (button_size - working.Height()) / 2;
        }
        working.bottom = working.top + button_size;
    }
}

// Shift from center to the first button top-left coordinate along an axis.
// reverse=true when iterating in the positive direction (left_to_right / up_to_down).
[[nodiscard]] int Calc_shift(int elements, bool reverse, int button_size,
                             int button_extended, int separator) noexcept {
    int shift = 0;
    if (elements % 2 == 0) {
        shift = button_extended * (elements / 2) - separator / 2;
    } else {
        shift = button_extended * ((elements - 1) / 2) + button_size / 2;
    }
    if (!reverse) {
        shift -= button_size;
    }
    return shift;
}

// Append n button top-left positions placed horizontally around center.
void Horizontal_positions(PointPx center, int n, bool left_to_right, int button_size,
                          int button_extended, int separator,
                          std::vector<PointPx> &out) {
    int const shift =
        Calc_shift(n, left_to_right, button_size, button_extended, separator);
    int x = left_to_right ? center.x - shift : center.x + shift;
    for (int i = 0; i < n; ++i) {
        out.push_back({x, center.y});
        if (left_to_right) {
            x += button_extended;
        } else {
            x -= button_extended;
        }
    }
}

// Append n button top-left positions placed vertically around center.
void Vertical_positions(PointPx center, int n, bool up_to_down, int button_size,
                        int button_extended, int separator, std::vector<PointPx> &out) {
    int const shift =
        Calc_shift(n, up_to_down, button_size, button_extended, separator);
    int y = up_to_down ? center.y - shift : center.y + shift;
    for (int i = 0; i < n; ++i) {
        out.push_back({center.x, y});
        if (up_to_down) {
            y += button_extended;
        } else {
            y -= button_extended;
        }
    }
}

void Adjust_horizontal_center(PointPx &center, BlockedSides const &blocked,
                              int button_extended) noexcept {
    if (blocked.left) {
        center.x += button_extended / 2;
    } else if (blocked.right) {
        center.x -= button_extended / 2;
    }
}

// Place remaining buttons inside the original selection, bottom-center upward.
void Position_buttons_inside(int elem, int button_count, RectPx const &original_sel,
                             std::span<const RectPx> available, int button_size,
                             int button_extended, std::vector<PointPx> &positions) {
    RectPx const main_area = Intersect_with_available(original_sel, available);
    int const buttons_per_row = main_area.Width() / button_extended;
    if (buttons_per_row == 0) {
        return;
    }
    // center.y: top of the bottom-most row, sitting button_extended above inclusive
    // bottom.
    PointPx center{(main_area.left + main_area.right) / 2,
                   main_area.bottom - 1 - button_extended};
    int separator = button_extended - button_size;
    while (elem < button_count) {
        int const add = std::min(buttons_per_row, button_count - elem);
        Horizontal_positions(center, add, true, button_size, button_extended, separator,
                             positions);
        elem += add;
        center.y -= button_extended;
    }
}

} // namespace

ToolbarPlacementResult Compute_toolbar_placement(ToolbarPlacementParams const &p) {
    ToolbarPlacementResult result{};
    if (p.button_count <= 0) {
        return result;
    }

    int const button_extended = p.button_size + p.separator;
    if (button_extended <= 0) {
        return result;
    }

    RectPx working = Intersect_with_available(p.selection, p.available);
    BlockedSides blocked =
        Compute_blocked_sides(working, p.available, p.button_size, p.separator);
    Ensure_min_size(working, p.button_size, blocked);

    int elem = 0;
    while (elem < p.button_count) {
        bool const one_horiz_blocked =
            (!blocked.right && blocked.left) || (blocked.right && !blocked.left);
        bool const horiz_blocked = blocked.right && blocked.left;
        bool const all_blocked = blocked.bottom && horiz_blocked && blocked.top;

        if (all_blocked) {
            Position_buttons_inside(elem, p.button_count, p.selection, p.available,
                                    p.button_size, button_extended, result.positions);
            result.buttons_inside = true;
            break;
        }

        int const per_row = (working.Width() + p.separator) / button_extended;
        int const per_col = (working.Height() + p.separator) / button_extended;
        int const extra = (p.button_count - elem) - (per_row + per_col) * 2;
        int corner_elems = extra > 4 ? 4 : (extra > 0 ? extra : 0);
        int const max_corner = one_horiz_blocked ? 1 : (horiz_blocked ? 0 : 2);
        int const corner_top = std::min(corner_elems, max_corner);
        corner_elems -= corner_top;
        int const corner_bottom = std::min(corner_elems, max_corner);

        int const cx = (working.left + working.right) / 2;
        int const cy = (working.top + working.bottom) / 2;

        // Bottom row (left-to-right, buttons below selection).
        if (!blocked.bottom) {
            int const add = std::min(per_row + corner_bottom, p.button_count - elem);
            PointPx center{cx, working.bottom + p.separator};
            if (corner_bottom > 0) {
                Adjust_horizontal_center(center, blocked, button_extended);
            }
            Horizontal_positions(center, add, true, p.button_size, button_extended,
                                 p.separator, result.positions);
            elem += add;
        }

        // Right column (upward from center, buttons right of selection).
        if (!blocked.right && elem < p.button_count) {
            int const add = std::min(per_col, p.button_count - elem);
            PointPx center{working.right + p.separator, cy};
            Vertical_positions(center, add, false, p.button_size, button_extended,
                               p.separator, result.positions);
            elem += add;
        }

        // Top row (right-to-left, buttons above selection).
        if (!blocked.top && elem < p.button_count) {
            int const add = std::min(per_row + corner_top, p.button_count - elem);
            PointPx center{cx, working.top - button_extended};
            if (corner_top > 0) {
                Adjust_horizontal_center(center, blocked, button_extended);
            }
            Horizontal_positions(center, add, false, p.button_size, button_extended,
                                 p.separator, result.positions);
            elem += add;
        }

        // Left column (downward from center, buttons left of selection).
        if (!blocked.left && elem < p.button_count) {
            int const add = std::min(per_col, p.button_count - elem);
            PointPx center{working.left - button_extended, cy};
            Vertical_positions(center, add, true, p.button_size, button_extended,
                               p.separator, result.positions);
            elem += add;
        }

        // If buttons remain, expand working rect by button_extended on each side.
        if (elem < p.button_count) {
            working = RectPx::From_ltrb(
                working.left - button_extended, working.top - button_extended,
                working.right + button_extended, working.bottom + button_extended);
            working = Intersect_with_available(working, p.available);
            blocked =
                Compute_blocked_sides(working, p.available, p.button_size, p.separator);
        }
    }

    return result;
}

} // namespace greenflame::core
