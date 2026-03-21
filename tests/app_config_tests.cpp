#include "greenflame_core/app_config.h"
#include "greenflame_core/app_config_json.h"

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
    EXPECT_EQ(config.padding_color, Make_colorref(0x00, 0x00, 0x00));
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

TEST(app_config, Normalize_PreservesDefaultPaddingColor) {
    AppConfig config{};

    config.Normalize();

    EXPECT_EQ(config.padding_color, Make_colorref(0x00, 0x00, 0x00));
}

TEST(app_config_json, Parse_AcceptsSparseEmptyRoot) {
    std::optional<AppConfig> const config = Parse_app_config_json("{}");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->brush_size, AppConfig::kDefaultBrushSize);
    EXPECT_EQ(config->default_save_format, L"");
    EXPECT_EQ(config->highlighter_opacity_percent, kDefaultHighlighterOpacityPercent);
}

TEST(app_config_json, Parse_AcceptsSchemaPropertyAndValidValues) {
    std::optional<AppConfig> const config = Parse_app_config_json(R"json(
{
  "$schema": "https://example.com/greenflame.config.schema.json",
  "tools": {
    "brush": { "size": 5 },
    "colors": { "4": "#ff00ff" },
    "font": { "sans": "Arial" },
    "highlighter": {
      "current_color": 5,
      "opacity_percent": 90
    },
    "text": { "current_font": "mono" }
  },
  "save": {
    "default_save_format": "jpg",
    "padding_color": "#112233"
  }
}
)json");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->brush_size, 5);
    EXPECT_EQ(config->annotation_colors[4], Make_colorref(0xFF, 0x00, 0xFF));
    EXPECT_EQ(config->text_font_sans, L"Arial");
    EXPECT_EQ(config->current_highlighter_color_index, 5);
    EXPECT_EQ(config->highlighter_opacity_percent, 90);
    EXPECT_EQ(config->text_current_font, TextFontChoice::Mono);
    EXPECT_EQ(config->default_save_format, L"jpg");
    EXPECT_EQ(config->padding_color, Make_colorref(0x11, 0x22, 0x33));
}

TEST(app_config_json, Parse_DecodesEscapedBackslashesInJsonStrings) {
    std::optional<AppConfig> const config = Parse_app_config_json(R"json(
{
  "save": {
    "default_save_dir": "C:\\captures\\greenflame"
  }
}
)json");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->default_save_dir, L"C:\\captures\\greenflame");
}

TEST(app_config_json, Parse_UnicodeEscapesInStringsArePassedThroughVerbatim) {
    // easyjson stores JSON unicode escapes (backslash-u + 4 hex digits) as
    // literal characters rather than decoding them to the actual code point.
    // The JSON escape for a space is "backslash u 0020", but after parsing it
    // appears in the config as those 6 literal characters, not as ' '.
    std::optional<AppConfig> const config = Parse_app_config_json(R"json(
{
  "tools": {
    "font": {
      "sans": "Arial\u0020Bold"
    }
  }
}
)json");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->text_font_sans, L"Arial\\u0020Bold");
}

TEST(app_config_json, Serialize_RoundTripsCanonicalSaveDirectoryStably) {
    std::string json = R"json(
{
  "save": {
    "default_save_dir": "C:\\captures\\greenflame"
  }
}
)json";
    std::string const expected = R"json({
  "save" : {
    "default_save_dir" : "C:\\captures\\greenflame"
  }
})json";

    std::string serialized = {};
    for (int iteration = 0; iteration < 5; ++iteration) {
        AppConfigParseResult const parsed =
            Parse_app_config_json_with_diagnostics(json);

        ASSERT_FALSE(parsed.Has_error());
        ASSERT_EQ(parsed.config.default_save_dir, L"C:\\captures\\greenflame");

        std::string const next = Serialize_app_config_json(parsed.config);
        if (iteration == 0) {
            EXPECT_EQ(next, expected);
        } else {
            EXPECT_EQ(next, serialized);
        }

        serialized = next;
        json = serialized;
    }
}

TEST(app_config_json, Parse_RejectsUnknownTopLevelKey) {
    EXPECT_FALSE(Parse_app_config_json(R"json({"extra":true})json").has_value());
}

TEST(app_config_json, Parse_RejectsStringToolSize) {
    EXPECT_FALSE(Parse_app_config_json(R"json({"tools":{"brush":{"size":"5"}}})json")
                     .has_value());
}

TEST(app_config_json, Parse_RejectsFloatingToolSize) {
    EXPECT_FALSE(Parse_app_config_json(R"json({"tools":{"brush":{"size":5.0}}})json")
                     .has_value());
}

TEST(app_config_json, Parse_RejectsColorArrays) {
    EXPECT_FALSE(Parse_app_config_json(R"json({"tools":{"colors":["#ff00ff"]}})json")
                     .has_value());
}

TEST(app_config_json, Parse_RejectsWhitespaceOnlyFontFamily) {
    EXPECT_FALSE(Parse_app_config_json(R"json({"tools":{"font":{"sans":"   "}}})json")
                     .has_value());
}

TEST(app_config_json, Parse_RejectsJpegAliasInSaveFormat) {
    EXPECT_FALSE(
        Parse_app_config_json(R"json({"save":{"default_save_format":"jpeg"}})json")
            .has_value());
}

TEST(app_config_json, ParseWithDiagnostics_ParseErrorReportsLocationAndKeepsPrefix) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(R"json(
{
  "ui": { "show_balloons": false }
  "tools": { "brush": { "size": 7 } }
}
)json");

    ASSERT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    EXPECT_EQ(result.diagnostic->kind, AppConfigDiagnosticKind::Parse);
    EXPECT_TRUE(result.diagnostic->line.has_value());
    EXPECT_TRUE(result.diagnostic->column.has_value());
    EXPECT_THAT(result.diagnostic->message,
                testing::HasSubstr(L"Expected ',' or '}' after an object member."));
    EXPECT_FALSE(result.config.show_balloons);
    EXPECT_EQ(result.config.brush_size, AppConfig::kDefaultBrushSize);
}

TEST(app_config_json, ParseWithDiagnostics_SchemaErrorLoadsOtherValidValues) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"ui":{"show_balloons":false},"tools":{"brush":{"size":"5"}},"save":{"default_save_format":"png"}})json");

    ASSERT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    EXPECT_EQ(result.diagnostic->kind, AppConfigDiagnosticKind::Schema);
    EXPECT_THAT(result.diagnostic->message, testing::HasSubstr(L"tools.brush.size"));
    EXPECT_FALSE(result.config.show_balloons);
    EXPECT_EQ(result.config.brush_size, AppConfig::kDefaultBrushSize);
    EXPECT_EQ(result.config.default_save_format, L"png");
}
