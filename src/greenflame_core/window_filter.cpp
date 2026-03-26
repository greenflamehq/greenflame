#include "greenflame_core/window_filter.h"

#include "greenflame_core/string_utils.h"

namespace greenflame::core {

namespace {

[[nodiscard]] std::wstring Format_hwnd(std::uintptr_t hwnd_value) {
    if (hwnd_value == 0) {
        return L"0x0";
    }

    constexpr std::array<wchar_t, 16> k_hex_digits = {{
        L'0',
        L'1',
        L'2',
        L'3',
        L'4',
        L'5',
        L'6',
        L'7',
        L'8',
        L'9',
        L'A',
        L'B',
        L'C',
        L'D',
        L'E',
        L'F',
    }};
    std::wstring result = {};
    std::uintptr_t value = hwnd_value;
    while (value != 0) {
        size_t const digit =
            static_cast<size_t>(value & static_cast<std::uintptr_t>(0xFu));
        result.push_back(k_hex_digits.at(digit));
        value >>= 4u;
    }
    std::reverse(result.begin(), result.end());
    result.insert(0, L"0x");
    return result;
}

} // namespace

bool Is_terminal_window_class(std::wstring_view class_name) noexcept {
    return Equals_no_case(class_name, L"ConsoleWindowClass") ||
           Equals_no_case(class_name, L"CASCADIA_HOSTING_WINDOW_CLASS");
}

bool Is_cli_invocation_window(WindowCandidateInfo const &candidate,
                              std::wstring_view query) noexcept {
    if (!Contains_no_case(candidate.title, L"greenflame.exe")) {
        return false;
    }
    if (!Contains_no_case(candidate.title, query)) {
        return false;
    }

    if (Is_terminal_window_class(candidate.class_name)) {
        return true;
    }

    return Contains_no_case(candidate.title, L"--window") ||
           Contains_no_case(candidate.title, L"-w ");
}

std::vector<WindowCandidateInfo>
Filter_cli_invocation_window(std::vector<WindowCandidateInfo> matches,
                             std::wstring_view query) {
    matches.erase(std::remove_if(matches.begin(), matches.end(),
                                 [&](WindowCandidateInfo const &candidate) {
                                     return Is_cli_invocation_window(candidate, query);
                                 }),
                  matches.end());
    return matches;
}

std::wstring Format_window_candidate_line(WindowCandidateInfo const &candidate,
                                          size_t index) {
    RectPx const normalized = candidate.rect.Normalized();
    std::wstring line = L"  [";
    line += std::to_wstring(index + 1);
    line += L"] hwnd=";
    line += Format_hwnd(candidate.hwnd_value);
    line += L" class=\"";
    line += candidate.class_name;
    line += L"\" title=\"";
    line += candidate.title;
    line += L"\" (x=";
    line += std::to_wstring(normalized.left);
    line += L", y=";
    line += std::to_wstring(normalized.top);
    line += L", w=";
    line += std::to_wstring(normalized.Width());
    line += L", h=";
    line += std::to_wstring(normalized.Height());
    line += L")";
    if (candidate.uncapturable) {
        line += L" [uncapturable]";
    }
    return line;
}

} // namespace greenflame::core
