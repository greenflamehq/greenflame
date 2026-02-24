#include "greenflame_core/cli_options.h"

using namespace greenflame::core;

TEST_CASE("CLI parser accepts no options", "[cli_options]") {
    std::vector<std::wstring> args = {};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE(result.ok);
    REQUIRE(result.options.capture_mode == CliCaptureMode::None);
    REQUIRE_FALSE(result.options.region_px.has_value());
    REQUIRE(result.options.output_path.empty());
}

TEST_CASE("CLI parser accepts region with equals syntax", "[cli_options]") {
    std::vector<std::wstring> args = {L"--region=10,20,30,40"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE(result.ok);
    REQUIRE(result.options.capture_mode == CliCaptureMode::Region);
    REQUIRE(result.options.region_px.has_value());
    REQUIRE(result.options.region_px->left == 10);
    REQUIRE(result.options.region_px->top == 20);
    REQUIRE(result.options.region_px->right == 40);
    REQUIRE(result.options.region_px->bottom == 60);
}

TEST_CASE("CLI parser rejects region with negative x or y", "[cli_options]") {
    std::vector<std::wstring> args = {L"--region=-10,20,30,40"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message.find(L"x>=0") != std::wstring::npos);
}

TEST_CASE("CLI parser rejects region with negative y", "[cli_options]") {
    std::vector<std::wstring> args = {L"--region=10,-20,30,40"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message.find(L"y>=0") != std::wstring::npos);
}

TEST_CASE("CLI parser rejects region with non-positive width or height",
          "[cli_options]") {
    {
        std::vector<std::wstring> args = {L"--region=10,20,0,40"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error_message.find(L"w>0") != std::wstring::npos);
    }
    {
        std::vector<std::wstring> args = {L"--region=10,20,30,-1"};
        CliParseResult const result = Parse_cli_arguments(args, false);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error_message.find(L"h>0") != std::wstring::npos);
    }
}

TEST_CASE("CLI parser rejects region split across four args", "[cli_options]") {
    std::vector<std::wstring> args = {L"-r", L"-100", L"-100", L"10", L"10"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message.find(L"one value x,y,w,h") != std::wstring::npos);
}

TEST_CASE("CLI parser accepts window and output with spaced values", "[cli_options]") {
    std::vector<std::wstring> args = {L"--window", L"Notepad", L"--output",
                                      L"C:\\tmp\\shot"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE(result.ok);
    REQUIRE(result.options.capture_mode == CliCaptureMode::Window);
    REQUIRE(result.options.window_name == L"Notepad");
    REQUIRE(result.options.output_path == L"C:\\tmp\\shot");
}

TEST_CASE("CLI parser accepts short monitor option with value", "[cli_options]") {
    std::vector<std::wstring> args = {L"-m", L"2"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE(result.ok);
    REQUIRE(result.options.capture_mode == CliCaptureMode::Monitor);
    REQUIRE(result.options.monitor_id == 2);
}

TEST_CASE("CLI parser rejects output without capture mode", "[cli_options]") {
    std::vector<std::wstring> args = {L"--output", L"C:\\tmp\\shot"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message.find(L"--output requires one capture mode") !=
            std::wstring::npos);
}

TEST_CASE("CLI parser rejects mutually exclusive capture modes", "[cli_options]") {
    std::vector<std::wstring> args = {L"--desktop", L"--monitor", L"1"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error_message.find(L"Only one capture mode") != std::wstring::npos);
}

TEST_CASE("CLI parser accepts help option", "[cli_options]") {
    std::vector<std::wstring> args = {L"--help"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE(result.ok);
    REQUIRE(result.options.capture_mode == CliCaptureMode::Help);
}

#ifdef DEBUG
TEST_CASE("CLI parser accepts debug-only testing flag in debug mode", "[cli_options]") {
    std::vector<std::wstring> args = {L"--testing-1-2"};
    CliParseResult const result = Parse_cli_arguments(args, true);
    REQUIRE(result.ok);
    REQUIRE(result.options.testing_1_2);
}
#endif

TEST_CASE("CLI parser rejects debug-only testing flag in release mode",
          "[cli_options]") {
    std::vector<std::wstring> args = {L"--testing-1-2"};
    CliParseResult const result = Parse_cli_arguments(args, false);
    REQUIRE_FALSE(result.ok);
}

TEST_CASE("CLI help text includes declared options", "[cli_options]") {
    std::wstring const help_release = Build_cli_help_text(false);
    REQUIRE(help_release.find(L"--region") != std::wstring::npos);
    REQUIRE(help_release.find(L"--window") != std::wstring::npos);
    REQUIRE(help_release.find(L"--monitor") != std::wstring::npos);
    REQUIRE(help_release.find(L"--desktop") != std::wstring::npos);
    REQUIRE(help_release.find(L"--help") != std::wstring::npos);
    REQUIRE(help_release.find(L"--output") != std::wstring::npos);
    REQUIRE(help_release.find(L"--testing-1-2") == std::wstring::npos);

#ifdef DEBUG
    std::wstring const help_debug = Build_cli_help_text(true);
    REQUIRE(help_debug.find(L"--testing-1-2") != std::wstring::npos);
#else
    std::wstring const help_debug = Build_cli_help_text(true);
    REQUIRE(help_debug.find(L"--testing-1-2") == std::wstring::npos);
#endif
}
