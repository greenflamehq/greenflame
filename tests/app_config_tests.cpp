#include "greenflame_core/app_config.h"

using namespace greenflame::core;

TEST(app_config, Normalize_ClampsBrushWidthAndOverlayDuration) {
    AppConfig config{};
    config.brush_width_px = 0;
    config.current_annotation_color_index = -4;
    config.current_highlighter_color_index = -3;
    config.highlighter_opacity_percent = -10;
    config.tool_size_overlay_duration_ms = -25;

    config.Normalize();

    EXPECT_EQ(config.brush_width_px, 1);
    EXPECT_EQ(config.current_annotation_color_index, 0);
    EXPECT_EQ(config.current_highlighter_color_index, 0);
    EXPECT_EQ(config.highlighter_opacity_percent, 0);
    EXPECT_EQ(config.tool_size_overlay_duration_ms, 0);
}

TEST(app_config, Normalize_ClampsBrushWidthToMaximum) {
    AppConfig config{};
    config.brush_width_px = 500;
    config.current_annotation_color_index = 400;
    config.current_highlighter_color_index = 400;
    config.highlighter_opacity_percent = 400;
    config.tool_size_overlay_duration_ms = 2500;

    config.Normalize();

    EXPECT_EQ(config.brush_width_px, 50);
    EXPECT_EQ(config.current_annotation_color_index, 7);
    EXPECT_EQ(config.current_highlighter_color_index, 5);
    EXPECT_EQ(config.highlighter_opacity_percent, 100);
    EXPECT_EQ(config.tool_size_overlay_duration_ms, 2500);
}

TEST(app_config, Defaults_UseBlackFirstPaletteEntryAndCurrentSlotZero) {
    AppConfig const config{};

    EXPECT_EQ(config.annotation_colors, kDefaultAnnotationColorPalette);
    EXPECT_EQ(config.current_annotation_color_index, 0);
    EXPECT_EQ(config.highlighter_colors, kDefaultHighlighterColorPalette);
    EXPECT_EQ(config.current_highlighter_color_index, 0);
    EXPECT_EQ(config.highlighter_opacity_percent, kDefaultHighlighterOpacityPercent);
}

TEST(app_config, Normalize_ClampsTextSizeToNearestAllowedValue) {
    AppConfig config{};
    config.text_size_points = 13;

    config.Normalize();

    EXPECT_EQ(config.text_size_points, 12);
}

TEST(app_config, Normalize_ResetsInvalidCurrentTextFontChoice) {
    AppConfig config{};
    config.text_current_font = static_cast<TextFontChoice>(99);

    config.Normalize();

    EXPECT_EQ(config.text_current_font, TextFontChoice::Sans);
}

TEST(app_config, Normalize_UsesDefaultFontFamiliesWhenTrimmedValueIsEmpty) {
    AppConfig config{};
    config.text_font_sans = L"   ";
    config.text_font_serif = L"\t";
    config.text_font_mono = L"\r\n";
    config.text_font_art = L"  \t  ";

    config.Normalize();

    EXPECT_EQ(config.text_font_sans, L"Arial");
    EXPECT_EQ(config.text_font_serif, L"Times New Roman");
    EXPECT_EQ(config.text_font_mono, L"Courier New");
    EXPECT_EQ(config.text_font_art, L"Comic Sans MS");
}

TEST(app_config, Normalize_TruncatesTextFontFamiliesTo128CodeUnits) {
    AppConfig config{};
    config.text_font_sans.assign(140, L'a');

    config.Normalize();

    EXPECT_EQ(config.text_font_sans.size(), 128u);
}

TEST(app_config, Normalize_PreservesValidBubbleFontChoices) {
    for (TextFontChoice const choice : {TextFontChoice::Sans, TextFontChoice::Serif,
                                        TextFontChoice::Mono, TextFontChoice::Art}) {
        AppConfig config{};
        config.bubble_current_font = choice;
        config.Normalize();
        EXPECT_EQ(config.bubble_current_font, choice);
    }
}

TEST(app_config, Normalize_ResetsInvalidBubbleFontChoiceToSans) {
    AppConfig config{};
    config.bubble_current_font = static_cast<TextFontChoice>(99);

    config.Normalize();

    EXPECT_EQ(config.bubble_current_font, TextFontChoice::Sans);
}
