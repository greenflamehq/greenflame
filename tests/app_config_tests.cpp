#include "greenflame_core/app_config.h"

using namespace greenflame::core;

TEST(app_config, Normalize_ClampsBrushWidthAndOverlayDuration) {
    AppConfig config{};
    config.brush_width_px = 0;
    config.tool_size_overlay_duration_ms = -25;

    config.Normalize();

    EXPECT_EQ(config.brush_width_px, 1);
    EXPECT_EQ(config.tool_size_overlay_duration_ms, 0);
}

TEST(app_config, Normalize_ClampsBrushWidthToMaximum) {
    AppConfig config{};
    config.brush_width_px = 500;
    config.tool_size_overlay_duration_ms = 2500;

    config.Normalize();

    EXPECT_EQ(config.brush_width_px, 50);
    EXPECT_EQ(config.tool_size_overlay_duration_ms, 2500);
}
