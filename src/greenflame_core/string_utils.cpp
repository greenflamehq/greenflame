#include "greenflame_core/string_utils.h"

namespace greenflame::core {

namespace {

constexpr wchar_t kSavedPrefix[] = L"Saved: ";
constexpr wchar_t kSavedFallbackFilename[] = L"file";
constexpr wchar_t kSavedCopiedSuffix[] = L" (file copied to clipboard).";

} // namespace

bool Contains_no_case(std::wstring_view text, std::wstring_view needle) noexcept {
    if (needle.empty() || needle.size() > text.size()) {
        return false;
    }
    for (size_t start = 0; start + needle.size() <= text.size(); ++start) {
        bool match = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            wchar_t const a = static_cast<wchar_t>(std::towlower(text[start + i]));
            wchar_t const b = static_cast<wchar_t>(std::towlower(needle[i]));
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool Equals_no_case(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t const x = static_cast<wchar_t>(std::towlower(a[i]));
        wchar_t const y = static_cast<wchar_t>(std::towlower(b[i]));
        if (x != y) {
            return false;
        }
    }
    return true;
}

std::wstring Filename_from_path(std::wstring_view path) {
    size_t const slash = path.find_last_of(L"\\/");
    if (slash == std::wstring_view::npos || slash + 1 >= path.size()) {
        return std::wstring(path);
    }
    return std::wstring(path.substr(slash + 1));
}

std::wstring Build_saved_selection_balloon_message(std::wstring_view saved_path,
                                                   bool file_copied_to_clipboard) {
    std::wstring message = kSavedPrefix;
    std::wstring const filename = Filename_from_path(saved_path);
    if (!filename.empty()) {
        message += filename;
    } else {
        message += kSavedFallbackFilename;
    }
    if (file_copied_to_clipboard) {
        message += kSavedCopiedSuffix;
    } else {
        message += L".";
    }
    return message;
}

std::wstring
Build_saved_selection_path_copy_failed_message(std::wstring_view saved_path) {
    std::wstring message = kSavedPrefix;
    std::wstring const filename = Filename_from_path(saved_path);
    message += filename.empty() ? std::wstring(kSavedFallbackFilename) : filename;
    message += L", but its path could not be copied to the clipboard.";
    return message;
}

} // namespace greenflame::core
