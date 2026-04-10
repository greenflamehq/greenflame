#include "greenflame_core/app_config.h"
#include "greenflame_core/cli_annotation_import.h"
#include "greenflame_core/selection_wheel.h"

using namespace greenflame::core;

namespace {

[[nodiscard]] AppConfig Make_config() {
    AppConfig config{};
    config.Normalize();
    return config;
}

[[nodiscard]] CliAnnotationParseContext
Make_context(AppConfig const &config,
             RectPx capture_rect = RectPx::From_ltrb(100, 200, 300, 400),
             RectPx virtual_bounds = RectPx::From_ltrb(-500, 0, 1500, 900)) {
    CliAnnotationParseContext context{};
    context.capture_rect_screen = capture_rect;
    context.virtual_desktop_bounds = virtual_bounds;
    context.config = &config;
    context.target_kind = CliAnnotationTargetKind::Capture;
    return context;
}

void Expect_parse_error_contains(std::string_view json,
                                 CliAnnotationParseContext const &context,
                                 std::wstring_view expected_fragment) {
    CliAnnotationParseResult const result = Parse_cli_annotations_json(json, context);
    EXPECT_FALSE(result.ok) << result.error_message;
    EXPECT_NE(result.error_message.find(expected_fragment), std::wstring::npos)
        << result.error_message;
}

} // namespace

TEST(cli_annotation_import, classify_input_detects_inline_json_and_file_path) {
    EXPECT_EQ(Classify_cli_annotation_input(L"{\"annotations\":[]}"),
              CliAnnotationInputKind::InlineJson);
    EXPECT_EQ(Classify_cli_annotation_input(L"   {\"annotations\":[]}"),
              CliAnnotationInputKind::InlineJson);
    EXPECT_EQ(Classify_cli_annotation_input(L"annotations.json"),
              CliAnnotationInputKind::FilePath);
}

TEST(cli_annotation_import, parse_rejects_unknown_top_level_key) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[],"bogus":1})", Make_context(config));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"unknown property"), std::wstring::npos);
}

TEST(cli_annotation_import, parse_translates_local_line_into_capture_space) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"line","start":{"x":1,"y":2},"end":{"x":10,"y":20},"size":3,"color":"#112233"}]})",
        Make_context(config));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<LineAnnotation>(result.annotations[0].data));

    LineAnnotation const &line = std::get<LineAnnotation>(result.annotations[0].data);
    EXPECT_EQ(line.start, (PointPx{101, 202}));
    EXPECT_EQ(line.end, (PointPx{110, 220}));
    EXPECT_EQ(line.style.width_px, 3);
    EXPECT_EQ(line.style.color, Make_colorref(0x11, 0x22, 0x33));
}

TEST(cli_annotation_import, parse_translates_global_coordinates_from_virtual_desktop) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"coordinate_space":"global","annotations":[{"type":"bubble","center":{"x":10,"y":20},"size":5}]})",
        Make_context(config, RectPx::From_ltrb(100, 200, 300, 400),
                     RectPx::From_ltrb(-500, -200, 1500, 900)));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<BubbleAnnotation>(result.annotations[0].data));

    BubbleAnnotation const &bubble =
        std::get<BubbleAnnotation>(result.annotations[0].data);
    EXPECT_EQ(bubble.center, (PointPx{-490, -180}));
    EXPECT_EQ(bubble.diameter_px, 25);
    EXPECT_EQ(bubble.counter_value, 1);
}

TEST(cli_annotation_import, parse_rejects_global_coordinate_space_for_input_image) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext context = Make_context(config);
    context.target_kind = CliAnnotationTargetKind::InputImage;

    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"coordinate_space":"global","annotations":[]})", context);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"not supported with --input"),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_assigns_bubble_numbers_only_to_bubbles) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"bubble","center":{"x":0,"y":0},"size":5},{"type":"line","start":{"x":0,"y":0},"end":{"x":1,"y":1},"size":2},{"type":"bubble","center":{"x":5,"y":5},"size":6}]})",
        Make_context(config));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 3u);
    EXPECT_EQ(std::get<BubbleAnnotation>(result.annotations[0].data).counter_value, 1);
    EXPECT_EQ(std::get<BubbleAnnotation>(result.annotations[2].data).counter_value, 2);
}

TEST(cli_annotation_import, parse_translates_local_obfuscate_into_capture_space) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"obfuscate","left":5,"top":7,"width":30,"height":20,"size":6}]})",
        Make_context(config));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    ASSERT_TRUE(
        std::holds_alternative<ObfuscateAnnotation>(result.annotations[0].data));

    ObfuscateAnnotation const &obfuscate =
        std::get<ObfuscateAnnotation>(result.annotations[0].data);
    EXPECT_EQ(obfuscate.bounds, (RectPx::From_ltrb(105, 207, 135, 227)));
    EXPECT_EQ(obfuscate.block_size, 6);
    EXPECT_TRUE(obfuscate.premultiplied_bgra.empty());
}

TEST(cli_annotation_import,
     parse_preserves_alternating_annotations_and_obfuscates_in_paint_order) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"brush","size":5,"points":[{"x":12,"y":18},{"x":24,"y":26},{"x":36,"y":22}]},{"type":"obfuscate","left":10,"top":12,"width":32,"height":18,"size":8},{"type":"line","start":{"x":40,"y":44},"end":{"x":78,"y":60},"size":3},{"type":"obfuscate","left":34,"top":40,"width":28,"height":24,"size":1}]})",
        Make_context(config));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 4u);
    EXPECT_TRUE(
        std::holds_alternative<FreehandStrokeAnnotation>(result.annotations[0].data));
    EXPECT_TRUE(
        std::holds_alternative<ObfuscateAnnotation>(result.annotations[1].data));
    EXPECT_TRUE(std::holds_alternative<LineAnnotation>(result.annotations[2].data));
    EXPECT_TRUE(
        std::holds_alternative<ObfuscateAnnotation>(result.annotations[3].data));

    FreehandStrokeAnnotation const &brush =
        std::get<FreehandStrokeAnnotation>(result.annotations[0].data);
    EXPECT_EQ(brush.points.front(), (PointPx{112, 218}));
    EXPECT_EQ(brush.points.back(), (PointPx{136, 222}));

    ObfuscateAnnotation const &first_obfuscate =
        std::get<ObfuscateAnnotation>(result.annotations[1].data);
    EXPECT_EQ(first_obfuscate.bounds, (RectPx::From_ltrb(110, 212, 142, 230)));
    EXPECT_EQ(first_obfuscate.block_size, 8);

    LineAnnotation const &line = std::get<LineAnnotation>(result.annotations[2].data);
    EXPECT_EQ(line.start, (PointPx{140, 244}));
    EXPECT_EQ(line.end, (PointPx{178, 260}));

    ObfuscateAnnotation const &second_obfuscate =
        std::get<ObfuscateAnnotation>(result.annotations[3].data);
    EXPECT_EQ(second_obfuscate.bounds, (RectPx::From_ltrb(134, 240, 162, 264)));
    EXPECT_EQ(second_obfuscate.block_size, 1);
}

TEST(cli_annotation_import, parse_smooths_brush_point_lists_when_enabled) {
    AppConfig config = Make_config();
    config.brush_smoothing_mode = FreehandSmoothingMode::Smooth;

    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"brush","size":6,"points":[{"x":0,"y":0},{"x":10,"y":0},{"x":20,"y":10},{"x":30,"y":10}]}]})",
        Make_context(config));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);

    FreehandStrokeAnnotation const &brush =
        std::get<FreehandStrokeAnnotation>(result.annotations[0].data);
    ASSERT_GT(brush.points.size(), 4u);
    EXPECT_EQ(brush.points.front(), (PointPx{100, 200}));
    EXPECT_EQ(brush.points.back(), (PointPx{130, 210}));
}

TEST(cli_annotation_import,
     parse_keeps_two_point_highlighter_segments_unsmoothed_when_enabled) {
    AppConfig config = Make_config();
    config.highlighter_smoothing_mode = FreehandSmoothingMode::Smooth;

    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"highlighter","start":{"x":5,"y":6},"end":{"x":40,"y":20},"size":7}]})",
        Make_context(config));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);

    FreehandStrokeAnnotation const &highlighter =
        std::get<FreehandStrokeAnnotation>(result.annotations[0].data);
    ASSERT_EQ(highlighter.points.size(), 2u);
    EXPECT_EQ(highlighter.points[0], (PointPx{105, 206}));
    EXPECT_EQ(highlighter.points[1], (PointPx{140, 220}));
}

TEST(cli_annotation_import, parse_applies_document_text_defaults_and_merges_spans) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"color":"#445566","font":{"preset":"mono"},"annotations":[{"type":"text","origin":{"x":3,"y":4},"size":10,"spans":[{"text":"ab","bold":true},{"text":"cd","bold":true},{"text":"ef","italic":true}]}]})",
        Make_context(config));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<TextAnnotation>(result.annotations[0].data));

    TextAnnotation const &text = std::get<TextAnnotation>(result.annotations[0].data);
    EXPECT_EQ(text.origin, (PointPx{103, 204}));
    EXPECT_EQ(text.base_style.color, Make_colorref(0x44, 0x55, 0x66));
    EXPECT_EQ(text.base_style.font_choice, TextFontChoice::Mono);
    EXPECT_TRUE(text.base_style.font_family.empty());
    EXPECT_EQ(text.base_style.point_size, Text_point_size_from_step(10));
    ASSERT_EQ(text.runs.size(), 2u);
    EXPECT_EQ(text.runs[0].text, L"abcd");
    EXPECT_TRUE(text.runs[0].flags.bold);
    EXPECT_EQ(text.runs[1].text, L"ef");
    EXPECT_TRUE(text.runs[1].flags.italic);
}

TEST(cli_annotation_import, parse_rejects_span_unknown_property) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"size":10,"spans":[{"text":"abc","color":"#ffffff"}]}]})",
        Make_context(config));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"unknown property"), std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_filled_shape_size) {
    AppConfig const config = Make_config();
    // "size" is not in the allowed key set for filled shapes, so it is
    // rejected as an unknown property before the explicit filled-shape check.
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"filled_rectangle","left":1,"top":2,"width":10,"height":20,"size":2}]})",
        Make_context(config));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"unknown property"), std::wstring::npos);
}

TEST(cli_annotation_import, resolve_text_font_families_maps_config_fields_in_order) {
    AppConfig config = Make_config();
    config.text_font_sans = L"TestSans";
    config.text_font_serif = L"TestSerif";
    config.text_font_mono = L"TestMono";
    config.text_font_art = L"TestArt";

    std::array<std::wstring, 4> const families = Resolve_text_font_families(config);
    EXPECT_EQ(families[0], L"TestSans");
    EXPECT_EQ(families[1], L"TestSerif");
    EXPECT_EQ(families[2], L"TestMono");
    EXPECT_EQ(families[3], L"TestArt");
}

TEST(cli_annotation_import,
     parse_translates_global_coordinates_for_line_rect_ellipse_text) {
    AppConfig const config = Make_config();
    // virtual_bounds starts at (-100, -50); global point (5, 10) -> (-95, -40)
    CliAnnotationParseContext const context =
        Make_context(config, RectPx::From_ltrb(100, 200, 300, 400),
                     RectPx::From_ltrb(-100, -50, 1500, 900));

    {
        CliAnnotationParseResult const result = Parse_cli_annotations_json(
            R"({"coordinate_space":"global","annotations":[{"type":"line","start":{"x":5,"y":10},"end":{"x":15,"y":20},"size":2}]})",
            context);
        ASSERT_TRUE(result.ok);
        ASSERT_EQ(result.annotations.size(), 1u);
        LineAnnotation const &line =
            std::get<LineAnnotation>(result.annotations[0].data);
        EXPECT_EQ(line.start, (PointPx{-95, -40}));
        EXPECT_EQ(line.end, (PointPx{-85, -30}));
    }

    {
        CliAnnotationParseResult const result = Parse_cli_annotations_json(
            R"({"coordinate_space":"global","annotations":[{"type":"rectangle","left":5,"top":10,"width":50,"height":30,"size":2}]})",
            context);
        ASSERT_TRUE(result.ok);
        ASSERT_EQ(result.annotations.size(), 1u);
        RectangleAnnotation const &rect =
            std::get<RectangleAnnotation>(result.annotations[0].data);
        EXPECT_EQ(rect.outer_bounds, (RectPx::From_ltrb(-95, -40, -45, -10)));
    }

    {
        CliAnnotationParseResult const result = Parse_cli_annotations_json(
            R"({"coordinate_space":"global","annotations":[{"type":"ellipse","center":{"x":5,"y":10},"width":30,"height":20,"size":2}]})",
            context);
        ASSERT_TRUE(result.ok);
        ASSERT_EQ(result.annotations.size(), 1u);
        EllipseAnnotation const &ellipse =
            std::get<EllipseAnnotation>(result.annotations[0].data);
        EXPECT_EQ(ellipse.outer_bounds, (RectPx::From_ltrb(-110, -50, -80, -30)));
    }

    {
        CliAnnotationParseResult const result = Parse_cli_annotations_json(
            R"({"coordinate_space":"global","annotations":[{"type":"text","origin":{"x":5,"y":10},"text":"hello","size":10}]})",
            context);
        ASSERT_TRUE(result.ok);
        ASSERT_EQ(result.annotations.size(), 1u);
        TextAnnotation const &text =
            std::get<TextAnnotation>(result.annotations[0].data);
        EXPECT_EQ(text.origin, (PointPx{-95, -40}));
    }
}

TEST(cli_annotation_import, parse_rejects_highlighter_geometry_mix) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"highlighter","start":{"x":0,"y":0},"end":{"x":10,"y":10},"points":[{"x":1,"y":1},{"x":2,"y":2}],"size":4}]})",
        Make_context(config));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"exactly one geometry form"),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_trailing_characters_after_root_value) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[]}) trailing", Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"Unexpected trailing characters"),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_invalid_json_escape_sequence) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"bad\q","size":10}]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error_message.empty());
}

TEST(cli_annotation_import, parse_rejects_invalid_json_number_exponent) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result =
        Parse_cli_annotations_json(R"({"annotations":[1e]})", Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"Invalid JSON number."), std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_root_highlighter_opacity_out_of_range) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"highlighter_opacity_percent":101,"annotations":[]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"must be an integer in 0..100."),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_invalid_hex_color_digits) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"line","start":{"x":0,"y":0},"end":{"x":10,"y":20},"size":3,"color":"#12zz34"}]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"must be a #rrggbb color string."),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_font_objects_with_both_preset_and_family) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"abc","size":10,"font":{"preset":"mono","family":"Consolas"}}]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"exactly one of \"preset\" or \"family\""),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_font_objects_with_neither_preset_nor_family) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"bubble","center":{"x":0,"y":0},"size":5,"font":{}}]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"exactly one of \"preset\" or \"family\""),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_font_family_that_trims_empty) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"abc","size":10,"font":{"family":"   "}}]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"must not be empty."), std::wstring::npos);
}

TEST(cli_annotation_import, parse_normalizes_text_newlines_from_json_escapes) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"alpha\r\nbeta\rgamma","size":10}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    TextAnnotation const &text = std::get<TextAnnotation>(result.annotations[0].data);
    ASSERT_EQ(text.runs.size(), 1u);
    EXPECT_EQ(text.runs[0].text, L"alpha\nbeta\ngamma");
}

TEST(cli_annotation_import,
     parse_annotation_values_override_document_defaults_but_keep_document_preset) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"color":"#010203","font":{"preset":"mono"},"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"abc","size":10,"color":"#445566","font":{"family":"Consolas"}}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    TextAnnotation const &text = std::get<TextAnnotation>(result.annotations[0].data);
    EXPECT_EQ(text.base_style.color, Make_colorref(0x44, 0x55, 0x66));
    EXPECT_EQ(text.base_style.font_choice, TextFontChoice::Mono);
    EXPECT_EQ(text.base_style.font_family, L"Consolas");
}

TEST(cli_annotation_import,
     parse_document_defaults_override_bubble_config_font_and_color) {
    AppConfig config = Make_config();
    config.bubble_current_font = TextFontChoice::Art;

    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"color":"#112233","font":{"preset":"serif"},"annotations":[{"type":"bubble","center":{"x":10,"y":20},"size":5}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    BubbleAnnotation const &bubble =
        std::get<BubbleAnnotation>(result.annotations[0].data);
    EXPECT_EQ(bubble.color, Make_colorref(0x11, 0x22, 0x33));
    EXPECT_EQ(bubble.font_choice, TextFontChoice::Serif);
    EXPECT_TRUE(bubble.font_family.empty());
}

TEST(cli_annotation_import, parse_rejects_empty_brush_points_array) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"brush","size":4,"points":[]}]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"must not be empty."), std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_missing_required_point_coordinate) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"line","start":{"x":1},"end":{"x":10,"y":20},"size":3}]})",
        Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"start.y is required."), std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_empty_input) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result =
        Parse_cli_annotations_json("", Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"Unexpected end of JSON input."),
              std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_false_and_null_top_level_literals) {
    AppConfig const config = Make_config();

    {
        CliAnnotationParseResult const result =
            Parse_cli_annotations_json("false", Make_context(config));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"top-level JSON value must be an object."),
                  std::wstring::npos);
    }

    {
        CliAnnotationParseResult const result =
            Parse_cli_annotations_json("null", Make_context(config));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"top-level JSON value must be an object."),
                  std::wstring::npos);
    }
}

TEST(cli_annotation_import, parse_rejects_object_and_array_syntax_errors) {
    AppConfig const config = Make_config();

    {
        CliAnnotationParseResult const result =
            Parse_cli_annotations_json(R"({"annotations" []})", Make_context(config));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(
            result.error_message.find(L"Expected ':' after an object property name."),
            std::wstring::npos);
    }

    {
        CliAnnotationParseResult const result = Parse_cli_annotations_json(
            R"({"annotations":[{} {}]})", Make_context(config));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(
            result.error_message.find(L"Expected ',' or ']' after an array element."),
            std::wstring::npos);
    }

    {
        CliAnnotationParseResult const result =
            Parse_cli_annotations_json(R"({annotations:[]})", Make_context(config));
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"Expected a quoted object property name."),
                  std::wstring::npos);
    }
}

TEST(cli_annotation_import,
     parse_arrow_uses_config_size_and_accepts_uppercase_hex_color) {
    AppConfig config = Make_config();
    config.arrow_size = 9;

    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"arrow","start":{"x":1,"y":2},"end":{"x":10,"y":20},"color":"#AaBbCc"}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    LineAnnotation const &arrow = std::get<LineAnnotation>(result.annotations[0].data);
    EXPECT_TRUE(arrow.arrow_head);
    EXPECT_EQ(arrow.style.width_px, 9);
    EXPECT_EQ(arrow.style.color, Make_colorref(0xAA, 0xBB, 0xCC));
}

TEST(cli_annotation_import,
     parse_highlighter_opacity_precedence_uses_annotation_then_document_then_config) {
    AppConfig config = Make_config();
    config.highlighter_opacity_percent = 61;

    {
        CliAnnotationParseResult const result = Parse_cli_annotations_json(
            R"({"highlighter_opacity_percent":42,"annotations":[{"type":"highlighter","start":{"x":0,"y":0},"end":{"x":10,"y":10},"size":4},{"type":"highlighter","start":{"x":10,"y":10},"end":{"x":20,"y":20},"size":4,"opacity_percent":17}]})",
            Make_context(config));

        ASSERT_TRUE(result.ok);
        ASSERT_EQ(result.annotations.size(), 2u);
        EXPECT_EQ(std::get<FreehandStrokeAnnotation>(result.annotations[0].data)
                      .style.opacity_percent,
                  42);
        EXPECT_EQ(std::get<FreehandStrokeAnnotation>(result.annotations[1].data)
                      .style.opacity_percent,
                  17);
    }

    {
        CliAnnotationParseResult const result = Parse_cli_annotations_json(
            R"({"annotations":[{"type":"highlighter","start":{"x":0,"y":0},"end":{"x":10,"y":10},"size":4}]})",
            Make_context(config));

        ASSERT_TRUE(result.ok);
        ASSERT_EQ(result.annotations.size(), 1u);
        EXPECT_EQ(std::get<FreehandStrokeAnnotation>(result.annotations[0].data)
                      .style.opacity_percent,
                  61);
    }
}

TEST(cli_annotation_import,
     parse_bubble_family_override_keeps_document_preset_for_font_choice) {
    AppConfig config = Make_config();
    config.bubble_current_font = TextFontChoice::Art;

    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"font":{"preset":"serif"},"annotations":[{"type":"bubble","center":{"x":10,"y":20},"size":5,"font":{"family":"Comic Sans"}}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    BubbleAnnotation const &bubble =
        std::get<BubbleAnnotation>(result.annotations[0].data);
    EXPECT_EQ(bubble.font_choice, TextFontChoice::Serif);
    EXPECT_EQ(bubble.font_family, L"Comic Sans");
}

TEST(cli_annotation_import, parse_rejects_invalid_utf8_text_string) {
    AppConfig const config = Make_config();
    std::string json = "{\"annotations\":[{\"type\":\"text\",\"origin\":{\"x\":0,\"y\":"
                       "0},\"size\":10,\"text\":\"";
    json.push_back(static_cast<char>(0x80));
    json += "\"}]}";

    CliAnnotationParseResult const result =
        Parse_cli_annotations_json(json, Make_context(config));

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"must be valid UTF-8."), std::wstring::npos);
}

TEST(cli_annotation_import, parse_rejects_additional_json_syntax_errors) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext const context = Make_context(config);

    Expect_parse_error_contains("@", context,
                                L"Unexpected character while parsing a JSON value.");
    Expect_parse_error_contains(R"({"annotations":[] "color":"#112233"})", context,
                                L"Expected ',' or '}' after an object member.");
    Expect_parse_error_contains(R"({"annotations":[])", context,
                                L"Unexpected end of JSON input inside an object.");
    Expect_parse_error_contains(R"({"annotations":[)", context,
                                L"Unexpected end of JSON input inside an array.");
    Expect_parse_error_contains(R"({"annotations":[trux]})", context,
                                L"Invalid JSON literal.");
    Expect_parse_error_contains(R"({"annotations":[-]})", context,
                                L"Invalid JSON number.");
    Expect_parse_error_contains(R"({"annotations":[1.]})", context,
                                L"Invalid JSON number.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"\u12x4","size":10}]})",
        context, L"Invalid \\u escape sequence in JSON string.");

    std::string control_char_json =
        "{\"annotations\":[{\"type\":\"text\",\"origin\":{\"x\":0,\"y\":0},\"text\":"
        "\"alpha\nbeta\",\"size\":10}]}";
    Expect_parse_error_contains(control_char_json, context,
                                L"Control characters are not allowed in JSON strings.");

    std::string incomplete_escape_json = "{\"annotations\":[{\"type\":\"text\","
                                         "\"origin\":{\"x\":0,\"y\":0},\"text\":\"abc";
    incomplete_escape_json.push_back('\\');
    Expect_parse_error_contains(incomplete_escape_json, context,
                                L"Incomplete escape sequence in JSON string.");

    std::string unterminated_string_json =
        "{\"annotations\":[{\"type\":\"text\",\"origin\":{\"x\":0,\"y\":0},\"text\":"
        "\"abc";
    Expect_parse_error_contains(unterminated_string_json, context,
                                L"Unterminated JSON string.");
}

TEST(cli_annotation_import, parse_rejects_missing_parse_context_config) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext context = Make_context(config);
    context.config = nullptr;

    Expect_parse_error_contains(
        R"({"annotations":[]})", context,
        L"internal annotation parse context is missing config.");
}

TEST(cli_annotation_import,
     parse_rejects_invalid_root_schema_coordinate_space_and_annotations_shape) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext const context = Make_context(config);

    Expect_parse_error_contains(R"({"$schema":-1.5e+2,"annotations":[]})", context,
                                L"$.$schema must be a string.");
    Expect_parse_error_contains(R"({"coordinate_space":1,"annotations":[]})", context,
                                L"$.coordinate_space must be \"local\" or \"global\".");
    Expect_parse_error_contains(R"({"coordinate_space":"desktop","annotations":[]})",
                                context,
                                L"$.coordinate_space must be \"local\" or \"global\".");
    Expect_parse_error_contains("{}", context, L"$.annotations is required.");
    Expect_parse_error_contains(R"({"annotations":{}})", context,
                                L"$.annotations must be an array.");
}

TEST(cli_annotation_import, parse_accepts_explicit_local_coordinate_space) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"coordinate_space":"local","annotations":[{"type":"bubble","center":{"x":4,"y":6},"size":3}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    BubbleAnnotation const &bubble =
        std::get<BubbleAnnotation>(result.annotations[0].data);
    EXPECT_EQ(bubble.center, (PointPx{104, 206}));
    EXPECT_EQ(bubble.diameter_px, 23);
}

TEST(cli_annotation_import, parse_rejects_additional_invalid_font_specs) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext const context = Make_context(config);

    Expect_parse_error_contains(R"({"font":"sans","annotations":[]})", context,
                                L"$.font must be an object.");
    Expect_parse_error_contains(R"({"font":{"preset":1},"annotations":[]})", context,
                                L"$.font.preset must be a string.");
    Expect_parse_error_contains(
        R"({"font":{"preset":"display"},"annotations":[]})", context,
        L"$.font.preset must be one of: sans, serif, mono, art.");
    Expect_parse_error_contains(R"({"font":{"family":1},"annotations":[]})", context,
                                L"$.font.family must be a string.");

    std::string invalid_utf8_family_json = "{\"font\":{\"family\":\"";
    invalid_utf8_family_json.push_back(static_cast<char>(0x80));
    invalid_utf8_family_json += "\"},\"annotations\":[]}";
    Expect_parse_error_contains(invalid_utf8_family_json, context,
                                L"$.font.family must be valid UTF-8.");

    std::string const long_family(kMaxTextFontFamilyChars + 1, 'a');
    std::string const long_family_json =
        "{\"font\":{\"family\":\"" + long_family + "\"},\"annotations\":[]}";
    Expect_parse_error_contains(
        long_family_json, context,
        L"$.font.family must be at most 128 UTF-16 code units.");
}

TEST(cli_annotation_import,
     parse_document_font_family_defaults_are_trimmed_and_presets_override_choice) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"font":{"family":"  Courier Prime  "},"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"abc","size":10,"font":{"preset":"mono"}},{"type":"bubble","center":{"x":1,"y":2},"size":4,"font":{"preset":"art"}}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 2u);

    TextAnnotation const &text = std::get<TextAnnotation>(result.annotations[0].data);
    EXPECT_EQ(text.base_style.font_family, L"Courier Prime");
    EXPECT_EQ(text.base_style.font_choice, TextFontChoice::Mono);

    BubbleAnnotation const &bubble =
        std::get<BubbleAnnotation>(result.annotations[1].data);
    EXPECT_EQ(bubble.font_family, L"Courier Prime");
    EXPECT_EQ(bubble.font_choice, TextFontChoice::Art);
}

TEST(cli_annotation_import, parse_rejects_invalid_annotation_objects_and_types) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext const context = Make_context(config);

    Expect_parse_error_contains(R"({"annotations":[1]})", context,
                                L"$.annotations[0] must be an object.");
    Expect_parse_error_contains(R"({"annotations":[{}]})", context,
                                L"$.annotations[0] must contain \"type\".");
    Expect_parse_error_contains(R"({"annotations":[{"type":1}]})", context,
                                L"$.annotations[0].type must be a string.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"triangle"}]})", context,
        L"$.annotations[0].type must be a supported annotation type.");

    std::string invalid_utf8_type_json = "{\"annotations\":[{\"type\":\"";
    invalid_utf8_type_json.push_back(static_cast<char>(0x80));
    invalid_utf8_type_json += "\"}]}";
    Expect_parse_error_contains(invalid_utf8_type_json, context,
                                L"$.annotations[0].type must be valid UTF-8.");
}

TEST(cli_annotation_import, parse_rejects_invalid_annotation_field_types) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext const context = Make_context(config);

    Expect_parse_error_contains(
        R"({"annotations":[{"type":"line","start":{"x":0,"y":0},"end":{"x":1,"y":1},"color":1}]})",
        context, L"$.annotations[0].color must be a #rrggbb color string.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"line","start":{"x":0,"y":0},"end":{"x":1,"y":1},"size":0}]})",
        context, L"$.annotations[0].size must be an integer in ");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"highlighter","start":{"x":0,"y":0},"end":{"x":1,"y":1},"size":4,"opacity_percent":"40"}]})",
        context, L"$.annotations[0].opacity_percent must be an integer in 0..100.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"line","start":[],"end":{"x":1,"y":1}}]})", context,
        L"$.annotations[0].start must be an object.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"line","start":{"x":0,"y":0,"z":1},"end":{"x":1,"y":1}}]})",
        context, L"$.annotations[0].start.z contains an unknown property.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"rectangle","left":"0","top":1,"width":2,"height":3}]})",
        context, L"$.annotations[0].left must be an integer.");
}

TEST(cli_annotation_import, parse_rejects_invalid_geometry_requirements) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext const context = Make_context(config);

    Expect_parse_error_contains(
        R"({"annotations":[{"type":"line","start":{"x":0,"y":0}}]})", context,
        L"$.annotations[0] must contain both \"start\" and \"end\".");
    Expect_parse_error_contains(R"({"annotations":[{"type":"brush","size":4}]})",
                                context, L"$.annotations[0].points is required.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"highlighter","points":"bad","size":4}]})", context,
        L"$.annotations[0].points must be an array.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"rectangle","left":1,"top":2,"width":0,"height":3}]})",
        context, L"requires positive width and height.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"ellipse","center":{"x":5,"y":6},"width":4,"height":0}]})",
        context, L"requires positive width and height.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"obfuscate","left":1,"top":2,"width":3,"height":0}]})",
        context, L"requires positive width and height.");
    Expect_parse_error_contains(R"({"annotations":[{"type":"bubble","size":4}]})",
                                context, L"$.annotations[0].center is required.");
}

TEST(cli_annotation_import, parse_rejects_invalid_text_shapes) {
    AppConfig const config = Make_config();
    CliAnnotationParseContext const context = Make_context(config);

    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"abc","spans":[{"text":"def"}],"size":10}]})",
        context, L"must contain exactly one of \"text\" or \"spans\".");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"size":10}]})",
        context, L"must contain exactly one of \"text\" or \"spans\".");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"text":"","size":10}]})",
        context, L"$.annotations[0].text must not be empty.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"spans":"bad","size":10}]})",
        context, L"$.annotations[0].spans must be an array.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"spans":[],"size":10}]})",
        context, L"$.annotations[0].spans must not be empty.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"spans":[1],"size":10}]})",
        context, L"$.annotations[0].spans[0] must be an object.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"spans":[{"text":""}],"size":10}]})",
        context, L"$.annotations[0].spans[0].text must not be empty.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":0,"y":0},"spans":[{"text":"abc","bold":1}],"size":10}]})",
        context, L"$.annotations[0].spans[0].bold must be a boolean.");
}

TEST(cli_annotation_import, parse_parses_filled_ellipse) {
    AppConfig const config = Make_config();
    CliAnnotationParseResult const result = Parse_cli_annotations_json(
        R"({"annotations":[{"type":"filled_ellipse","center":{"x":20,"y":40},"width":30,"height":10,"color":"#102030"}]})",
        Make_context(config));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.annotations.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<EllipseAnnotation>(result.annotations[0].data));

    EllipseAnnotation const &ellipse =
        std::get<EllipseAnnotation>(result.annotations[0].data);
    EXPECT_TRUE(ellipse.filled);
    EXPECT_EQ(ellipse.outer_bounds, (RectPx::From_ltrb(105, 235, 135, 245)));
    EXPECT_EQ(ellipse.style.color, Make_colorref(0x10, 0x20, 0x30));
}

TEST(cli_annotation_import, parse_rejects_coordinate_and_bounds_overflows) {
    AppConfig const config = Make_config();
    int32_t const max_int32 = std::numeric_limits<int32_t>::max();
    RectPx const near_limit_rect =
        RectPx::From_ltrb(max_int32 - 8, max_int32 - 8, max_int32 - 1, max_int32 - 1);
    RectPx const near_edge_rect =
        RectPx::From_ltrb(max_int32 - 2, max_int32 - 2, max_int32 - 1, max_int32 - 1);

    Expect_parse_error_contains(
        R"({"annotations":[{"type":"line","start":{"x":5,"y":0},"end":{"x":0,"y":0}}]})",
        Make_context(config, near_edge_rect), L"overflows screen-space coordinates.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"brush","size":4,"points":[{"x":5,"y":0}]}]})",
        Make_context(config, near_edge_rect), L"overflows screen-space coordinates.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"highlighter","points":[{"x":5,"y":0},{"x":6,"y":1}],"size":4}]})",
        Make_context(config, near_edge_rect), L"overflows screen-space coordinates.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"rectangle","left":1,"top":1,"width":10,"height":10}]})",
        Make_context(config, near_limit_rect), L"overflows rectangle bounds.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"ellipse","center":{"x":0,"y":0},"width":10,"height":10}]})",
        Make_context(config, near_edge_rect), L"overflows ellipse bounds.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"obfuscate","left":1,"top":1,"width":10,"height":10}]})",
        Make_context(config, near_limit_rect), L"overflows rectangle bounds.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"text","origin":{"x":5,"y":0},"text":"abc","size":10}]})",
        Make_context(config, near_edge_rect), L"overflows screen-space coordinates.");
    Expect_parse_error_contains(
        R"({"annotations":[{"type":"bubble","center":{"x":5,"y":0},"size":4}]})",
        Make_context(config, near_edge_rect), L"overflows screen-space coordinates.");
}
