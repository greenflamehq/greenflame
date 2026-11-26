#pragma once

namespace greenflame::core {

[[nodiscard]] bool Contains_no_case(std::wstring_view text,
                                    std::wstring_view needle) noexcept;

[[nodiscard]] bool Equals_no_case(std::wstring_view a, std::wstring_view b) noexcept;

[[nodiscard]] std::wstring Filename_from_path(std::wstring_view path);

[[nodiscard]] std::wstring
Build_saved_selection_balloon_message(std::wstring_view saved_path,
                                      bool file_copied_to_clipboard);

// For a save whose file-path copy to the clipboard failed.
[[nodiscard]] std::wstring
Build_saved_selection_path_copy_failed_message(std::wstring_view saved_path);

} // namespace greenflame::core
