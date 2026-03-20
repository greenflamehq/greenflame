#include "greenflame_core/app_config.h"

using namespace greenflame::core;

TEST(app_config, Normalize_ClampsToolSizesToMinimum) {
    AppConfig config{};
    config.brush_size = 0;
    config.line_size = 0;
    config.arrow_size = 0;
    config.rect_size = 0;
    config.highlighter_size = 0;
    config.bubble_size = 0;
    config.text_size = 0;
    config.current_annotation_color_index = -4;
    config.current_highlighter_color_index = -3;
    config.highlighter_opacity_percent = -10;
    config.tool_size_overlay_duration_ms = -25;

    config.Normalize();

    EXPECT_EQ(config.brush_size, 1);
    EXPECT_EQ(config.line_size, 1);
    EXPECT_EQ(config.arrow_size, 1);
    EXPECT_EQ(config.rect_size, 1);
    EXPECT_EQ(config.highlighter_size, 1);
    EXPECT_EQ(config.bubble_size, 1);
    EXPECT_EQ(config.text_size, 1);
    EXPECT_EQ(config.current_annotation_color_index, 0);
    EXPECT_EQ(config.current_highlighter_color_index, 0);
    EXPECT_EQ(config.highlighter_opacity_percent, 0);
    EXPECT_EQ(config.tool_size_overlay_duration_ms, 0);
}

TEST(app_config, Normalize_ClampsToolSizesToMaximum) {
    AppConfig config{};
    config.brush_size = 500;
    config.line_size = 500;
    config.arrow_size = 500;
    config.rect_size = 500;
    config.highlighter_size = 500;
    config.bubble_size = 500;
    config.text_size = 500;
    config.current_annotation_color_index = 400;
    config.current_highlighter_color_index = 400;
    config.highlighter_opacity_percent = 400;
    config.tool_size_overlay_duration_ms = 2500;

    config.Normalize();

    EXPECT_EQ(config.brush_size, 50);
    EXPECT_EQ(config.line_size, 50);
    EXPECT_EQ(config.arrow_size, 50);
    EXPECT_EQ(config.rect_size, 50);
    EXPECT_EQ(config.highlighter_size, 50);
    EXPECT_EQ(config.bubble_size, 50);
    EXPECT_EQ(config.text_size, 50);
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

TEST(app_config, Normalize_ClampsTextSizeStep) {
    AppConfig config{};
    config.text_size = 0;
    config.Normalize();
    EXPECT_EQ(config.text_size, 1);

    config.text_size = 51;
    config.Normalize();
    EXPECT_EQ(config.text_size, 50);
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

TEST(app_config, Normalize_ClampsHighlighterStraightenFieldsToNonNegative) {
    AppConfig config{};
    config.highlighter_pause_straighten_ms = -100;
    config.highlighter_pause_straighten_deadzone_px = -5;

    config.Normalize();

    EXPECT_EQ(config.highlighter_pause_straighten_ms, 0);
    EXPECT_EQ(config.highlighter_pause_straighten_deadzone_px, 0);
}
