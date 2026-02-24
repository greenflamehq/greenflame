#include "greenflame_core/cli_options.h"

namespace greenflame::core {

namespace {

enum class CliOptionId : uint8_t {
    Region = 0,
    Window = 1,
    Monitor = 2,
    Desktop = 3,
    Help = 4,
    Output = 5,
#ifdef DEBUG
    Testing12 = 6,
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
        L"Capture a visible top-level window matching title text.",
        L'w',
        CliOptionId::Window,
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
        L"output",
        L"<path>",
        L"Output file path. Valid only with a capture mode.",
        L'o',
        CliOptionId::Output,
        CliOptionValueKind::Path,
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
    std::wstring_view parts[4] = {};
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
        return L"Capture Mode (Mutually Exclusive):";
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
Find_option_by_long_name_any(std::wstring_view name) noexcept {
    for (CliOptionSpec const &spec : kCliOptionSpecs) {
        if (spec.long_name != nullptr && name == spec.long_name) {
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

[[nodiscard]] bool Try_set_capture_mode(CliOptions &options, CliCaptureMode mode,
                                        std::wstring &error_message) {
    if (options.capture_mode != CliCaptureMode::None) {
        error_message = L"Only one capture mode can be specified per invocation.";
        return false;
    }
    options.capture_mode = mode;
    return true;
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
        return CliParseResult{true, options, {}};
    }
    case CliOptionId::Window:
        if (value.empty()) {
            return Make_error(L"--window expects a non-empty name.");
        }
        if (!Try_set_capture_mode(options, CliCaptureMode::Window, error_message)) {
            return Make_error(error_message);
        }
        options.window_name = value;
        return CliParseResult{true, options, {}};
    case CliOptionId::Monitor: {
        int32_t monitor_id = 0;
        if (!Try_parse_int32(value, monitor_id) || monitor_id < 1) {
            return Make_error(L"--monitor expects an integer id >= 1.");
        }
        if (!Try_set_capture_mode(options, CliCaptureMode::Monitor, error_message)) {
            return Make_error(error_message);
        }
        options.monitor_id = monitor_id;
        return CliParseResult{true, options, {}};
    }
    case CliOptionId::Desktop:
        if (!Try_set_capture_mode(options, CliCaptureMode::Desktop, error_message)) {
            return Make_error(error_message);
        }
        return CliParseResult{true, options, {}};
    case CliOptionId::Help:
        if (!Try_set_capture_mode(options, CliCaptureMode::Help, error_message)) {
            return Make_error(error_message);
        }
        return CliParseResult{true, options, {}};
    case CliOptionId::Output:
        if (value.empty()) {
            return Make_error(L"--output expects a non-empty path.");
        }
        if (!options.output_path.empty()) {
            return Make_error(L"--output can only be specified once.");
        }
        options.output_path = value;
        return CliParseResult{true, options, {}};
#ifdef DEBUG
    case CliOptionId::Testing12:
        options.testing_1_2 = true;
        return CliParseResult{true, options, {}};
#endif
    }
    return Make_error(L"Internal CLI parser error.");
}

[[nodiscard]] CliParseResult Validate_cli_options(CliOptions const &options) {
    if (!options.output_path.empty() && !Is_capture_mode(options.capture_mode)) {
        return Make_error(
            L"--output requires one capture mode: --region, --window, --monitor, "
            L"or --desktop.");
    }
    if (options.capture_mode == CliCaptureMode::Region &&
        !options.region_px.has_value()) {
        return Make_error(L"--region value is missing.");
    }
    if (options.capture_mode == CliCaptureMode::Window && options.window_name.empty()) {
        return Make_error(L"--window value is missing.");
    }
    if (options.capture_mode == CliCaptureMode::Monitor && options.monitor_id <= 0) {
        return Make_error(L"--monitor value is missing.");
    }
    return CliParseResult{true, options, {}};
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
        CliOptionSpec const *all_build_spec = Find_option_by_long_name_any(name_view);
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

std::wstring Build_cli_help_text(bool debug_build) {
    std::wstring help_text;
    help_text += L"Greenflame\n";
    help_text += L"Yet another Windows screenshot tool.\n";
    help_text += L"\n";
    help_text += L"Usage:\n";
    help_text += L"  greenflame [capture-mode] [options]\n";
    help_text += L"  greenflame --help\n";
    help_text += L"\n";

    Append_help_group(help_text, CliOptionGroup::Exclusive, debug_build);
    Append_help_group(help_text, CliOptionGroup::Optional, debug_build);
    Append_help_group(help_text, CliOptionGroup::Debug, debug_build);

    help_text += L"Notes:\n";
    help_text += L"  --option=value and --option value are both supported.\n";
    help_text += L"  No capture mode starts the tray app as usual.\n";
    help_text += L"\n";
    return help_text;
}

} // namespace greenflame::core
