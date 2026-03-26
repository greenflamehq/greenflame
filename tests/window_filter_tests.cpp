#include "greenflame_core/window_filter.h"

using namespace greenflame::core;

namespace {

WindowCandidateInfo Make_candidate(std::wstring const &title,
                                   std::wstring const &class_name, int32_t left) {
    WindowCandidateInfo candidate{};
    candidate.title = title;
    candidate.class_name = class_name;
    candidate.rect = RectPx::From_ltrb(left, 20, left + 300, 220);
    candidate.hwnd_value = static_cast<std::uintptr_t>(left) + 0x1000u;
    return candidate;
}

} // namespace

TEST(window_filter, Is_terminal_window_class_KnownClass) {
    EXPECT_TRUE(Is_terminal_window_class(L"ConsoleWindowClass"));
    EXPECT_TRUE(Is_terminal_window_class(L"cascadia_hosting_window_class"));
}

TEST(window_filter, Is_terminal_window_class_UnknownClass) {
    EXPECT_FALSE(Is_terminal_window_class(L"Notepad"));
}

TEST(window_filter, Is_cli_invocation_window_TerminalInvocation) {
    WindowCandidateInfo const candidate =
        Make_candidate(L"greenflame.exe --window Notepad", L"ConsoleWindowClass", 10);
    EXPECT_TRUE(Is_cli_invocation_window(candidate, L"Notepad"));
}

TEST(window_filter, Is_cli_invocation_window_FallbackWindowSwitch) {
    WindowCandidateInfo const candidate =
        Make_candidate(L"greenflame.exe -w Notepad", L"SomeHostClass", 10);
    EXPECT_TRUE(Is_cli_invocation_window(candidate, L"Notepad"));
}

TEST(window_filter, Is_cli_invocation_window_NotInvocation) {
    WindowCandidateInfo const candidate =
        Make_candidate(L"Notepad - notes.txt", L"Notepad", 10);
    EXPECT_FALSE(Is_cli_invocation_window(candidate, L"Notepad"));
}

TEST(window_filter, Filter_cli_invocation_window_RemovesOnlyInvocationWindows) {
    std::vector<WindowCandidateInfo> candidates = {
        Make_candidate(L"greenflame.exe --window Notepad", L"ConsoleWindowClass", 0),
        Make_candidate(L"Notepad - notes.txt", L"Notepad", 100),
        Make_candidate(L"notepad++", L"Notepad++", 200)};

    std::vector<WindowCandidateInfo> const filtered =
        Filter_cli_invocation_window(candidates, L"Notepad");

    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].title, L"Notepad - notes.txt");
    EXPECT_EQ(filtered[1].title, L"notepad++");
}

TEST(window_filter, Format_window_candidate_line_FormatsRectAndIndex) {
    WindowCandidateInfo candidate{};
    candidate.title = L"Notepad";
    candidate.class_name = L"Notepad";
    candidate.rect = RectPx::From_ltrb(10, 20, 310, 220);
    candidate.hwnd_value = static_cast<std::uintptr_t>(0x5151u);
    EXPECT_EQ(Format_window_candidate_line(candidate, 1),
              L"  [2] hwnd=0x5151 class=\"Notepad\" title=\"Notepad\" "
              L"(x=10, y=20, w=300, h=200)");
}

TEST(window_filter, Format_window_candidate_line_AppendsUncapturableMarker) {
    WindowCandidateInfo candidate{};
    candidate.title = L"Signal";
    candidate.class_name = L"Chrome_WidgetWin_1";
    candidate.rect = RectPx::From_ltrb(0, 0, 800, 600);
    candidate.hwnd_value = static_cast<std::uintptr_t>(0xABCDu);
    candidate.uncapturable = true;
    EXPECT_EQ(Format_window_candidate_line(candidate, 0),
              L"  [1] hwnd=0xABCD class=\"Chrome_WidgetWin_1\" title=\"Signal\" "
              L"(x=0, y=0, w=800, h=600) [uncapturable]");
}

TEST(window_filter, Format_window_candidate_line_OmitsUncapturableMarkerWhenFalse) {
    WindowCandidateInfo candidate{};
    candidate.title = L"Notepad";
    candidate.class_name = L"Notepad";
    candidate.rect = RectPx::From_ltrb(10, 20, 310, 220);
    candidate.hwnd_value = static_cast<std::uintptr_t>(0x5151u);
    candidate.uncapturable = false;
    std::wstring const line = Format_window_candidate_line(candidate, 0);
    EXPECT_EQ(line.find(L"[uncapturable]"), std::wstring::npos);
}
