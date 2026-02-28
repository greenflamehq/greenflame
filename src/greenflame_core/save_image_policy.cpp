#include "greenflame_core/save_image_policy.h"

#include "greenflame_core/cli_options.h"

namespace greenflame::core {

namespace {
constexpr size_t kMaxWindowTitleChars = 50;

[[nodiscard]] std::wstring Pad_unsigned(unsigned value, size_t width) {
    auto s = std::to_wstring(value);
    if (s.size() >= width) return s;
    return std::wstring(width - s.size(), L'0') + s;
}

[[nodiscard]] bool Is_invalid_filename_char(wchar_t ch) noexcept {
    static constexpr std::wstring_view kInvalid = L"\\/:*?\"<>|";
    return static_cast<unsigned>(ch) < 0x20u ||
           kInvalid.find(ch) != std::wstring_view::npos;
}

[[nodiscard]] bool Equals_no_case(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

[[nodiscard]] bool Ends_with_no_case(std::wstring_view s,
                                     std::wstring_view suffix) noexcept {
    if (s.size() < suffix.size()) return false;
    size_t const offset = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        wchar_t const a = static_cast<wchar_t>(std::towlower(s[offset + i]));
        wchar_t const b = static_cast<wchar_t>(std::towlower(suffix[i]));
        if (a != b) return false;
    }
    return true;
}

[[nodiscard]] bool Is_known_variable(std::wstring_view name) noexcept {
    return name == L"YYYY" || name == L"YY" || name == L"MM" || name == L"DD" ||
           name == L"hh" || name == L"mm" || name == L"ss" || name == L"title" ||
           name == L"monitor" || name == L"num";
}

[[nodiscard]] std::wstring Resolve_variable(std::wstring_view name,
                                            FilenamePatternContext const &ctx) {
    auto const &ts = ctx.timestamp;
    if (name == L"YYYY") {
        return Pad_unsigned(ts.year, 4);
    }
    if (name == L"YY") {
        return Pad_unsigned(ts.year % 100, 2);
    }
    if (name == L"MM") {
        return Pad_unsigned(ts.month, 2);
    }
    if (name == L"DD") {
        return Pad_unsigned(ts.day, 2);
    }
    if (name == L"hh") {
        return Pad_unsigned(ts.hour, 2);
    }
    if (name == L"mm") {
        return Pad_unsigned(ts.minute, 2);
    }
    if (name == L"ss") {
        return Pad_unsigned(ts.second, 2);
    }
    if (name == L"title") {
        if (ctx.window_title.empty()) return L"window";
        std::wstring sanitized =
            Sanitize_filename_segment(ctx.window_title, kMaxWindowTitleChars);
        return sanitized.empty() ? std::wstring(L"window") : sanitized;
    }
    if (name == L"monitor") {
        if (!ctx.monitor_index_zero_based.has_value()) return {};
        return std::to_wstring(*ctx.monitor_index_zero_based + 1);
    }
    if (name == L"num") {
        return Pad_unsigned(ctx.incrementing_number, 6);
    }
    return {};
}

} // namespace

std::wstring Sanitize_filename_segment(std::wstring_view input, size_t max_chars) {
    if (max_chars == 0) return L"";
    size_t const out_size = input.size() < max_chars ? input.size() : max_chars;
    std::wstring out;
    out.reserve(out_size);
    for (size_t i = 0; i < out_size; ++i) {
        wchar_t const ch = input[i];
        out.push_back(Is_invalid_filename_char(ch) || std::iswspace(ch) ? L'_' : ch);
    }
    return out;
}

std::wstring Expand_filename_pattern(std::wstring_view pattern,
                                     FilenamePatternContext const &ctx) {
    std::wstring result;
    result.reserve(pattern.size() + 32);
    size_t i = 0;
    while (i < pattern.size()) {
        if (i + 1 < pattern.size() && pattern[i] == L'$' && pattern[i + 1] == L'{') {
            size_t const close = pattern.find(L'}', i + 2);
            if (close == std::wstring_view::npos) {
                result.append(pattern.substr(i));
                break;
            }
            std::wstring_view var_name = pattern.substr(i + 2, close - (i + 2));
            if (Is_known_variable(var_name)) {
                result.append(Resolve_variable(var_name, ctx));
            } else {
                result.append(pattern.substr(i, close - i + 1));
            }
            i = close + 1;
        } else {
            result.push_back(pattern[i]);
            ++i;
        }
    }
    return result;
}

bool Pattern_uses_num(std::wstring_view pattern) noexcept {
    return pattern.find(L"${num}") != std::wstring_view::npos;
}

unsigned
Find_next_num_for_pattern(std::wstring_view pattern, FilenamePatternContext const &ctx,
                          std::vector<std::wstring> const &existing_filenames) {
    if (!Pattern_uses_num(pattern)) return 1;

    static constexpr std::wstring_view kExtensions[] = {L".png", L".jpg", L".jpeg",
                                                        L".bmp"};
    FilenamePatternContext trial_ctx = ctx;
    for (unsigned num = 1;; ++num) {
        trial_ctx.incrementing_number = num;
        std::wstring const candidate = Expand_filename_pattern(pattern, trial_ctx);
        bool found = false;
        for (std::wstring const &existing : existing_filenames) {
            for (std::wstring_view ext : kExtensions) {
                std::wstring with_ext = candidate;
                with_ext += ext;
                if (Equals_no_case(existing, with_ext)) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return num;
    }
}

std::wstring_view Default_filename_pattern(SaveSelectionSource source) {
    switch (source) {
    case SaveSelectionSource::Region:
        return kDefaultPatternRegion;
    case SaveSelectionSource::Desktop:
        return kDefaultPatternDesktop;
    case SaveSelectionSource::Monitor:
        return kDefaultPatternMonitor;
    case SaveSelectionSource::Window:
        return kDefaultPatternWindow;
    }
    return kDefaultPatternRegion;
}

std::wstring Build_default_save_name(SaveSelectionSource selection_source,
                                     FilenamePatternContext const &context,
                                     std::wstring_view pattern_override) {
    std::wstring_view const pattern = pattern_override.empty()
                                          ? Default_filename_pattern(selection_source)
                                          : pattern_override;
    return Expand_filename_pattern(pattern, context);
}

std::wstring Ensure_image_save_extension(std::wstring_view path,
                                         uint32_t filter_index_1_based) {
    if (Ends_with_no_case(path, L".png") || Ends_with_no_case(path, L".jpg") ||
        Ends_with_no_case(path, L".jpeg") || Ends_with_no_case(path, L".bmp")) {
        return std::wstring(path);
    }
    std::wstring out(path);
    if (filter_index_1_based == 2) {
        out += L".jpg";
    } else if (filter_index_1_based == 3) {
        out += L".bmp";
    } else {
        out += L".png";
    }
    return out;
}

ImageSaveFormat Image_save_format_from_cli_format(CliOutputFormat format) noexcept {
    switch (format) {
    case CliOutputFormat::Jpeg:
        return ImageSaveFormat::Jpeg;
    case CliOutputFormat::Bmp:
        return ImageSaveFormat::Bmp;
    case CliOutputFormat::Png:
        return ImageSaveFormat::Png;
    }
    return ImageSaveFormat::Png;
}

std::wstring_view Extension_for_image_save_format(ImageSaveFormat format) noexcept {
    switch (format) {
    case ImageSaveFormat::Jpeg:
        return L".jpg";
    case ImageSaveFormat::Bmp:
        return L".bmp";
    case ImageSaveFormat::Png:
        return L".png";
    }
    return L".png";
}

std::wstring_view Name_for_image_save_format(ImageSaveFormat format) noexcept {
    switch (format) {
    case ImageSaveFormat::Jpeg:
        return L"jpg";
    case ImageSaveFormat::Bmp:
        return L"bmp";
    case ImageSaveFormat::Png:
        return L"png";
    }
    return L"png";
}

ImageSaveFormat Detect_image_save_format_from_path(std::wstring_view path) noexcept {
    if (Ends_with_no_case(path, L".jpg") || Ends_with_no_case(path, L".jpeg")) {
        return ImageSaveFormat::Jpeg;
    }
    if (Ends_with_no_case(path, L".bmp")) return ImageSaveFormat::Bmp;
    return ImageSaveFormat::Png;
}

} // namespace greenflame::core
