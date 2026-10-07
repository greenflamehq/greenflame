#include "greenflame_core/mouse_move_history.h"

using namespace greenflame::core;

namespace {

constexpr int32_t kWrap = 65536;

// Jocelyn's desk: the laptop sits left of the primary at (-1920, 137). The overlay
// covers the virtual desktop, so client (0, 0) is screen (-1920, 0).
constexpr PointPx kOrigin = {-1920, 0};

[[nodiscard]] MouseMoveSample Raw(int32_t x, int32_t y, uint32_t time_ms) {
    return MouseMoveSample{.screen = {x < 0 ? x + kWrap : x, y < 0 ? y + kWrap : y},
                           .time_ms = time_ms};
}

[[nodiscard]] MouseMoveSample Signed(int32_t x, int32_t y, uint32_t time_ms) {
    return MouseMoveSample{.screen = {x, y}, .time_ms = time_ms};
}

} // namespace

TEST(mouse_move_history, UnwrapRestoresNegativeCoordinates) {
    EXPECT_EQ(Unwrap_mouse_history_coordinate(63616), -1920);
    EXPECT_EQ(Unwrap_mouse_history_coordinate(65535), -1);
    EXPECT_EQ(Unwrap_mouse_history_coordinate(0), 0);
    EXPECT_EQ(Unwrap_mouse_history_coordinate(137), 137);
    EXPECT_EQ(Unwrap_mouse_history_coordinate(32767), 32767);
}

TEST(mouse_move_history, TickComparisonSurvivesWrap) {
    EXPECT_TRUE(Is_tick_newer(101, 100));
    EXPECT_FALSE(Is_tick_newer(100, 100));
    EXPECT_FALSE(Is_tick_newer(99, 100));
    EXPECT_TRUE(Is_tick_newer(5, 0xFFFFFFF0u));
    EXPECT_FALSE(Is_tick_newer(0xFFFFFFF0u, 5));
}

TEST(mouse_move_history, ReturnsNewerPointsOldestFirstInClientPixels) {
    // Newest first, as GetMouseMovePointsEx fills the buffer. The laptop's top-left
    // corner (-1920, 137) maps to client (0, 137).
    std::vector<MouseMoveSample> const history = {
        Raw(-1900, 140, 13), Raw(-1910, 139, 12), Raw(-1915, 138, 11),
        Raw(-1920, 137, 10), Raw(-1925, 136, 9),
    };
    MouseHistoryResult const result =
        Collect_new_mouse_points(history, Signed(-1920, 137, 10), kOrigin);
    EXPECT_EQ(result.points_client,
              (std::vector<PointPx>{{5, 138}, {10, 139}, {20, 140}}));
    EXPECT_EQ(result.newest, Signed(-1900, 140, 13));
    EXPECT_FALSE(result.gap);
}

TEST(mouse_move_history, NothingNewKeepsLastConsumed) {
    std::vector<MouseMoveSample> const history = {Raw(10, 10, 5), Raw(9, 10, 4)};
    MouseHistoryResult const result =
        Collect_new_mouse_points(history, Signed(10, 10, 5), kOrigin);
    EXPECT_TRUE(result.points_client.empty());
    EXPECT_EQ(result.newest, Signed(10, 10, 5));
    EXPECT_FALSE(result.gap);

    MouseHistoryResult const empty = Collect_new_mouse_points({}, Signed(1, 2, 3), {});
    EXPECT_TRUE(empty.points_client.empty());
    EXPECT_EQ(empty.newest, Signed(1, 2, 3));
    EXPECT_FALSE(empty.gap);
}

TEST(mouse_move_history, StationaryClickNeverReplaysMovementBeforeThePress) {
    // The press at t=50 added no history entry. Entries up to t=40 are the approach
    // path and must not enter the stroke.
    std::vector<MouseMoveSample> const history = {
        Raw(30, 30, 70), Raw(20, 20, 60), Raw(5, 5, 40), Raw(4, 4, 30), Raw(3, 3, 20),
    };
    MouseHistoryResult const result =
        Collect_new_mouse_points(history, Signed(5, 5, 50), {});
    EXPECT_EQ(result.points_client, (std::vector<PointPx>{{20, 20}, {30, 30}}));
    EXPECT_FALSE(result.gap);
}

TEST(mouse_move_history, SameTickDifferentPositionsStayInOrder) {
    std::vector<MouseMoveSample> const history = {
        Raw(4, 0, 11), Raw(3, 0, 11), Raw(2, 0, 11), Raw(1, 0, 10), Raw(0, 0, 10),
    };
    MouseHistoryResult const result =
        Collect_new_mouse_points(history, Signed(1, 0, 10), {});
    EXPECT_EQ(result.points_client, (std::vector<PointPx>{{2, 0}, {3, 0}, {4, 0}}));
}

TEST(mouse_move_history, DropsRepeatedPositions) {
    std::vector<MouseMoveSample> const history = {
        Raw(3, 0, 15), Raw(2, 0, 14), Raw(2, 0, 13), Raw(2, 0, 13),
        Raw(1, 0, 12), Raw(1, 0, 11), Raw(1, 0, 10),
    };
    MouseHistoryResult const result =
        Collect_new_mouse_points(history, Signed(1, 0, 10), {});
    EXPECT_EQ(result.points_client, (std::vector<PointPx>{{2, 0}, {3, 0}}));
}

TEST(mouse_move_history, FullBufferNewerThanLastConsumedReportsGap) {
    std::vector<MouseMoveSample> history;
    for (uint32_t index = 0; index < kMouseMoveHistoryCapacity; ++index) {
        uint32_t const time_ms = 1000 - index;
        history.push_back(Raw(static_cast<int32_t>(time_ms), 0, time_ms));
    }
    MouseHistoryResult const result =
        Collect_new_mouse_points(history, Signed(0, 0, 100), {});
    EXPECT_TRUE(result.gap);
    ASSERT_EQ(result.points_client.size(), kMouseMoveHistoryCapacity);
    EXPECT_EQ(result.points_client.front().x, 1000 - 63);
    EXPECT_EQ(result.points_client.back().x, 1000);

    // The same buffer with the last consumed sample inside it has no gap.
    MouseHistoryResult const matched =
        Collect_new_mouse_points(history, Signed(990, 0, 990), {});
    EXPECT_FALSE(matched.gap);
    EXPECT_EQ(matched.points_client.size(), 10u);
}

TEST(mouse_move_history, TickWrapBetweenSamples) {
    std::vector<MouseMoveSample> const history = {
        Raw(3, 0, 2),
        Raw(2, 0, 0),
        Raw(1, 0, 0xFFFFFFFEu),
        Raw(0, 0, 0xFFFFFFF0u),
    };
    // Exact match before the wrap.
    EXPECT_EQ(
        Collect_new_mouse_points(history, Signed(1, 0, 0xFFFFFFFEu), {}).points_client,
        (std::vector<PointPx>{{2, 0}, {3, 0}}));
    // No match: only samples newer across the wrap.
    EXPECT_EQ(
        Collect_new_mouse_points(history, Signed(9, 9, 0xFFFFFFFFu), {}).points_client,
        (std::vector<PointPx>{{2, 0}, {3, 0}}));
}
