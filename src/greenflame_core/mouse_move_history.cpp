#include "greenflame_core/mouse_move_history.h"

namespace greenflame::core {

namespace {

constexpr int32_t kMaxPositive16Bit = 32767;
constexpr int32_t kWrap16Bit = 65536;

[[nodiscard]] MouseMoveSample Unwrap(MouseMoveSample sample) noexcept {
    return MouseMoveSample{
        .screen = {Unwrap_mouse_history_coordinate(sample.screen.x),
                   Unwrap_mouse_history_coordinate(sample.screen.y)},
        .time_ms = sample.time_ms,
    };
}

} // namespace

int32_t Unwrap_mouse_history_coordinate(int32_t value) noexcept {
    return value > kMaxPositive16Bit ? value - kWrap16Bit : value;
}

bool Is_tick_newer(uint32_t time_ms, uint32_t than_ms) noexcept {
    return static_cast<int32_t>(time_ms - than_ms) > 0;
}

MouseHistoryResult
Collect_new_mouse_points(std::span<const MouseMoveSample> raw_newest_first,
                         MouseMoveSample last_consumed, PointPx client_origin_screen) {
    MouseHistoryResult result{};
    result.newest = last_consumed;

    // Walk back to the last consumed sample, or to the first sample not newer than
    // it when the exact sample is no longer (or never was) in the buffer.
    size_t count = 0;
    bool matched = false;
    for (MouseMoveSample const &raw : raw_newest_first) {
        MouseMoveSample const sample = Unwrap(raw);
        if (sample == last_consumed) {
            matched = true;
            break;
        }
        if (!Is_tick_newer(sample.time_ms, last_consumed.time_ms)) {
            break;
        }
        ++count;
    }
    result.gap = !matched && count == raw_newest_first.size() &&
                 raw_newest_first.size() >= kMouseMoveHistoryCapacity;
    if (count == 0) {
        return result;
    }

    result.newest = Unwrap(raw_newest_first.front());
    result.points_client.reserve(count);
    PointPx previous = last_consumed.screen;
    for (size_t index = count; index > 0; --index) {
        PointPx const screen = Unwrap(raw_newest_first[index - 1]).screen;
        if (screen == previous) {
            continue;
        }
        previous = screen;
        result.points_client.push_back(
            {screen.x - client_origin_screen.x, screen.y - client_origin_screen.y});
    }
    return result;
}

} // namespace greenflame::core
