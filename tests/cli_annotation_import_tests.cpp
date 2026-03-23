#include "greenflame_core/app_config.h"
#include "greenflame_core/cli_annotation_import.h"
#include "greenflame_core/color_wheel.h"

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
