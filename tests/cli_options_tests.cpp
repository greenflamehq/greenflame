#include "greenflame_core/cli_options.h"

using namespace greenflame::core;

TEST(cli_options, CLI_parser_AcceptsNoOptions) {
    std::vector<std::wstring> args = {};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.options.capture_mode, CliCaptureMode::None);
    EXPECT_EQ(result.options.action, CliAction::None);
    EXPECT_FALSE(result.options.region_px.has_value());
    EXPECT_TRUE(result.options.output_path.empty());
    EXPECT_FALSE(result.options.output_format.has_value());
    EXPECT_FALSE(result.options.overwrite_output);
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
    EXPECT_NE(result.error_message.find(L"--format requires one capture mode"),
              std::wstring::npos);
}

TEST(cli_options, CLI_parser_RejectsOutputWithoutCaptureMode) {
    std::vector<std::wstring> args = {L"--output", L"C:\\tmp\\shot"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"--output requires one capture mode"),
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
    std::vector<std::wstring> args = {L"--desktop", L"--monitor", L"1"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find(L"Only one mode"), std::wstring::npos);
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
    EXPECT_NE(help_release.find(L"--monitor"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--desktop"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--help"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--version"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--output"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--format"), std::wstring::npos);
    EXPECT_NE(help_release.find(L"--overwrite"), std::wstring::npos);
    EXPECT_EQ(help_release.find(L"--testing-1-2"), std::wstring::npos);

#ifdef DEBUG
    std::wstring const help_debug = Build_cli_help_text(true);
    EXPECT_NE(help_debug.find(L"--testing-1-2"), std::wstring::npos);
#else
    std::wstring const help_debug = Build_cli_help_text(true);
    EXPECT_EQ(help_debug.find(L"--testing-1-2"), std::wstring::npos);
#endif
}
