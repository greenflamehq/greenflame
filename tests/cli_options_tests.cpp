#include "greenflame_core/cli_options.h"
#include "greenflame_core/color_wheel.h"

using namespace greenflame::core;

TEST(cli_options, CLI_parser_AcceptsNoOptions) {
    std::vector<std::wstring> args = {};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::None);
    EXPECT_EQ(result.options.action, CliAction::None);
    EXPECT_FALSE(result.options.region_px.has_value());
    EXPECT_FALSE(result.options.window_hwnd.has_value());
    EXPECT_FALSE(result.options.padding_px.has_value());
    EXPECT_FALSE(result.options.padding_color_override.has_value());
    EXPECT_FALSE(result.options.annotate_value.has_value());
    EXPECT_TRUE(result.options.input_path.empty());
    EXPECT_TRUE(result.options.output_path.empty());
    EXPECT_FALSE(result.options.output_format.has_value());
    EXPECT_EQ(result.options.window_capture_backend, WindowCaptureBackend::Auto);
    EXPECT_FALSE(result.options.window_capture_backend_explicit);
    EXPECT_EQ(result.options.cursor_override, CliCursorOverride::UseConfig);
    EXPECT_FALSE(result.options.overwrite_output);
#ifdef DEBUG
#endif
}

TEST(cli_options, CLI_parser_AcceptsRegionEquals) {
    std::vector<std::wstring> args = {L"--region=10,20,30,40"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::Region);
    EXPECT_TRUE(result.options.region_px.has_value());
    EXPECT_EQ(result.options.region_px->left, 10);
    EXPECT_EQ(result.options.region_px->top, 20);
    EXPECT_EQ(result.options.region_px->right, 40);
    EXPECT_EQ(result.options.region_px->bottom, 60);
}

TEST(cli_options, CLI_parser_RejectsRegionNegativeX) {
    std::vector<std::wstring> args = {L"--region=-10,20,30,40"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"x>=0"), std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsRegionNegativeY) {
    std::vector<std::wstring> args = {L"--region=10,-20,30,40"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"y>=0"), std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsNonPositiveWidthOrHeight) {
    {
        std::vector<std::wstring> args = {L"--region=10,20,0,40"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"w>0"), std::wstring::npos);
    }
    {
        std::vector<std::wstring> args = {L"--region=10,20,30,-1"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"h>0"), std::wstring::npos);
    }
}

TEST(cli_options, CLI_parser_RejectsRegionSplitAcrossArgs) {
    std::vector<std::wstring> args = {L"-r", L"-100", L"-100", L"10", L"10"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"one value x,y,w,h"), std::wstring::npos);
}

TEST(cli_options, CLI_parser_AcceptsWindowAndOutput) {
    std::vector<std::wstring> args = {L"--window", L"Notepad", L"--output",
                                      L"C:\\tmp\\shot"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::Window);
    EXPECT_EQ(result.options.window_name, L"Notepad");
    EXPECT_EQ(result.options.output_path, L"C:\\tmp\\shot");
}

TEST(cli_options, CLI_parser_AcceptsWindowHwnd) {
    std::vector<std::wstring> args = {L"--window-hwnd", L"0x1234ABCD"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::Window);
    ASSERT_TRUE(result.options.window_hwnd.has_value());
    EXPECT_EQ(*result.options.window_hwnd, static_cast<std::uintptr_t>(0x1234ABCDu));
    EXPECT_TRUE(result.options.window_name.empty());
}

TEST(cli_options, CLI_parser_RejectsInvalidWindowHwnd) {
    for (std::vector<std::wstring> const &args :
         {std::vector<std::wstring>{L"--window-hwnd", L"0"},
          std::vector<std::wstring>{L"--window-hwnd", L"xyz"},
          std::vector<std::wstring>{L"--window-hwnd", L"0x"}}) {
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"--window-hwnd expects"),
                  std::wstring::npos);
    }
}

TEST(cli_options, CLI_parser_AcceptsInputAnnotateOutputAndOverwrite) {
    std::vector<std::wstring> args = {L"--input",     L"shot.png",
                                      L"--annotate",  L"{\"annotations\":[]}",
                                      L"--output",    L"C:\\tmp\\annotated.png",
                                      L"--overwrite", L"--padding",
                                      L"4",           L"--format",
                                      L"png"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::None);
    EXPECT_EQ(result.options.input_path, L"shot.png");
    ASSERT_TRUE(result.options.annotate_value.has_value());
    EXPECT_EQ(*result.options.annotate_value, L"{\"annotations\":[]}");
    EXPECT_EQ(result.options.output_path, L"C:\\tmp\\annotated.png");
    EXPECT_TRUE(result.options.overwrite_output);
    ASSERT_TRUE(result.options.padding_px.has_value());
    EXPECT_EQ(*result.options.padding_px, (InsetsPx{4, 4, 4, 4}));
    ASSERT_TRUE(result.options.output_format.has_value());
    EXPECT_EQ(*result.options.output_format, CliOutputFormat::Png);
}

TEST(cli_options, CLI_parser_RejectsInputWithoutAnnotate) {
    std::vector<std::wstring> args = {L"--input", L"shot.png", L"--overwrite"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--input requires --annotate."),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsBareInputWithoutOutputOrOverwrite) {
    std::vector<std::wstring> args = {L"--input", L"shot.png", L"--annotate",
                                      L"{\"annotations\":[]}"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(
        result.error_message.find(L"--input requires either --output or --overwrite."),
        std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsInputWithLiveCaptureModesAndWindowCapture) {
    for (std::vector<std::wstring> const &args :
         {std::vector<std::wstring>{L"--input", L"shot.png", L"--region",
                                    L"10,20,30,40"},
          std::vector<std::wstring>{L"--input", L"shot.png", L"--window", L"Notepad"},
          std::vector<std::wstring>{L"--input", L"shot.png", L"--window-hwnd",
                                    L"0x1234"},
          std::vector<std::wstring>{L"--input", L"shot.png", L"--monitor", L"1"},
          std::vector<std::wstring>{L"--input", L"shot.png", L"--desktop"},
          std::vector<std::wstring>{L"--input", L"shot.png", L"--window-capture",
                                    L"gdi"}}) {
        std::vector<std::wstring> full_args = args;
        if (full_args.back() != L"gdi") {
            full_args.push_back(L"--annotate");
            full_args.push_back(L"{\"annotations\":[]}");
            full_args.push_back(L"--overwrite");
        }
        CliParseResult const result = Parse_cli_arguments(full_args, false);
        EXPECT_FALSE(result.ok);
    }
}

TEST(cli_options, CLI_parser_AcceptsPaddingSingleValue) {
    std::vector<std::wstring> args = {L"--desktop", L"--padding", L"12"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    ASSERT_TRUE(result.options.padding_px.has_value());
    EXPECT_EQ(*result.options.padding_px, (InsetsPx{12, 12, 12, 12}));
}

TEST(cli_options, CLI_parser_AcceptsPaddingShortOptionAndTwoValueForm) {
    std::vector<std::wstring> args = {L"--monitor", L"2", L"-p", L"8,16"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    ASSERT_TRUE(result.options.padding_px.has_value());
    EXPECT_EQ(*result.options.padding_px, (InsetsPx{8, 16, 8, 16}));
}

TEST(cli_options, CLI_parser_AcceptsPaddingFourValueFormAndColorOverride) {
    std::vector<std::wstring> args = {L"--region", L"10,20,30,40",     L"--padding",
                                      L"1,2,3,4",  L"--padding-color", L"#AaBbCc"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    ASSERT_TRUE(result.options.padding_px.has_value());
    ASSERT_TRUE(result.options.padding_color_override.has_value());
    EXPECT_EQ(*result.options.padding_px, (InsetsPx{1, 2, 3, 4}));
    EXPECT_EQ(*result.options.padding_color_override, Make_colorref(0xAA, 0xBB, 0xCC));
}

TEST(cli_options, CLI_parser_AcceptsPaddingWithWhitespaceAroundCommas) {
    std::vector<std::wstring> args = {L"--desktop", L"--padding", L" 4 , 8 "};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    ASSERT_TRUE(result.options.padding_px.has_value());
    EXPECT_EQ(*result.options.padding_px, (InsetsPx{4, 8, 4, 8}));
}

TEST(cli_options, CLI_parser_RejectsZeroOnlyPadding) {
    for (std::vector<std::wstring> const &args :
         {std::vector<std::wstring>{L"--desktop", L"--padding", L"0"},
          std::vector<std::wstring>{L"--desktop", L"--padding", L"0,0"},
          std::vector<std::wstring>{L"--desktop", L"--padding", L"0,0,0,0"}}) {
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"--padding expects"), std::wstring::npos);
    }
}

TEST(cli_options, CLI_parser_RejectsNegativePaddingAndThreeValuePadding) {
    {
        std::vector<std::wstring> args = {L"--desktop", L"--padding", L"-1"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"--padding expects"), std::wstring::npos);
    }
    {
        std::vector<std::wstring> args = {L"--desktop", L"--padding", L"1,2,3"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"--padding expects"), std::wstring::npos);
    }
}

TEST(cli_options, CLI_parser_RejectsPaddingWithoutCaptureMode) {
    std::vector<std::wstring> args = {L"--padding", L"4"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--padding requires one render source"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsPaddingColorWithoutPadding) {
    std::vector<std::wstring> args = {L"--desktop", L"--padding-color", L"#112233"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--padding-color requires --padding"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_AcceptsAnnotateInlineJson) {
    std::vector<std::wstring> args = {L"--desktop", L"--annotate",
                                      L"{\"annotations\":[]}"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    ASSERT_TRUE(result.options.annotate_value.has_value());
    EXPECT_EQ(*result.options.annotate_value, L"{\"annotations\":[]}");
}

TEST(cli_options, CLI_parser_AcceptsAnnotateEqualsForm) {
    std::vector<std::wstring> args = {L"--desktop", L"--annotate=annotations.json"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    ASSERT_TRUE(result.options.annotate_value.has_value());
    EXPECT_EQ(*result.options.annotate_value, L"annotations.json");
}

TEST(cli_options, CLI_parser_RejectsDuplicateAnnotate) {
    std::vector<std::wstring> args = {L"--desktop", L"--annotate", L"{}", L"--annotate",
                                      L"annotations.json"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--annotate can only be specified once"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsAnnotateWithoutCaptureMode) {
    std::vector<std::wstring> args = {L"--annotate", L"{}"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--annotate requires one render source"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsWhitespaceOnlyAnnotateValue) {
    std::vector<std::wstring> args = {L"--desktop", L"--annotate", L"   "};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(
                  L"--annotate expects a non-empty JSON string or path"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_AcceptsWindowCaptureBackendValues) {
    {
        std::vector<std::wstring> args = {L"--window", L"Notepad", L"--window-capture",
                                          L"auto"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.window_capture_backend, WindowCaptureBackend::Auto);
        EXPECT_TRUE(result.options.window_capture_backend_explicit);
    }
    {
        std::vector<std::wstring> args = {L"--window", L"Notepad",
                                          L"--window-capture=gdi"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.window_capture_backend, WindowCaptureBackend::Gdi);
        EXPECT_TRUE(result.options.window_capture_backend_explicit);
    }
    {
        std::vector<std::wstring> args = {L"--window-hwnd", L"0x1234",
                                          L"--window-capture", L"wgc"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.window_capture_backend, WindowCaptureBackend::Wgc);
        EXPECT_TRUE(result.options.window_capture_backend_explicit);
    }
}

TEST(cli_options, CLI_parser_RejectsInvalidWindowCaptureBackend) {
    std::vector<std::wstring> args = {L"--window", L"Notepad", L"--window-capture",
                                      L"dxgi"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--window-capture expects one of"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsDuplicateWindowCaptureBackend) {
    std::vector<std::wstring> args = {L"--window",         L"Notepad",
                                      L"--window-capture", L"wgc",
                                      L"--window-capture", L"gdi"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(
        result.error_message.find(L"--window-capture can only be specified once."),
        std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsWindowCaptureBackendWithoutWindowMode) {
    for (std::vector<std::wstring> const &args :
         {std::vector<std::wstring>{L"--desktop", L"--window-capture", L"auto"},
          std::vector<std::wstring>{L"--monitor", L"1", L"--window-capture", L"wgc"},
          std::vector<std::wstring>{L"--window-capture", L"gdi"}}) {
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(
                      L"--window-capture requires --window or --window-hwnd."),
                  std::wstring::npos);
    }
}

TEST(cli_options, CLI_parser_AcceptsCursorOverrideFlags) {
    {
        std::vector<std::wstring> args = {L"--desktop", L"--cursor"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.cursor_override, CliCursorOverride::ForceInclude);
    }
    {
        std::vector<std::wstring> args = {L"--region", L"10,20,30,40", L"--no-cursor"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.cursor_override, CliCursorOverride::ForceExclude);
    }
}

TEST(cli_options, CLI_parser_RejectsMutuallyExclusiveCursorOverrideFlags) {
    std::vector<std::wstring> args = {L"--desktop", L"--cursor", L"--no-cursor"};
    CliParseResult const result = Parse_cli_arguments(args, false);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--cursor and --no-cursor are mutually exclusive"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsCursorOverrideWithInput) {
    for (std::vector<std::wstring> const &args :
         {std::vector<std::wstring>{L"--input",     L"shot.png",
                                    L"--annotate",  L"{\"annotations\":[]}",
                                    L"--overwrite", L"--cursor"},
          std::vector<std::wstring>{L"--input",     L"shot.png",
                                    L"--annotate",  L"{\"annotations\":[]}",
                                    L"--overwrite", L"--no-cursor"}}) {
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(
            result.error_message.find(L"--cursor and --no-cursor cannot be used with --input"),
            std::wstring::npos);
    }
}

TEST(cli_options, CLI_parser_RejectsInvalidPaddingColorAndDuplicates) {
    {
        std::vector<std::wstring> args = {L"--desktop", L"--padding", L"4",
                                          L"--padding-color", L"#12345"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"--padding-color expects"),
                  std::wstring::npos);
    }
    {
        std::vector<std::wstring> args = {L"--desktop", L"--padding", L"4",
                                          L"--padding", L"8"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"--padding can only be specified once"),
                  std::wstring::npos);
    }
    {
        std::vector<std::wstring> args = {
            L"--desktop", L"--padding",       L"4",      L"--padding-color",
            L"#112233",   L"--padding-color", L"#445566"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(
            result.error_message.find(L"--padding-color can only be specified once"),
            std::wstring::npos);
    }
}

TEST(cli_options, CLI_parser_AcceptsShortMonitor) {
    std::vector<std::wstring> args = {L"-m", L"2"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::Monitor);
    EXPECT_EQ(result.options.monitor_id, 2);
}

TEST(cli_options, CLI_parser_AcceptsFormatOption) {
    {
        std::vector<std::wstring> args = {L"--desktop", L"--format", L"png"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.capture_mode, CliCaptureMode::Desktop);
        EXPECT_TRUE(result.options.output_format.has_value());
        EXPECT_EQ(*result.options.output_format, CliOutputFormat::Png);
    }
    {
        std::vector<std::wstring> args = {L"--desktop", L"-t", L"jpeg"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_TRUE(result.options.output_format.has_value());
        EXPECT_EQ(*result.options.output_format, CliOutputFormat::Jpeg);
    }
    {
        std::vector<std::wstring> args = {L"--desktop", L"--format=bMp"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_TRUE(result.options.output_format.has_value());
        EXPECT_EQ(*result.options.output_format, CliOutputFormat::Bmp);
    }
}

TEST(cli_options, CLI_parser_RejectsInvalidFormat) {
    std::vector<std::wstring> args = {L"--desktop", L"--format", L"tiff"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--format expects one of"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsFormatWithoutCaptureMode) {
    std::vector<std::wstring> args = {L"--format", L"jpg"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--format requires one render source"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsOutputWithoutCaptureMode) {
    std::vector<std::wstring> args = {L"--output", L"C:\\tmp\\shot"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--output requires one render source"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_AcceptsOverwrite) {
    {
        std::vector<std::wstring> args = {L"--overwrite"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.capture_mode, CliCaptureMode::None);
        EXPECT_TRUE(result.options.overwrite_output);
    }
    {
        std::vector<std::wstring> args = {L"--desktop", L"-f"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_TRUE(result.ok);
        EXPECT_EQ(result.options.capture_mode, CliCaptureMode::Desktop);
        EXPECT_TRUE(result.options.overwrite_output);
    }
}

TEST(cli_options, CLI_parser_RejectsMutuallyExclusiveModes) {
    for (std::vector<std::wstring> const &args :
         {std::vector<std::wstring>{L"--desktop", L"--monitor", L"1"},
          std::vector<std::wstring>{L"--window", L"Notepad", L"--window-hwnd",
                                    L"0x1234"}}) {
        CliParseResult const result = Parse_cli_arguments(args, false);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error_message.find(L"Only one mode"), std::wstring::npos);
    }
}

TEST(cli_options, Has_cli_render_source_TreatsInputAsOneShotSource) {
    CliOptions options{};
    EXPECT_FALSE(Has_cli_render_source(options));

    options.capture_mode = CliCaptureMode::Desktop;
    EXPECT_TRUE(Has_cli_render_source(options));

    options.capture_mode = CliCaptureMode::None;
    options.input_path = L"shot.png";
    EXPECT_TRUE(Has_cli_render_source(options));
}

TEST(cli_options, CLI_parser_AcceptsHelp) {
    std::vector<std::wstring> args = {L"--help"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::None);
    EXPECT_EQ(result.options.action, CliAction::Help);
}

TEST(cli_options, CLI_parser_AcceptsVersionLong) {
    std::vector<std::wstring> args = {L"--version"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::None);
    EXPECT_EQ(result.options.action, CliAction::Version);
}

TEST(cli_options, CLI_parser_AcceptsVersionShort) {
    std::vector<std::wstring> args = {L"-v"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::None);
    EXPECT_EQ(result.options.action, CliAction::Version);
}

#ifdef DEBUG
TEST(cli_options, CLI_parser_AcceptsDebugFlag) {
    std::vector<std::wstring> args = {L"--testing-1-2"};
    CliParseResult const result = Parse_cli_arguments(args, true);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.options.testing_1_2);
}
#endif

TEST(cli_options, CLI_parser_RejectsDebugFlagInRelease) {
    std::vector<std::wstring> args = {L"--testing-1-2"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
}

TEST(cli_options, CLI_help_IncludesDeclaredOptions) {
    std::wstring const help_release = Build_cli_help_text(false);
    EXPECT_NE(help_release.find(L"--region"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--window"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--window-hwnd"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--monitor"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--desktop"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--input"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--help"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--version"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--output"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--format"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"-p, --padding"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--padding-color"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--annotate"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--window-capture"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--cursor"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--no-cursor"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--overwrite"), std::wstring::npos);
    EXPECT_EQ(help_release.find(L"--testing-1-2"), std::wstring::npos);

#ifdef DEBUG
    std::wstring const help_debug = Build_cli_help_text(true);
    EXPECT_NE(help_debug.find(L"--window-capture"), std::wstring::npos);
    EXPECT_NE(help_debug.find(L"--testing-1-2"), std::wstring::npos);
#else
    std::wstring const help_debug = Build_cli_help_text(true);
    EXPECT_NE(help_debug.find(L"--window-capture"), std::wstring::npos);
    EXPECT_EQ(help_debug.find(L"--testing-1-2"), std::wstring::npos);
#endif
}
