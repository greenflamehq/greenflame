#include "greenflame_core/cli_options.h"
#include "greenflame_core/selection_wheel.h"

namespace greenflame::core {

namespace {

enum class CliOptionId : uint8_t {
    Region = 0,
    Window = 1,
    WindowHwnd = 2,
    Monitor = 3,
    Desktop = 4,
    Input = 5,
    Help = 6,
    Version = 7,
    Output = 8,
    Format = 9,
    Padding = 10,
    PaddingColor = 11,
    Annotate = 12,
    WindowCapture = 13,
    Cursor = 14,
    NoCursor = 15,
    Overwrite = 16,
#ifdef DEBUG
    Testing12 = 17,
#endif
};

enum class CliOptionValueKind : uint8_t {
    None = 0,
    Region = 1,
    String = 2,
    Integer = 3,
    Path = 4,
};

enum class CliOptionGroup : uint8_t {
    Exclusive = 0,
    Optional = 1,
    Debug = 2,
};

struct CliOptionSpec final {
    wchar_t const *long_name = nullptr;
    wchar_t const *value_placeholder = nullptr;
    wchar_t const *description = nullptr;
    wchar_t short_name = L'\0';
    CliOptionId id = CliOptionId::Help;
    CliOptionValueKind value_kind = CliOptionValueKind::None;
    CliOptionGroup group = CliOptionGroup::Optional;
    bool debug_only = false;
};

constexpr CliOptionSpec kCliOptionSpecs[] = {
    {
        L"region",
        L"<x,y,w,h>",
        L"Capture an explicit physical-pixel region.",
        L'r',
        CliOptionId::Region,
        CliOptionValueKind::Region,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"window",
        L"<name>",
        L"Capture a visible top-level window by title text. A unique exact-title "
        L"match wins over broader substring matches.",
        L'w',
        CliOptionId::Window,
        CliOptionValueKind::String,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"window-hwnd",
        L"<hex>",
        L"Capture a visible top-level window by exact HWND in hex.",
        L'\0',
        CliOptionId::WindowHwnd,
        CliOptionValueKind::String,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"monitor",
        L"<id>",
        L"Capture a monitor by 1-based monitor id.",
        L'm',
        CliOptionId::Monitor,
        CliOptionValueKind::Integer,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"desktop",
        nullptr,
        L"Capture the full virtual desktop.",
        L'd',
        CliOptionId::Desktop,
        CliOptionValueKind::None,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"input",
        L"<path>",
        L"Load an existing PNG/JPEG/BMP image, apply --annotate, and save the "
        L"result. Requires --annotate and either --output or --overwrite.",
        L'\0',
        CliOptionId::Input,
        CliOptionValueKind::Path,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"help",
        nullptr,
        L"Display help and exit.",
        L'h',
        CliOptionId::Help,
        CliOptionValueKind::None,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"version",
        nullptr,
        L"Display version and exit.",
        L'v',
        CliOptionId::Version,
        CliOptionValueKind::None,
        CliOptionGroup::Exclusive,
        false,
    },
    {
        L"output",
        L"<path>",
        L"Output file path. Valid only with a live capture source or --input.",
        L'o',
        CliOptionId::Output,
        CliOptionValueKind::Path,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"format",
        L"<png|jpg|bmp>",
        L"Output image format. Accepts png, jpg/jpeg, or bmp. Valid only with "
        L"a live capture source or --input.",
        L't',
        CliOptionId::Format,
        CliOptionValueKind::String,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"padding",
        L"<n|h,v|l,t,r,b>",
        L"Add synthetic padding around the rendered image in physical pixels.",
        L'p',
        CliOptionId::Padding,
        CliOptionValueKind::String,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"padding-color",
        L"<#rrggbb>",
        L"Override the padding color for this invocation only. Valid only with "
        L"--padding.",
        L'\0',
        CliOptionId::PaddingColor,
        CliOptionValueKind::String,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"annotate",
        L"<json|path>",
        L"Apply JSON-defined annotations to the saved CLI render result.",
        L'\0',
        CliOptionId::Annotate,
        CliOptionValueKind::String,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"window-capture",
        L"<auto|gdi|wgc>",
        L"CLI-only window capture backend. Valid only with --window or "
        L"--window-hwnd. Defaults to auto.",
        L'\0',
        CliOptionId::WindowCapture,
        CliOptionValueKind::String,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"cursor",
        nullptr,
        L"Include the captured cursor for this live-capture invocation only.",
        L'\0',
        CliOptionId::Cursor,
        CliOptionValueKind::None,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"no-cursor",
        nullptr,
        L"Exclude the captured cursor for this live-capture invocation only.",
        L'\0',
        CliOptionId::NoCursor,
        CliOptionValueKind::None,
        CliOptionGroup::Optional,
        false,
    },
    {
        L"overwrite",
        nullptr,
        L"Allow replacing an existing explicit --output path.",
        L'f',
        CliOptionId::Overwrite,
        CliOptionValueKind::None,
        CliOptionGroup::Optional,
        false,
    },
#ifdef DEBUG
    {
        L"testing-1-2",
        nullptr,
        L"Enable debug-only testing hotkeys.",
        L'\0',
        CliOptionId::Testing12,
        CliOptionValueKind::None,
        CliOptionGroup::Debug,
        true,
    },
#endif
};

[[nodiscard]] std::wstring_view Trim_wspace(std::wstring_view text) noexcept {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(text[begin]) != 0) {
        ++begin;
    }
    while (end > begin && std::iswspace(text[end - 1]) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

[[nodiscard]] bool Try_parse_int32(std::wstring_view text, int32_t &out) noexcept {
    text = Trim_wspace(text);
    if (text.empty()) {
        return false;
    }

    bool negative = false;
    size_t i = 0;
    if (text[i] == L'+' || text[i] == L'-') {
        negative = text[i] == L'-';
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }

    int64_t value = 0;
    for (; i < text.size(); ++i) {
        wchar_t const ch = text[i];
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        value = value * 10 + static_cast<int64_t>(ch - L'0');
        if (!negative && value > static_cast<int64_t>(INT32_MAX)) {
            return false;
        }
        if (negative &&
            value > static_cast<int64_t>(INT32_MAX) + static_cast<int64_t>(1)) {
            return false;
        }
    }

    if (negative) {
        if (value == static_cast<int64_t>(INT32_MAX) + static_cast<int64_t>(1)) {
            out = INT32_MIN;
        } else {
            out = -static_cast<int32_t>(value);
        }
        return true;
    }
    out = static_cast<int32_t>(value);
    return true;
}

[[nodiscard]] bool Try_parse_region(std::wstring_view value, RectPx &region) noexcept {
    std::array<std::wstring_view, 4> parts = {};
    size_t part_count = 0;
    size_t segment_start = 0;

    for (size_t i = 0; i <= value.size(); ++i) {
        if (i != value.size() && value[i] != L',') {
            continue;
        }
        if (part_count >= 4) {
            return false;
        }
        parts[part_count++] =
            Trim_wspace(value.substr(segment_start, i - segment_start));
        segment_start = i + 1;
    }

    if (part_count != 4) {
        return false;
    }

    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
    if (!Try_parse_int32(parts[0], x) || !Try_parse_int32(parts[1], y) ||
        !Try_parse_int32(parts[2], w) || !Try_parse_int32(parts[3], h)) {
        return false;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0) {
        return false;
    }

    int64_t const right64 = static_cast<int64_t>(x) + static_cast<int64_t>(w);
    int64_t const bottom64 = static_cast<int64_t>(y) + static_cast<int64_t>(h);
    if (right64 < static_cast<int64_t>(INT32_MIN) ||
        right64 > static_cast<int64_t>(INT32_MAX) ||
        bottom64 < static_cast<int64_t>(INT32_MIN) ||
        bottom64 > static_cast<int64_t>(INT32_MAX)) {
        return false;
    }

    region = RectPx::From_ltrb(x, y, static_cast<int32_t>(right64),
                               static_cast<int32_t>(bottom64));
    return true;
}

[[nodiscard]] bool Try_parse_padding(std::wstring_view value,
                                     InsetsPx &padding) noexcept {
    std::array<std::wstring_view, 4> parts = {};
    size_t part_count = 0;
    size_t segment_start = 0;

    for (size_t i = 0; i <= value.size(); ++i) {
        if (i != value.size() && value[i] != L',') {
            continue;
        }
        if (part_count >= parts.size()) {
            return false;
        }
        parts[part_count++] =
            Trim_wspace(value.substr(segment_start, i - segment_start));
        segment_start = i + 1;
    }

    if (part_count != 1 && part_count != 2 && part_count != 4) {
        return false;
    }

    std::array<int32_t, 4> parsed = {};
    for (size_t i = 0; i < part_count; ++i) {
        if (!Try_parse_int32(parts[i], parsed[i]) || parsed[i] < 0) {
            return false;
        }
    }

    if (part_count == 1) {
        if (parsed[0] <= 0) {
            return false;
        }
        padding = InsetsPx{parsed[0], parsed[0], parsed[0], parsed[0]};
        return true;
    }

    if (part_count == 2) {
        if (parsed[0] == 0 && parsed[1] == 0) {
            return false;
        }
        padding = InsetsPx{parsed[0], parsed[1], parsed[0], parsed[1]};
        return true;
    }

    if (parsed[0] == 0 && parsed[1] == 0 && parsed[2] == 0 && parsed[3] == 0) {
        return false;
    }
    padding = InsetsPx{parsed[0], parsed[1], parsed[2], parsed[3]};
    return true;
}

[[nodiscard]] bool Try_parse_hex_digit(wchar_t ch, uint8_t &value) noexcept {
    if (ch >= L'0' && ch <= L'9') {
        value = static_cast<uint8_t>(ch - L'0');
        return true;
    }
    if (ch >= L'a' && ch <= L'f') {
        value = static_cast<uint8_t>(10 + (ch - L'a'));
        return true;
    }
    if (ch >= L'A' && ch <= L'F') {
        value = static_cast<uint8_t>(10 + (ch - L'A'));
        return true;
    }
    return false;
}

[[nodiscard]] bool Try_parse_hex_byte(std::wstring_view value, uint8_t &out) noexcept {
    if (value.size() != 2) {
        return false;
    }
    uint8_t high = 0;
    uint8_t low = 0;
    if (!Try_parse_hex_digit(value[0], high) || !Try_parse_hex_digit(value[1], low)) {
        return false;
    }
    out = static_cast<uint8_t>((high << 4u) | low);
    return true;
}

[[nodiscard]] bool Try_parse_hex_uintptr(std::wstring_view value,
                                         std::uintptr_t &out) noexcept {
    std::wstring_view const trimmed = Trim_wspace(value);
    if (trimmed.empty()) {
        return false;
    }

    std::wstring_view digits = trimmed;
    if (digits.size() >= 2 && digits[0] == L'0' &&
        (digits[1] == L'x' || digits[1] == L'X')) {
        digits.remove_prefix(2);
    }
    if (digits.empty()) {
        return false;
    }

    std::uintptr_t parsed = 0;
    constexpr std::uintptr_t k_max_before_shift =
        (std::numeric_limits<std::uintptr_t>::max)() >> 4u;
    for (wchar_t const ch : digits) {
        uint8_t nibble = 0;
        if (!Try_parse_hex_digit(ch, nibble) || parsed > k_max_before_shift) {
            return false;
        }
        parsed = static_cast<std::uintptr_t>((parsed << 4u) |
                                             static_cast<std::uintptr_t>(nibble));
    }
    if (parsed == 0) {
        return false;
    }

    out = parsed;
    return true;
}

[[nodiscard]] bool Try_parse_padding_color(std::wstring_view value,
                                           COLORREF &color) noexcept {
    constexpr size_t k_hex_color_length = 7;
    constexpr size_t k_red_offset = 1;
    constexpr size_t k_green_offset = 3;
    constexpr size_t k_blue_offset = 5;
    constexpr size_t k_hex_byte_length = 2;

    std::wstring_view const trimmed = Trim_wspace(value);
    if (trimmed.size() != k_hex_color_length || trimmed[0] != L'#') {
        return false;
    }

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (!Try_parse_hex_byte(trimmed.substr(k_red_offset, k_hex_byte_length), red) ||
        !Try_parse_hex_byte(trimmed.substr(k_green_offset, k_hex_byte_length), green) ||
        !Try_parse_hex_byte(trimmed.substr(k_blue_offset, k_hex_byte_length), blue)) {
        return false;
    }

    color = Make_colorref(red, green, blue);
    return true;
}

[[nodiscard]] bool Try_parse_output_format(std::wstring_view value,
                                           CliOutputFormat &format) noexcept {
    std::wstring_view const trimmed = Trim_wspace(value);
    if (trimmed.empty()) {
        return false;
    }

    std::wstring lower;
    lower.reserve(trimmed.size());
    for (wchar_t const ch : trimmed) {
        lower.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }

    if (lower == L"png") {
        format = CliOutputFormat::Png;
        return true;
    }
    if (lower == L"jpg" || lower == L"jpeg") {
        format = CliOutputFormat::Jpeg;
        return true;
    }
    if (lower == L"bmp") {
        format = CliOutputFormat::Bmp;
        return true;
    }
    return false;
}

[[nodiscard]] bool
Try_parse_window_capture_backend(std::wstring_view value,
                                 WindowCaptureBackend &backend) noexcept {
    std::wstring_view const trimmed = Trim_wspace(value);
    if (trimmed.empty()) {
        return false;
    }

    std::wstring lower;
    lower.reserve(trimmed.size());
    for (wchar_t const ch : trimmed) {
        lower.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }

    if (lower == L"auto") {
        backend = WindowCaptureBackend::Auto;
        return true;
    }
    if (lower == L"gdi") {
        backend = WindowCaptureBackend::Gdi;
        return true;
    }
    if (lower == L"wgc") {
        backend = WindowCaptureBackend::Wgc;
        return true;
    }
    return false;
}

[[nodiscard]] std::wstring Make_option_display_name(CliOptionSpec const &spec) {
    std::wstring out;
    if (spec.short_name != L'\0') {
        out += L"-";
        out.push_back(spec.short_name);
    }
    if (spec.long_name != nullptr) {
        if (!out.empty()) {
            out += L", ";
        }
        out += L"--";
        out += spec.long_name;
    }
    if (spec.value_placeholder != nullptr) {
        out += L" ";
        out += spec.value_placeholder;
    }
    return out;
}

[[nodiscard]] std::wstring Group_header(CliOptionGroup group) {
    switch (group) {
    case CliOptionGroup::Exclusive:
        return L"Mode (Mutually Exclusive):";
    case CliOptionGroup::Optional:
        return L"Options:";
    case CliOptionGroup::Debug:
        return L"Debug Options:";
    }
    return L"Options:";
}

[[nodiscard]] CliParseResult Make_error(std::wstring const &message) {
    CliParseResult result{};
    result.ok = false;
    result.error_message = message;
    return result;
}

[[nodiscard]] CliOptionSpec const *Find_option_by_long_name(std::wstring_view name,
                                                            bool debug_build) noexcept {
    for (CliOptionSpec const &spec : kCliOptionSpecs) {
        if (spec.long_name == nullptr) {
            continue;
        }
        if (spec.debug_only && !debug_build) {
            continue;
        }
        if (name == spec.long_name) {
            return &spec;
        }
    }
    return nullptr;
}

[[nodiscard]] CliOptionSpec const *
Find_option_by_short_name(wchar_t name, bool debug_build) noexcept {
    for (CliOptionSpec const &spec : kCliOptionSpecs) {
        if (spec.short_name == L'\0') {
            continue;
        }
        if (spec.debug_only && !debug_build) {
            continue;
        }
        if (name == spec.short_name) {
            return &spec;
        }
    }
    return nullptr;
}

[[nodiscard]] bool Has_exclusive_mode(CliOptions const &options) {
    return Has_cli_render_source(options) || options.action != CliAction::None;
}

[[nodiscard]] bool Try_set_capture_mode(CliOptions &options, CliCaptureMode mode,
                                        std::wstring &error_message) {
    if (Has_exclusive_mode(options)) {
        error_message = L"Only one mode can be specified per invocation.";
        return false;
    }
    options.capture_mode = mode;
    return true;
}

[[nodiscard]] bool Try_set_action(CliOptions &options, CliAction action,
                                  std::wstring &error_message) {
    if (Has_exclusive_mode(options)) {
        error_message = L"Only one mode can be specified per invocation.";
        return false;
    }
    options.action = action;
    return true;
}

[[nodiscard]] bool Try_set_help_action(CliOptions &options, CliHelpTopic topic,
                                       std::wstring &error_message) {
    if (!Try_set_action(options, CliAction::Help, error_message)) {
        return false;
    }
    options.help_topic = topic;
    return true;
}

[[nodiscard]] bool Try_parse_help_topic(std::wstring_view value,
                                        CliHelpTopic &topic) {
    std::wstring_view const trimmed = Trim_wspace(value);
    std::wstring lower;
    lower.reserve(trimmed.size());
    for (wchar_t const ch : trimmed) {
        lower.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }

    if (lower.empty() || lower == L"main") {
        topic = CliHelpTopic::Main;
        return true;
    }
    if (lower == L"agent") {
        topic = CliHelpTopic::Agent;
        return true;
    }
    if (lower == L"annotate" || lower == L"annotations") {
        topic = CliHelpTopic::Annotate;
        return true;
    }
    if (lower == L"annotation-types" || lower == L"types") {
        topic = CliHelpTopic::AnnotationTypes;
        return true;
    }
    if (lower == L"windows" || lower == L"window") {
        topic = CliHelpTopic::Windows;
        return true;
    }
    if (lower == L"exits" || lower == L"exit-codes") {
        topic = CliHelpTopic::Exits;
        return true;
    }
    return false;
}

[[nodiscard]] CliParseResult Apply_option(CliOptions &options,
                                          CliOptionSpec const &spec,
                                          std::wstring const &value) {
    std::wstring error_message;
    switch (spec.id) {
    case CliOptionId::Region: {
        RectPx region{};
        if (!Try_parse_region(value, region)) {
            return Make_error(L"--region expects one value x,y,w,h with x>=0, y>=0, "
                              L"w>0, and h>0.");
        }
        if (!Try_set_capture_mode(options, CliCaptureMode::Region, error_message)) {
            return Make_error(error_message);
        }
        options.region_px = region;
        return CliParseResult{{}, options, true};
    }
    case CliOptionId::Window:
        if (value.empty()) {
            return Make_error(L"--window expects a non-empty name.");
        }
        if (!Try_set_capture_mode(options, CliCaptureMode::Window, error_message)) {
            return Make_error(error_message);
        }
        options.window_name = value;
        return CliParseResult{{}, options, true};
    case CliOptionId::WindowHwnd: {
        std::uintptr_t hwnd = 0;
        if (!Try_parse_hex_uintptr(value, hwnd)) {
            return Make_error(L"--window-hwnd expects a non-zero hex HWND, optionally "
                              L"prefixed with 0x.");
        }
        if (!Try_set_capture_mode(options, CliCaptureMode::Window, error_message)) {
            return Make_error(error_message);
        }
        options.window_hwnd = hwnd;
        return CliParseResult{{}, options, true};
    }
    case CliOptionId::Monitor: {
        int32_t monitor_id = 0;
        if (!Try_parse_int32(value, monitor_id) || monitor_id < 1) {
            return Make_error(L"--monitor expects an integer id >= 1.");
        }
        if (!Try_set_capture_mode(options, CliCaptureMode::Monitor, error_message)) {
            return Make_error(error_message);
        }
        options.monitor_id = monitor_id;
        return CliParseResult{{}, options, true};
    }
    case CliOptionId::Desktop:
        if (!Try_set_capture_mode(options, CliCaptureMode::Desktop, error_message)) {
            return Make_error(error_message);
        }
        return CliParseResult{{}, options, true};
    case CliOptionId::Input:
        if (value.empty()) {
            return Make_error(L"--input expects a non-empty path.");
        }
        if (Has_exclusive_mode(options)) {
            return Make_error(L"Only one mode can be specified per invocation.");
        }
        options.input_path = value;
        return CliParseResult{{}, options, true};
    case CliOptionId::Help:
        if (!Try_set_help_action(options, CliHelpTopic::Main, error_message)) {
            return Make_error(error_message);
        }
        return CliParseResult{{}, options, true};
    case CliOptionId::Version:
        if (!Try_set_action(options, CliAction::Version, error_message)) {
            return Make_error(error_message);
        }
        return CliParseResult{{}, options, true};
    case CliOptionId::Output:
        if (value.empty()) {
            return Make_error(L"--output expects a non-empty path.");
        }
        if (!options.output_path.empty()) {
            return Make_error(L"--output can only be specified once.");
        }
        options.output_path = value;
        return CliParseResult{{}, options, true};
    case CliOptionId::Format: {
        CliOutputFormat format = CliOutputFormat::Png;
        if (!Try_parse_output_format(value, format)) {
            return Make_error(L"--format expects one of: png, jpg/jpeg, or bmp.");
        }
        if (options.output_format.has_value()) {
            return Make_error(L"--format can only be specified once.");
        }
        options.output_format = format;
        return CliParseResult{{}, options, true};
    }
    case CliOptionId::Padding: {
        InsetsPx padding{};
        if (!Try_parse_padding(value, padding)) {
            return Make_error(L"--padding expects one value n>0, two values h,v "
                              L"with h>=0 and v>=0 and at least one >0, or "
                              L"four values l,t,r,b with each >=0 and at least "
                              L"one >0.");
        }
        if (options.padding_px.has_value()) {
            return Make_error(L"--padding can only be specified once.");
        }
        options.padding_px = padding;
        return CliParseResult{{}, options, true};
    }
    case CliOptionId::PaddingColor: {
        COLORREF color = static_cast<COLORREF>(0);
        if (!Try_parse_padding_color(value, color)) {
            return Make_error(L"--padding-color expects a color value in the form "
                              L"#rrggbb.");
        }
        if (options.padding_color_override.has_value()) {
            return Make_error(L"--padding-color can only be specified once.");
        }
        options.padding_color_override = color;
        return CliParseResult{{}, options, true};
    }
    case CliOptionId::Annotate:
        if (Trim_wspace(value).empty()) {
            return Make_error(L"--annotate expects a non-empty JSON string or path.");
        }
        if (options.annotate_value.has_value()) {
            return Make_error(L"--annotate can only be specified once.");
        }
        options.annotate_value = value;
        return CliParseResult{{}, options, true};
    case CliOptionId::WindowCapture: {
        WindowCaptureBackend backend = WindowCaptureBackend::Auto;
        if (!Try_parse_window_capture_backend(value, backend)) {
            return Make_error(L"--window-capture expects one of: auto, gdi, or wgc.");
        }
        if (options.window_capture_backend_explicit) {
            return Make_error(L"--window-capture can only be specified once.");
        }
        options.window_capture_backend = backend;
        options.window_capture_backend_explicit = true;
        return CliParseResult{{}, options, true};
    }
    case CliOptionId::Cursor:
        if (options.cursor_override == CliCursorOverride::ForceInclude) {
            return Make_error(L"--cursor can only be specified once.");
        }
        if (options.cursor_override == CliCursorOverride::ForceExclude) {
            return Make_error(L"--cursor and --no-cursor are mutually exclusive.");
        }
        options.cursor_override = CliCursorOverride::ForceInclude;
        return CliParseResult{{}, options, true};
    case CliOptionId::NoCursor:
        if (options.cursor_override == CliCursorOverride::ForceExclude) {
            return Make_error(L"--no-cursor can only be specified once.");
        }
        if (options.cursor_override == CliCursorOverride::ForceInclude) {
            return Make_error(L"--cursor and --no-cursor are mutually exclusive.");
        }
        options.cursor_override = CliCursorOverride::ForceExclude;
        return CliParseResult{{}, options, true};
    case CliOptionId::Overwrite:
        options.overwrite_output = true;
        return CliParseResult{{}, options, true};
#ifdef DEBUG
    case CliOptionId::Testing12:
        options.testing_1_2 = true;
        return CliParseResult{{}, options, true};
#endif
    }
    return Make_error(L"Internal CLI parser error.");
}

[[nodiscard]] CliParseResult Validate_cli_options(CliOptions const &options) {
    if (!options.output_path.empty() && !Has_cli_render_source(options)) {
        return Make_error(L"--output requires one render source: --region, --window, "
                          L"--window-hwnd, --monitor, --desktop, or --input.");
    }
    if (options.output_format.has_value() && !Has_cli_render_source(options)) {
        return Make_error(L"--format requires one render source: --region, --window, "
                          L"--window-hwnd, --monitor, --desktop, or --input.");
    }
    if (options.padding_px.has_value() && !Has_cli_render_source(options)) {
        return Make_error(L"--padding requires one render source: --region, --window, "
                          L"--window-hwnd, --monitor, --desktop, or --input.");
    }
    if (options.cursor_override != CliCursorOverride::UseConfig &&
        !options.input_path.empty()) {
        return Make_error(L"--cursor and --no-cursor cannot be used with --input.");
    }
    if (options.cursor_override != CliCursorOverride::UseConfig &&
        !Is_capture_mode(options.capture_mode)) {
        return Make_error(L"--cursor and --no-cursor require one live capture source: "
                          L"--region, --window, --window-hwnd, --monitor, or "
                          L"--desktop.");
    }
    if (options.annotate_value.has_value() && !Has_cli_render_source(options)) {
        return Make_error(L"--annotate requires one render source: --region, --window, "
                          L"--window-hwnd, --monitor, --desktop, or --input.");
    }
    if (!options.input_path.empty() && !options.annotate_value.has_value()) {
        return Make_error(L"--input requires --annotate.");
    }
    if (!options.input_path.empty() && options.output_path.empty() &&
        !options.overwrite_output) {
        return Make_error(L"--input requires either --output or --overwrite.");
    }
    if (options.window_capture_backend_explicit &&
        options.capture_mode != CliCaptureMode::Window) {
        return Make_error(L"--window-capture requires --window or --window-hwnd.");
    }
    if (options.padding_color_override.has_value() && !options.padding_px.has_value()) {
        return Make_error(L"--padding-color requires --padding.");
    }
    if (options.capture_mode == CliCaptureMode::Region &&
        !options.region_px.has_value()) {
        return Make_error(L"--region value is missing.");
    }
    if (options.capture_mode == CliCaptureMode::Window && options.window_name.empty() &&
        !options.window_hwnd.has_value()) {
        return Make_error(L"--window or --window-hwnd value is missing.");
    }
    if (options.capture_mode == CliCaptureMode::Monitor && options.monitor_id <= 0) {
        return Make_error(L"--monitor value is missing.");
    }
    return CliParseResult{{}, options, true};
}

[[nodiscard]] CliParseResult Parse_option(CliOptions &options,
                                          CliOptionSpec const &spec,
                                          std::optional<std::wstring> const &value) {
    if (spec.value_kind == CliOptionValueKind::None) {
        if (value.has_value()) {
            std::wstring message = L"Option does not accept a value: --";
            message += spec.long_name;
            return Make_error(message);
        }
        return Apply_option(options, spec, L"");
    }

    if (!value.has_value()) {
        std::wstring message = L"Missing value for option: --";
        message += spec.long_name;
        return Make_error(message);
    }
    return Apply_option(options, spec, *value);
}

[[nodiscard]] CliParseResult Parse_long_option(CliOptions &options,
                                               std::wstring const &arg, size_t &index,
                                               std::vector<std::wstring> const &args,
                                               bool debug_build) {
    if (arg == L"--") {
        return Make_error(L"Unexpected positional arguments are not supported.");
    }

    size_t const equals = arg.find(L'=');
    std::wstring_view name_view = equals == std::wstring::npos
                                      ? std::wstring_view(arg).substr(2)
                                      : std::wstring_view(arg).substr(2, equals - 2);
    if (name_view.empty()) {
        return Make_error(L"Invalid long option syntax.");
    }

    CliOptionSpec const *spec = Find_option_by_long_name(name_view, debug_build);
    if (spec == nullptr) {
        CliOptionSpec const *all_build_spec = Find_option_by_long_name(name_view, true);
        if (all_build_spec != nullptr && all_build_spec->debug_only && !debug_build) {
            std::wstring message = L"Option is only available in debug builds: --";
            message += std::wstring(name_view);
            return Make_error(message);
        }
        std::wstring message = L"Unknown option: --";
        message += std::wstring(name_view);
        return Make_error(message);
    }

    std::optional<std::wstring> value = std::nullopt;
    if (equals != std::wstring::npos) {
        value = arg.substr(equals + 1);
    } else if (spec->value_kind != CliOptionValueKind::None) {
        size_t const next_index = index + 1;
        if (next_index >= args.size()) {
            std::wstring message = L"Missing value for option: --";
            message += spec->long_name;
            return Make_error(message);
        }
        value = args[next_index];
        index = next_index;
    }

    return Parse_option(options, *spec, value);
}

[[nodiscard]] CliParseResult Parse_short_option(CliOptions &options,
                                                std::wstring const &arg, size_t &index,
                                                std::vector<std::wstring> const &args,
                                                bool debug_build) {
    if (arg.size() < 2) {
        return Make_error(L"Invalid short option syntax.");
    }

    wchar_t const option_name = arg[1];
    CliOptionSpec const *spec = Find_option_by_short_name(option_name, debug_build);
    if (spec == nullptr) {
        std::wstring message = L"Unknown option: -";
        message.push_back(option_name);
        return Make_error(message);
    }

    std::optional<std::wstring> value = std::nullopt;
    if (arg.size() > 2) {
        if (arg[2] == L'=') {
            value = arg.substr(3);
        } else {
            if (spec->value_kind == CliOptionValueKind::None) {
                std::wstring message = L"Option does not accept a value: -";
                message.push_back(option_name);
                return Make_error(message);
            }
            value = arg.substr(2);
        }
    } else if (spec->value_kind != CliOptionValueKind::None) {
        size_t const next_index = index + 1;
        if (next_index >= args.size()) {
            std::wstring message = L"Missing value for option: -";
            message.push_back(option_name);
            return Make_error(message);
        }
        value = args[next_index];
        index = next_index;
    }

    return Parse_option(options, *spec, value);
}

void Append_help_group(std::wstring &help_text, CliOptionGroup group,
                       bool debug_build) {
    constexpr size_t k_help_option_name_column_width = 28;
    bool any_in_group = false;
    for (CliOptionSpec const &spec : kCliOptionSpecs) {
        if (spec.group != group) {
            continue;
        }
        if (spec.debug_only && !debug_build) {
            continue;
        }
        if (!any_in_group) {
            help_text += Group_header(group);
            help_text += L"\n";
            any_in_group = true;
        }
        std::wstring const option_name = Make_option_display_name(spec);
        help_text += L"  ";
        help_text += option_name;
        if (option_name.size() < k_help_option_name_column_width) {
            help_text.append(k_help_option_name_column_width - option_name.size(),
                             L' ');
        } else {
            help_text += L" ";
        }
        help_text += spec.description;
        help_text += L"\n";
    }
    if (any_in_group) {
        help_text += L"\n";
    }
}

} // namespace

CliParseResult Parse_cli_arguments(std::vector<std::wstring> const &args,
                                   bool debug_build) {
    CliOptions options{};

    for (size_t i = 0; i < args.size(); ++i) {
        std::wstring const &arg = args[i];
        if (arg.size() >= 2 && arg[0] == L'-' && arg[1] == L'-') {
            CliParseResult result =
                Parse_long_option(options, arg, i, args, debug_build);
            if (!result.ok) {
                return result;
            }
            options = result.options;
            if (arg == L"--help" && i + 1 < args.size()) {
                CliHelpTopic topic = CliHelpTopic::Main;
                if (!Try_parse_help_topic(args[i + 1], topic)) {
                    std::wstring message = L"Unknown help topic: ";
                    message += args[i + 1];
                    return Make_error(message);
                }
                options.help_topic = topic;
                ++i;
            }
            continue;
        }
        if (arg.size() >= 2 && arg[0] == L'-') {
            CliParseResult result =
                Parse_short_option(options, arg, i, args, debug_build);
            if (!result.ok) {
                return result;
            }
            options = result.options;
            continue;
        }
        std::wstring message = L"Unexpected positional argument: ";
        message += arg;
        return Make_error(message);
    }

    return Validate_cli_options(options);
}

[[nodiscard]] static std::wstring Build_main_help_text(bool debug_build) {
    std::wstring help_text;
    help_text += L"Greenflame\n";
    help_text += L"Yet another Windows screenshot tool.\n";
    help_text += L"\n";
    help_text += L"Usage:\n";
    help_text += L"  greenflame [mode] [options]\n";
    help_text +=
        L"  greenflame --help [agent|annotate|annotation-types|windows|exits]\n";
    help_text += L"  greenflame --help\n";
    help_text += L"  greenflame --version\n";
    help_text += L"\n";

    Append_help_group(help_text, CliOptionGroup::Exclusive, debug_build);
    Append_help_group(help_text, CliOptionGroup::Optional, debug_build);
    Append_help_group(help_text, CliOptionGroup::Debug, debug_build);

    help_text += L"Notes:\n";
    help_text += L"  --option=value and --option value are both supported.\n";
    help_text += L"  No capture mode starts the tray app as usual.\n";
    help_text += L"  For automation workflow guidance, run: greenflame --help agent\n";
    help_text += L"\n";
    return help_text;
}

[[nodiscard]] static std::wstring Build_agent_help_text() {
    return L"Greenflame Agent Help\n"
           L"\n"
           L"Default annotation workflow:\n"
           L"  1. Capture to a file with an explicit --output path.\n"
           L"  2. Inspect that same saved image.\n"
           L"  3. Write annotation JSON to a file.\n"
           L"  4. Re-run Greenflame with --input <capture> --annotate <json> "
           L"--output <annotated>.\n"
           L"\n"
           L"Do not take a fresh screenshot for the annotation pass unless the user "
           L"asks for a new capture.\n"
           L"\n"
           L"Prefer:\n"
           L"  - explicit --output paths\n"
           L"  - PNG unless the user asks for jpg/jpeg or bmp\n"
           L"  - annotation JSON files over inline JSON except for trivial payloads\n"
           L"  - coordinate_space \"local\" for --input annotations\n"
           L"\n"
           L"More help:\n"
           L"  greenflame --help annotate  Annotation JSON and --input rules\n"
           L"  greenflame --help annotation-types  Per-type annotation fields\n"
           L"  greenflame --help windows   Window capture and ambiguity recovery\n"
           L"  greenflame --help exits     Exit codes and retry meaning\n";
}

[[nodiscard]] static std::wstring Build_annotate_help_text() {
    return L"Greenflame Annotation Help\n"
           L"\n"
           L"--annotate accepts inline JSON or a UTF-8 JSON file path.\n"
           L"--input <path> loads an existing PNG/JPEG/BMP, applies --annotate, "
           L"and saves with --output or --overwrite.\n"
           L"\n"
           L"Document shape:\n"
           L"  {\"coordinate_space\":\"local\",\"annotations\":[...]}\n"
           L"  Optional document defaults: color, size, font, "
           L"highlighter_opacity_percent.\n"
           L"\n"
           L"--input rules:\n"
           L"  - local image coordinates only\n"
           L"  - use coordinate_space \"local\"\n"
           L"  - cannot be combined with live capture modes, --window-capture, "
           L"--cursor, or --no-cursor\n"
           L"\n"
           L"Use --padding when labels need margin space instead of covering source "
           L"UI text.\n"
           L"Use obfuscate only when concealment is explicitly requested; CLI use "
           L"requires tools.obfuscate.risk_acknowledged = true.\n"
           L"\n"
           L"Per-type fields: greenflame --help annotation-types\n";
}

[[nodiscard]] static std::wstring Build_annotation_types_help_text() {
    return L"Greenflame Annotation Types Help\n"
           L"\n"
           L"Common optional fields: color \"#rrggbb\", size integer.\n"
           L"\n"
           L"Line and arrow:\n"
           L"  {\"type\":\"line\",\"start\":{\"x\":10,\"y\":20},"
           L"\"end\":{\"x\":120,\"y\":80}}\n"
           L"  {\"type\":\"arrow\",\"start\":{\"x\":10,\"y\":20},"
           L"\"end\":{\"x\":120,\"y\":80}}\n"
           L"\n"
           L"Freehand and highlighter:\n"
           L"  {\"type\":\"brush\",\"points\":[{\"x\":10,\"y\":20},"
           L"{\"x\":20,\"y\":25}]}\n"
           L"  {\"type\":\"highlighter\",\"start\":{\"x\":10,\"y\":20},"
           L"\"end\":{\"x\":120,\"y\":80},\"opacity_percent\":50}\n"
           L"\n"
           L"Rectangles and ellipses:\n"
           L"  {\"type\":\"rectangle\",\"left\":10,\"top\":20,"
           L"\"width\":100,\"height\":60}\n"
           L"  {\"type\":\"filled_rectangle\",\"left\":10,\"top\":20,"
           L"\"width\":100,\"height\":60}\n"
           L"  {\"type\":\"ellipse\",\"center\":{\"x\":60,\"y\":50},"
           L"\"width\":100,\"height\":60}\n"
           L"  {\"type\":\"filled_ellipse\",\"center\":{\"x\":60,\"y\":50},"
           L"\"width\":100,\"height\":60}\n"
           L"\n"
           L"Text and bubble:\n"
           L"  {\"type\":\"text\",\"origin\":{\"x\":10,\"y\":20},"
           L"\"text\":\"Label\",\"font\":{\"preset\":\"sans\"}}\n"
           L"  {\"type\":\"bubble\",\"center\":{\"x\":60,\"y\":50},"
           L"\"size\":5}\n"
           L"\n"
           L"Obfuscate:\n"
           L"  {\"type\":\"obfuscate\",\"left\":10,\"top\":20,"
           L"\"width\":100,\"height\":60,\"size\":8}\n";
}

[[nodiscard]] static std::wstring Build_windows_help_text() {
    return L"Greenflame Window Help\n"
           L"\n"
           L"--window <title> captures by title. A unique case-insensitive exact "
           L"title match wins over broader substring matches.\n"
           L"--window-hwnd 0x... captures one exact window after disambiguation.\n"
           L"\n"
           L"--window-capture:\n"
           L"  auto  Prefer WGC, fall back to GDI when appropriate\n"
           L"  wgc   Capture the target window itself; fail instead of GDI fallback\n"
           L"  gdi   Capture visible desktop pixels for the window rectangle\n"
           L"\n"
           L"If --window exits ambiguous, stderr lists candidates like:\n"
           L"  hwnd=0x... class=\"...\" title=\"...\" (x=..., y=..., w=..., h=...)\n"
           L"\n"
           L"Retry with --window-hwnd only when one non-uncapturable candidate "
           L"clearly matches the user's intent.\n"
           L"Do not replace a failed window capture with --region unless visible "
           L"desktop pixels are explicitly acceptable.\n";
}

[[nodiscard]] static std::wstring Build_exits_help_text() {
    return L"Greenflame Exit Help\n"
           L"\n"
           L"Relevant CLI exits:\n"
           L"  2   argument or validation failure\n"
           L"  6   no visible window matched\n"
           L"  7   window match ambiguous; inspect candidates and retry with "
           L"--window-hwnd if clear\n"
           L"  10  output path resolution or reservation failed\n"
           L"  11  capture or save failed\n"
           L"  12  matched window became unavailable before capture\n"
           L"  13  matched window is minimized\n"
           L"  14  --annotate input invalid\n"
           L"  15  forced --window-capture wgc failed\n"
           L"  16  input image unreadable or unsupported\n"
           L"  17  window is excluded from capture\n"
           L"  18  obfuscate risk not acknowledged\n";
}

std::wstring Build_cli_help_text(CliHelpTopic topic, bool debug_build) {
    switch (topic) {
    case CliHelpTopic::Main:
        return Build_main_help_text(debug_build);
    case CliHelpTopic::Agent:
        return Build_agent_help_text();
    case CliHelpTopic::Annotate:
        return Build_annotate_help_text();
    case CliHelpTopic::AnnotationTypes:
        return Build_annotation_types_help_text();
    case CliHelpTopic::Windows:
        return Build_windows_help_text();
    case CliHelpTopic::Exits:
        return Build_exits_help_text();
    }
    return Build_main_help_text(debug_build);
}

std::wstring Build_cli_help_text(bool debug_build) {
    return Build_cli_help_text(CliHelpTopic::Main, debug_build);
}

} // namespace greenflame::core
