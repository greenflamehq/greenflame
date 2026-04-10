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
    config.obfuscate_block_size = 0;
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
    EXPECT_EQ(config.obfuscate_block_size, 1);
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
    config.obfuscate_block_size = 500;
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
    EXPECT_EQ(config.obfuscate_block_size, 50);
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

TEST(app_config, Defaults_UseFreehandSmoothingSmooth) {
    AppConfig const config{};

    EXPECT_EQ(config.brush_smoothing_mode, FreehandSmoothingMode::Smooth);
    EXPECT_EQ(config.highlighter_smoothing_mode, FreehandSmoothingMode::Smooth);
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

TEST(app_config, Normalize_ClampsObfuscateBlockSize) {
    AppConfig config{};
    config.obfuscate_block_size = 0;
    config.Normalize();
    EXPECT_EQ(config.obfuscate_block_size, 1);

    config.obfuscate_block_size = 51;
    config.Normalize();
    EXPECT_EQ(config.obfuscate_block_size, 50);
}

TEST(app_config, Defaults_DoNotAcknowledgeObfuscateRisk) {
    AppConfig const config{};

    EXPECT_FALSE(config.obfuscate_risk_acknowledged);
}

TEST(app_config, Defaults_DoNotIncludeCapturedCursor) {
    AppConfig const config{};

    EXPECT_FALSE(config.include_cursor);
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
    if (!config.has_value()) {
        return;
    }
    AppConfig const &config_value = config.value();
    EXPECT_EQ(config_value.brush_size, AppConfig::kDefaultBrushSize);
    EXPECT_EQ(config_value.default_save_format, L"");
    EXPECT_EQ(config_value.highlighter_opacity_percent, kDefaultHighlighterOpacityPercent);
}

TEST(app_config_json, Parse_AcceptsSchemaPropertyAndValidValues) {
    std::optional<AppConfig> const config = Parse_app_config_json(R"json(
{
  "$schema": "https://example.com/greenflame.config.schema.json",
  "capture": { "include_cursor": true },
  "tools": {
    "brush": { "size": 5, "smoothing_mode": "off" },
    "colors": { "4": "#ff00ff" },
    "font": { "sans": "Arial" },
    "highlighter": {
      "current_color": 5,
      "opacity_percent": 90,
      "smoothing_mode": "smooth"
    },
    "obfuscate": { "block_size": 13, "risk_acknowledged": true },
    "text": { "current_font": "mono" }
  },
  "save": {
    "default_save_format": "jpg",
    "padding_color": "#112233"
  }
}
)json");

    ASSERT_TRUE(config.has_value());
    if (!config.has_value()) {
        return;
    }
    AppConfig const &config_value = config.value();
    EXPECT_TRUE(config_value.include_cursor);
    EXPECT_EQ(config_value.brush_size, 5);
    EXPECT_EQ(config_value.brush_smoothing_mode, FreehandSmoothingMode::Off);
    EXPECT_EQ(config_value.annotation_colors[4], Make_colorref(0xFF, 0x00, 0xFF));
    EXPECT_EQ(config_value.text_font_sans, L"Arial");
    EXPECT_EQ(config_value.current_highlighter_color_index, 5);
    EXPECT_EQ(config_value.highlighter_opacity_percent, 90);
    EXPECT_EQ(config_value.highlighter_smoothing_mode, FreehandSmoothingMode::Smooth);
    EXPECT_EQ(config_value.obfuscate_block_size, 13);
    EXPECT_TRUE(config_value.obfuscate_risk_acknowledged);
    EXPECT_EQ(config_value.text_current_font, TextFontChoice::Mono);
    EXPECT_EQ(config_value.default_save_format, L"jpg");
    EXPECT_EQ(config_value.padding_color, Make_colorref(0x11, 0x22, 0x33));
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
    if (!config.has_value()) {
        return;
    }
    EXPECT_EQ(config.value().default_save_dir, L"C:\\captures\\greenflame");
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
    if (!config.has_value()) {
        return;
    }
    EXPECT_EQ(config.value().text_font_sans, L"Arial\\u0020Bold");
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

TEST(app_config_json, Serialize_WritesObfuscateBlockSizeWhenNonDefault) {
    AppConfig config{};
    config.obfuscate_block_size = 17;

    std::string const serialized = Serialize_app_config_json(config);
    std::optional<AppConfig> const round_tripped = Parse_app_config_json(serialized);

    EXPECT_NE(serialized.find(R"json("obfuscate")json"), std::string::npos);
    EXPECT_NE(serialized.find(R"json("block_size")json"), std::string::npos);
    ASSERT_TRUE(round_tripped.has_value());
    if (!round_tripped.has_value()) {
        return;
    }
    EXPECT_EQ(round_tripped.value().obfuscate_block_size, 17);
}

TEST(app_config_json, Serialize_WritesFreehandSmoothingModesWhenNonDefault) {
    AppConfig config{};
    config.brush_smoothing_mode = FreehandSmoothingMode::Off;
    config.highlighter_smoothing_mode = FreehandSmoothingMode::Off;

    std::string const serialized = Serialize_app_config_json(config);
    std::optional<AppConfig> const round_tripped = Parse_app_config_json(serialized);

    EXPECT_NE(serialized.find(R"json("smoothing_mode")json"), std::string::npos);
    ASSERT_TRUE(round_tripped.has_value());
    if (!round_tripped.has_value()) {
        return;
    }
    AppConfig const &round_tripped_value = round_tripped.value();
    EXPECT_EQ(round_tripped_value.brush_smoothing_mode, FreehandSmoothingMode::Off);
    EXPECT_EQ(round_tripped_value.highlighter_smoothing_mode, FreehandSmoothingMode::Off);
}

TEST(app_config_json, Serialize_WritesObfuscateRiskAcknowledgedWhenTrue) {
    AppConfig config{};
    config.obfuscate_risk_acknowledged = true;

    std::string const serialized = Serialize_app_config_json(config);
    std::optional<AppConfig> const round_tripped = Parse_app_config_json(serialized);

    EXPECT_NE(serialized.find(R"json("obfuscate")json"), std::string::npos);
    EXPECT_NE(serialized.find(R"json("risk_acknowledged")json"), std::string::npos);
    ASSERT_TRUE(round_tripped.has_value());
    if (!round_tripped.has_value()) {
        return;
    }
    EXPECT_TRUE(round_tripped.value().obfuscate_risk_acknowledged);
}

TEST(app_config_json, Serialize_WritesIncludeCursorWhenTrue) {
    AppConfig config{};
    config.include_cursor = true;

    std::string const serialized = Serialize_app_config_json(config);
    std::optional<AppConfig> const round_tripped = Parse_app_config_json(serialized);

    EXPECT_NE(serialized.find(R"json("capture")json"), std::string::npos);
    EXPECT_NE(serialized.find(R"json("include_cursor")json"), std::string::npos);
    ASSERT_TRUE(round_tripped.has_value());
    if (!round_tripped.has_value()) {
        return;
    }
    EXPECT_TRUE(round_tripped.value().include_cursor);
}

TEST(app_config_json, Serialize_OmitsIncludeCursorWhenFalse) {
    AppConfig config{};

    std::string const serialized = Serialize_app_config_json(config);

    EXPECT_EQ(serialized.find(R"json("include_cursor")json"), std::string::npos);
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

TEST(app_config_json, Parse_RejectsUnknownFreehandSmoothingMode) {
    EXPECT_FALSE(Parse_app_config_json(
                     R"json({"tools":{"brush":{"smoothing_mode":"arcane"}}})json")
                     .has_value());
}

TEST(app_config_json, Parse_RejectsObfuscateSizeAlias) {
    EXPECT_FALSE(Parse_app_config_json(R"json({"tools":{"obfuscate":{"size":5}}})json")
                     .has_value());
}

TEST(app_config_json, Parse_RejectsNonBooleanObfuscateRiskAcknowledged) {
    EXPECT_FALSE(Parse_app_config_json(
                     R"json({"tools":{"obfuscate":{"risk_acknowledged":"yes"}}})json")
                     .has_value());
}

TEST(app_config_json, Parse_RejectsNonBooleanCaptureIncludeCursor) {
    EXPECT_FALSE(
        Parse_app_config_json(R"json({"capture":{"include_cursor":"yes"}})json")
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
    if (!result.diagnostic.has_value()) {
        return;
    }
    AppConfigDiagnostic const &diagnostic = result.diagnostic.value();
    EXPECT_EQ(diagnostic.kind, AppConfigDiagnosticKind::Parse);
    EXPECT_TRUE(diagnostic.line.has_value());
    EXPECT_TRUE(diagnostic.column.has_value());
    EXPECT_THAT(diagnostic.message,
                testing::HasSubstr(L"Expected ',' or '}' after an object member."));
    EXPECT_FALSE(result.config.show_balloons);
    EXPECT_EQ(result.config.brush_size, AppConfig::kDefaultBrushSize);
}

TEST(app_config_json, ParseWithDiagnostics_SchemaErrorLoadsOtherValidValues) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"ui":{"show_balloons":false},"tools":{"brush":{"size":"5"}},"save":{"default_save_format":"png"}})json");

    ASSERT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    if (!result.diagnostic.has_value()) {
        return;
    }
    AppConfigDiagnostic const &diagnostic = result.diagnostic.value();
    EXPECT_EQ(diagnostic.kind, AppConfigDiagnosticKind::Schema);
    EXPECT_THAT(diagnostic.message, testing::HasSubstr(L"tools.brush.size"));
    EXPECT_FALSE(result.config.show_balloons);
    EXPECT_EQ(result.config.brush_size, AppConfig::kDefaultBrushSize);
    EXPECT_EQ(result.config.default_save_format, L"png");
}

TEST(app_config_json, Parse_SpellCheckLanguages_SingleLanguage) {
    std::optional<AppConfig> const config = Parse_app_config_json(
        R"json({"tools":{"text":{"spell_check_languages":["en-US"]}}})json");

    ASSERT_TRUE(config.has_value());
    if (!config.has_value()) {
        return;
    }
    AppConfig const &config_value = config.value();
    ASSERT_EQ(config_value.spell_check_languages.size(), 1u);
    EXPECT_EQ(config_value.spell_check_languages[0], L"en-US");
}

TEST(app_config_json, Parse_SpellCheckLanguages_MultipleLanguages) {
    std::optional<AppConfig> const config = Parse_app_config_json(
        R"json({"tools":{"text":{"spell_check_languages":["en-US","fr-CA"]}}})json");

    ASSERT_TRUE(config.has_value());
    if (!config.has_value()) {
        return;
    }
    AppConfig const &config_value = config.value();
    ASSERT_EQ(config_value.spell_check_languages.size(), 2u);
    EXPECT_EQ(config_value.spell_check_languages[0], L"en-US");
    EXPECT_EQ(config_value.spell_check_languages[1], L"fr-CA");
}

TEST(app_config_json, Parse_SpellCheckLanguages_EmptyArray_YieldsEmptyVector) {
    std::optional<AppConfig> const config = Parse_app_config_json(
        R"json({"tools":{"text":{"spell_check_languages":[]}}})json");

    ASSERT_TRUE(config.has_value());
    if (!config.has_value()) {
        return;
    }
    EXPECT_TRUE(config.value().spell_check_languages.empty());
}

TEST(app_config_json, Parse_SpellCheckLanguages_NonArray_ReportsSchemaError) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"tools":{"text":{"spell_check_languages":"en-US"}}})json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    if (!result.diagnostic.has_value()) {
        return;
    }
    EXPECT_THAT(result.diagnostic.value().message,
                testing::HasSubstr(L"spell_check_languages"));
}

TEST(app_config_json, Parse_SpellCheckLanguages_NonStringElement_ReportsSchemaError) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"tools":{"text":{"spell_check_languages":[42]}}})json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    if (!result.diagnostic.has_value()) {
        return;
    }
    EXPECT_THAT(result.diagnostic.value().message,
                testing::HasSubstr(L"spell_check_languages"));
}

TEST(app_config_json, Parse_SpellCheckLanguages_TooManyEntries_ReportsSchemaError) {
    // 9 entries exceeds the max of 8.
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"tools":{"text":{"spell_check_languages":["a","b","c","d","e","f","g","h","i"]}}})json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    if (!result.diagnostic.has_value()) {
        return;
    }
    EXPECT_THAT(result.diagnostic.value().message,
                testing::HasSubstr(L"spell_check_languages"));
}

TEST(app_config_json, Serialize_SpellCheckLanguages_RoundTrip) {
    AppConfig config;
    config.spell_check_languages = {L"en-US", L"fr-CA"};
    std::string const json = Serialize_app_config_json(config);
    std::optional<AppConfig> const parsed = Parse_app_config_json(json);

    ASSERT_TRUE(parsed.has_value());
    if (!parsed.has_value()) {
        return;
    }
    AppConfig const &parsed_value = parsed.value();
    ASSERT_EQ(parsed_value.spell_check_languages.size(), 2u);
    EXPECT_EQ(parsed_value.spell_check_languages[0], L"en-US");
    EXPECT_EQ(parsed_value.spell_check_languages[1], L"fr-CA");
}

TEST(app_config_json, Serialize_SpellCheckLanguages_EmptyVector_NotWritten) {
    AppConfig config;
    // Default: empty — key must not appear in output.
    std::string const json = Serialize_app_config_json(config);
    EXPECT_EQ(json.find("spell_check_languages"), std::string::npos);
}

TEST(app_config, Normalize_TruncatesPathsAndPatternsAndMasksPaddingColor) {
    AppConfig config{};
    config.default_save_dir.assign(300, L'a');
    config.last_save_as_dir.assign(300, L'b');
    config.filename_pattern_region.assign(300, L'c');
    config.filename_pattern_desktop.assign(300, L'd');
    config.filename_pattern_monitor.assign(300, L'e');
    config.filename_pattern_window.assign(300, L'f');
    config.padding_color = static_cast<COLORREF>(0xFF123456u);

    config.Normalize();

    EXPECT_EQ(config.default_save_dir.size(), 259u);
    EXPECT_EQ(config.last_save_as_dir.size(), 259u);
    EXPECT_EQ(config.filename_pattern_region.size(), 256u);
    EXPECT_EQ(config.filename_pattern_desktop.size(), 256u);
    EXPECT_EQ(config.filename_pattern_monitor.size(), 256u);
    EXPECT_EQ(config.filename_pattern_window.size(), 256u);
    EXPECT_EQ(config.padding_color, Make_colorref(0x56, 0x34, 0x12));
}

TEST(app_config, Normalize_NormalizesDefaultSaveFormatCaseWhitespaceAndJpegAlias) {
    AppConfig config{};

    config.default_save_format = L"  JpEg  ";
    config.Normalize();
    EXPECT_EQ(config.default_save_format, L"jpg");

    config.default_save_format = L"  PnG ";
    config.Normalize();
    EXPECT_EQ(config.default_save_format, L"png");

    config.default_save_format = L" BMP\t";
    config.Normalize();
    EXPECT_EQ(config.default_save_format, L"bmp");
}

TEST(app_config, Normalize_ClearsUnsupportedDefaultSaveFormat) {
    AppConfig config{};
    config.default_save_format = L" gif ";

    config.Normalize();

    EXPECT_TRUE(config.default_save_format.empty());
}

TEST(app_config_json, Parse_RejectsLineToolWhenNotObject) {
    EXPECT_FALSE(Parse_app_config_json(R"json({"tools":{"line":5}})json").has_value());
}

TEST(app_config_json, Parse_RejectsUnknownAnnotationColorSlot) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"tools":{"colors":{"8":"#112233"}}})json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    EXPECT_THAT(result.diagnostic->message, testing::HasSubstr(L"tools.colors.8"));
}

TEST(app_config_json, Parse_RejectsUnknownHighlighterColorSlot) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"tools":{"highlighter":{"colors":{"6":"#112233"}}}})json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    EXPECT_THAT(result.diagnostic->message,
                testing::HasSubstr(L"tools.highlighter.colors.6"));
}

TEST(app_config_json, Parse_RejectsInvalidTextCurrentFontToken) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"tools":{"text":{"current_font":"script"}}})json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    EXPECT_THAT(result.diagnostic->message, testing::HasSubstr(L"current_font"));
}

TEST(app_config_json, Serialize_WritesSizeOnlyToolsAndFontTokensWhenNonDefault) {
    AppConfig config{};
    config.line_size = 7;
    config.arrow_size = 8;
    config.rect_size = 9;
    config.ellipse_size = 10;
    config.text_current_font = TextFontChoice::Mono;
    config.bubble_current_font = TextFontChoice::Art;

    std::string const json = Serialize_app_config_json(config);
    std::optional<AppConfig> const parsed = Parse_app_config_json(json);

    EXPECT_NE(json.find(R"json("line")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("arrow")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("rect")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("ellipse")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("current_font")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("mono")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("art")json"), std::string::npos);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->line_size, 7);
    EXPECT_EQ(parsed->arrow_size, 8);
    EXPECT_EQ(parsed->rect_size, 9);
    EXPECT_EQ(parsed->ellipse_size, 10);
    EXPECT_EQ(parsed->text_current_font, TextFontChoice::Mono);
    EXPECT_EQ(parsed->bubble_current_font, TextFontChoice::Art);
}

TEST(app_config_json, Serialize_WritesFontFamiliesAndSaveFieldsWhenNonDefault) {
    AppConfig config{};
    config.text_font_sans = L"Aptos";
    config.text_font_serif = L"Georgia";
    config.text_font_mono = L"Consolas";
    config.text_font_art = L"Impact";
    config.default_save_dir = L"C:\\captures";
    config.last_save_as_dir = L"C:\\exports";
    config.filename_pattern_region = L"region_%Y%m%d";
    config.filename_pattern_desktop = L"desktop_%H%M";
    config.filename_pattern_monitor = L"monitor_%N";
    config.filename_pattern_window = L"window_%T";
    config.default_save_format = L"png";

    std::string const json = Serialize_app_config_json(config);
    std::optional<AppConfig> const parsed = Parse_app_config_json(json);

    EXPECT_NE(json.find(R"json("font")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("default_save_dir")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("last_save_as_dir")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("filename_pattern_window")json"), std::string::npos);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->text_font_sans, L"Aptos");
    EXPECT_EQ(parsed->text_font_serif, L"Georgia");
    EXPECT_EQ(parsed->text_font_mono, L"Consolas");
    EXPECT_EQ(parsed->text_font_art, L"Impact");
    EXPECT_EQ(parsed->default_save_dir, L"C:\\captures");
    EXPECT_EQ(parsed->last_save_as_dir, L"C:\\exports");
    EXPECT_EQ(parsed->filename_pattern_region, L"region_%Y%m%d");
    EXPECT_EQ(parsed->filename_pattern_desktop, L"desktop_%H%M");
    EXPECT_EQ(parsed->filename_pattern_monitor, L"monitor_%N");
    EXPECT_EQ(parsed->filename_pattern_window, L"window_%T");
    EXPECT_EQ(parsed->default_save_format, L"png");
}

TEST(app_config_json, Serialize_WritesColorMapsAndPaddingColorAsHex) {
    AppConfig config{};
    config.annotation_colors[1] = Make_colorref(0x12, 0x34, 0x56);
    config.highlighter_colors[2] = Make_colorref(0x89, 0xab, 0xcd);
    config.current_annotation_color_index = 1;
    config.current_highlighter_color_index = 2;
    config.padding_color = Make_colorref(0x44, 0x55, 0x66);

    std::string const json = Serialize_app_config_json(config);
    std::optional<AppConfig> const parsed = Parse_app_config_json(json);

    EXPECT_NE(json.find(R"json("#123456")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("#89abcd")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("#445566")json"), std::string::npos);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->annotation_colors[1], Make_colorref(0x12, 0x34, 0x56));
    EXPECT_EQ(parsed->highlighter_colors[2], Make_colorref(0x89, 0xab, 0xcd));
    EXPECT_EQ(parsed->current_annotation_color_index, 1);
    EXPECT_EQ(parsed->current_highlighter_color_index, 2);
    EXPECT_EQ(parsed->padding_color, Make_colorref(0x44, 0x55, 0x66));
}

TEST(app_config_json, Parse_AcceptsUppercaseHexColors) {
    std::optional<AppConfig> const config = Parse_app_config_json(R"json(
{
  "tools": {
    "colors": { "1": "#AaBbCc" },
    "highlighter": { "colors": { "2": "#DDEEFF" } }
  },
  "save": { "padding_color": "#0A0B0C" }
}
)json");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->annotation_colors[1], Make_colorref(0xAA, 0xBB, 0xCC));
    EXPECT_EQ(config->highlighter_colors[2], Make_colorref(0xDD, 0xEE, 0xFF));
    EXPECT_EQ(config->padding_color, Make_colorref(0x0A, 0x0B, 0x0C));
}

TEST(app_config_json, ParseWithDiagnostics_TopLevelNullAndSchemaTypeReportErrors) {
    {
        AppConfigParseResult const result =
            Parse_app_config_json_with_diagnostics("null");

        EXPECT_TRUE(result.Has_error());
        ASSERT_TRUE(result.diagnostic.has_value());
        EXPECT_THAT(result.diagnostic->message,
                    testing::HasSubstr(L"Top-level JSON value must be an object."));
    }

    {
        AppConfigParseResult const result =
            Parse_app_config_json_with_diagnostics(R"json({"$schema":1})json");

        EXPECT_TRUE(result.Has_error());
        ASSERT_TRUE(result.diagnostic.has_value());
        EXPECT_THAT(result.diagnostic->message, testing::HasSubstr(L"$schema"));
    }
}

TEST(app_config_json, ParseWithDiagnostics_RejectsMalformedSectionObjectTypes) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(R"json(
{
  "capture": 0,
  "ui": 0,
  "tools": {
    "brush": 0,
    "line": 0,
    "font": 0,
    "highlighter": 0,
    "text": 0,
    "bubble": 0,
    "obfuscate": 0,
    "colors": []
  },
  "save": 0
}
)json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
}

TEST(app_config_json, ParseWithDiagnostics_RejectsColorSlotValueTypesAndSpellCheckUtf8Issues) {
    {
        AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
            R"json({"tools":{"colors":{"0":7},"highlighter":{"colors":{"0":"#12zz34"}}}})json");

        EXPECT_TRUE(result.Has_error());
        ASSERT_TRUE(result.diagnostic.has_value());
        EXPECT_THAT(result.diagnostic->message,
                    testing::HasSubstr(L"tools.colors.0"));
    }

    {
        std::string const long_tag(65u, 'a');
        std::string const json =
            "{\"tools\":{\"text\":{\"spell_check_languages\":[\"" + long_tag +
            "\"]}}}";

        AppConfigParseResult const result =
            Parse_app_config_json_with_diagnostics(json);

        EXPECT_TRUE(result.Has_error());
        ASSERT_TRUE(result.diagnostic.has_value());
        EXPECT_THAT(result.diagnostic->message,
                    testing::HasSubstr(L"spell_check_languages"));
    }

    {
        std::string json = "{\"tools\":{\"text\":{\"spell_check_languages\":[\"";
        json.push_back(static_cast<char>(0x80));
        json += "\"]}}}";

        AppConfigParseResult const result =
            Parse_app_config_json_with_diagnostics(json);

        EXPECT_TRUE(result.Has_error());
        ASSERT_TRUE(result.diagnostic.has_value());
        EXPECT_THAT(result.diagnostic->message,
                    testing::HasSubstr(L"spell_check_languages"));
    }
}

TEST(app_config_json, ParseWithDiagnostics_RejectsNonStringSaveFields) {
    AppConfigParseResult const result = Parse_app_config_json_with_diagnostics(
        R"json({"save":{"default_save_format":5,"padding_color":5}})json");

    EXPECT_TRUE(result.Has_error());
    ASSERT_TRUE(result.diagnostic.has_value());
    EXPECT_THAT(result.diagnostic->message,
                testing::HasSubstr(L"default_save_format"));
}

TEST(app_config_json, Serialize_WritesUiAndToolSizeOverridesWhenNonDefault) {
    AppConfig config{};
    config.show_balloons = false;
    config.show_selection_size_side_labels = false;
    config.show_selection_size_center_label = false;
    config.tool_size_overlay_duration_ms = 750;
    config.brush_size = 7;
    config.highlighter_size = 8;
    config.highlighter_opacity_percent = 91;
    config.highlighter_pause_straighten_ms = 500;
    config.highlighter_pause_straighten_deadzone_px = 11;
    config.text_size = 9;
    config.bubble_size = 11;

    std::string const json = Serialize_app_config_json(config);
    std::optional<AppConfig> const parsed = Parse_app_config_json(json);

    EXPECT_NE(json.find(R"json("ui")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("show_balloons")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("brush")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("highlighter")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("text")json"), std::string::npos);
    EXPECT_NE(json.find(R"json("bubble")json"), std::string::npos);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->show_balloons);
    EXPECT_FALSE(parsed->show_selection_size_side_labels);
    EXPECT_FALSE(parsed->show_selection_size_center_label);
    EXPECT_EQ(parsed->tool_size_overlay_duration_ms, 750);
    EXPECT_EQ(parsed->brush_size, 7);
    EXPECT_EQ(parsed->highlighter_size, 8);
    EXPECT_EQ(parsed->highlighter_opacity_percent, 91);
    EXPECT_EQ(parsed->highlighter_pause_straighten_ms, 500);
    EXPECT_EQ(parsed->highlighter_pause_straighten_deadzone_px, 11);
    EXPECT_EQ(parsed->text_size, 9);
    EXPECT_EQ(parsed->bubble_size, 11);
}
