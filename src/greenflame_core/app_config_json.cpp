#include "greenflame_core/app_config_json.h"
#include "greenflame_core/annotation_types.h"
#include "greenflame_core/color_wheel.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

namespace {

using Json = easyjson::JSON;
using JsonClass = easyjson::JSON::Class;

constexpr size_t kHexColorTextLength = 7;
constexpr size_t kHexColorComponentLength = 2;
constexpr size_t kHexColorRedOffset = 1;
constexpr size_t kHexColorGreenOffset = 3;
constexpr size_t kHexColorBlueOffset = 5;
constexpr COLORREF kColorChannelMask = 0xFF;
constexpr uint8_t kHexLowNibbleMask = 0x0F;
constexpr size_t kMaxConfigPathChars = 259;
constexpr size_t kMaxFilenamePatternChars = 256;
constexpr size_t kMaxTextFontFamilyChars = 128;
constexpr int32_t kMinToolSize = 1;
constexpr int32_t kMaxToolSize = 50;
constexpr int32_t kMinColorIndex = 0;
constexpr int32_t kMaxAnnotationColorIndex =
    static_cast<int32_t>(kAnnotationColorSlotCount) - 1;
constexpr int32_t kMaxHighlighterColorIndex =
    static_cast<int32_t>(kHighlighterColorSlotCount) - 1;

constexpr std::array<std::string_view, 5> kRootKeys = {
    {"$schema", "capture", "ui", "tools", "save"}};
constexpr std::array<std::string_view, 1> kCaptureKeys = {{"include_cursor"}};
constexpr std::array<std::string_view, 4> kUiKeys = {
    {"show_balloons", "show_selection_size_side_labels",
     "show_selection_size_center_label", "tool_size_overlay_duration_ms"}};
constexpr std::array<std::string_view, 12> kToolsKeys = {
    {"brush", "line", "arrow", "rect", "ellipse", "colors", "current_color", "font",
     "highlighter", "text", "bubble", "obfuscate"}};
constexpr std::array<std::string_view, 4> kFontKeys = {
    {"sans", "serif", "mono", "art"}};
constexpr std::array<std::string_view, 6> kHighlighterKeys = {
    {"size", "colors", "current_color", "opacity_percent", "pause_straighten_ms",
     "pause_straighten_deadzone_px"}};
constexpr std::array<std::string_view, 2> kTextToolKeys = {{"size", "current_font"}};
constexpr std::array<std::string_view, 8> kSaveKeys = {
    {"default_save_dir", "last_save_as_dir", "default_save_format", "padding_color",
     "filename_pattern_region", "filename_pattern_desktop", "filename_pattern_monitor",
     "filename_pattern_window"}};
constexpr std::array<std::string_view, 1> kSizeOnlyKeys = {{"size"}};
constexpr std::array<std::string_view, 2> kObfuscateKeys = {
    {"block_size", "risk_acknowledged"}};

[[nodiscard]] bool Contains_key(std::span<const std::string_view> allowed_keys,
                                std::string_view key) noexcept {
    for (std::string_view const allowed : allowed_keys) {
        if (allowed == key) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::wstring Widen_ascii(std::string_view value) {
    std::wstring widened;
    widened.reserve(value.size());
    for (char const ch : value) {
        widened.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    }
    return widened;
}

[[nodiscard]] std::wstring Join_path(std::wstring_view base, std::string_view child) {
    std::wstring path(base);
    if (!path.empty()) {
        path.push_back(L'.');
    }
    path += Widen_ascii(child);
    return path;
}

[[nodiscard]] bool Try_parse_hex_digit(char ch, uint8_t &value) noexcept {
    if (ch >= '0' && ch <= '9') {
        value = static_cast<uint8_t>(ch - '0');
        return true;
    }
    if (ch >= 'a' && ch <= 'f') {
        value = static_cast<uint8_t>(10 + (ch - 'a'));
        return true;
    }
    if (ch >= 'A' && ch <= 'F') {
        value = static_cast<uint8_t>(10 + (ch - 'A'));
        return true;
    }
    return false;
}

[[nodiscard]] bool Try_parse_hex_byte(std::string_view value, uint8_t &out) noexcept {
    if (value.size() != kHexColorComponentLength) {
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

// Returns the decoded string stored by easyjson for a String-type node.
// easyjson::to_string() re-encodes the stored value with json_escape(), which
// would double backslashes on every round trip. We bypass that by reading the
// internal storage directly.
[[nodiscard]] std::string Get_json_string(Json const &value) noexcept {
    return value.Internal.String.has_value() ? *(value.Internal.String.value())
                                             : std::string{};
}

[[nodiscard]] bool Try_parse_color(std::string_view value, COLORREF &out) noexcept {
    if (value.size() != kHexColorTextLength || value[0] != '#') {
        return false;
    }

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (!Try_parse_hex_byte(value.substr(kHexColorRedOffset, kHexColorComponentLength),
                            red) ||
        !Try_parse_hex_byte(
            value.substr(kHexColorGreenOffset, kHexColorComponentLength), green) ||
        !Try_parse_hex_byte(value.substr(kHexColorBlueOffset, kHexColorComponentLength),
                            blue)) {
        return false;
    }

    out = Make_colorref(red, green, blue);
    return true;
}

[[nodiscard]] bool Try_decode_utf8(std::string_view value, std::wstring &out) noexcept {
    if (value.empty()) {
        out.clear();
        return true;
    }

    int const required_chars =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0);
    if (required_chars <= 0) {
        return false;
    }

    out.resize(static_cast<size_t>(required_chars));
    int const converted_chars =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), out.data(), required_chars);
    return converted_chars == required_chars;
}

[[nodiscard]] std::string To_utf8(std::wstring const &value) {
    if (value.empty()) {
        return {};
    }

    int const required_chars = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
                                                   nullptr, 0, nullptr, nullptr);
    if (required_chars <= 1) {
        return {};
    }

    std::string out(static_cast<size_t>(required_chars - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), required_chars,
                        nullptr, nullptr);
    return out;
}

[[nodiscard]] char Hex_digit(uint8_t value) {
    return static_cast<char>((value < 10) ? ('0' + value) : ('a' + (value - 10)));
}

[[nodiscard]] std::string_view Text_font_choice_token(TextFontChoice choice) {
    switch (choice) {
    case TextFontChoice::Sans:
        return "sans";
    case TextFontChoice::Serif:
        return "serif";
    case TextFontChoice::Mono:
        return "mono";
    case TextFontChoice::Art:
        return "art";
    }
    return "sans";
}

[[nodiscard]] std::string To_hex_color(COLORREF color) {
    auto channel = [&](unsigned shift) {
        return static_cast<uint8_t>((color >> shift) & kColorChannelMask);
    };

    uint8_t const red = channel(0);
    uint8_t const green = channel(8);
    uint8_t const blue = channel(16);

    std::string value(kHexColorTextLength, '#');
    value[kHexColorRedOffset] = Hex_digit(static_cast<uint8_t>(red >> 4u));
    value[kHexColorRedOffset + 1] =
        Hex_digit(static_cast<uint8_t>(red & kHexLowNibbleMask));
    value[kHexColorGreenOffset] = Hex_digit(static_cast<uint8_t>(green >> 4u));
    value[kHexColorGreenOffset + 1] =
        Hex_digit(static_cast<uint8_t>(green & kHexLowNibbleMask));
    value[kHexColorBlueOffset] = Hex_digit(static_cast<uint8_t>(blue >> 4u));
    value[kHexColorBlueOffset + 1] =
        Hex_digit(static_cast<uint8_t>(blue & kHexLowNibbleMask));
    return value;
}

[[nodiscard]] bool Has_non_whitespace(std::wstring_view value) noexcept {
    for (wchar_t const ch : value) {
        if (std::iswspace(ch) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool Try_read_int32(Json const &value, int32_t &out) noexcept {
    if (value.JSON_type() != JsonClass::Integral) {
        return false;
    }

    long long const parsed = static_cast<long long>(value.to_int());
    if (parsed < static_cast<long long>(std::numeric_limits<int32_t>::min()) ||
        parsed > static_cast<long long>(std::numeric_limits<int32_t>::max())) {
        return false;
    }

    out = static_cast<int32_t>(parsed);
    return true;
}

class JsonSyntaxChecker final {
  public:
    explicit JsonSyntaxChecker(std::string_view text) : text_(text) {}

    [[nodiscard]] std::optional<AppConfigDiagnostic> Check() noexcept {
        Skip_whitespace();
        if (!Parse_value()) {
            return diagnostic_;
        }

        Skip_whitespace();
        if (!At_end()) {
            (void)Fail(L"Unexpected trailing characters after the root JSON value.");
        }
        return diagnostic_;
    }

  private:
    [[nodiscard]] bool At_end() const noexcept { return index_ >= text_.size(); }

    [[nodiscard]] char Peek() const noexcept { return At_end() ? '\0' : text_[index_]; }

    char Advance() noexcept {
        char const ch = Peek();
        if (!At_end()) {
            ++index_;
            if (ch == '\n') {
                ++line_;
                column_ = 1;
            } else {
                ++column_;
            }
        }
        return ch;
    }

    void Skip_whitespace() noexcept {
        while (!At_end() && std::isspace(static_cast<unsigned char>(Peek())) != 0) {
            (void)Advance();
        }
    }

    bool Fail(std::wstring_view message) noexcept {
        if (!diagnostic_.has_value()) {
            diagnostic_ = AppConfigDiagnostic{AppConfigDiagnosticKind::Parse,
                                              std::wstring(message), line_, column_};
        }
        return false;
    }

    bool Parse_value() noexcept {
        Skip_whitespace();
        if (At_end()) {
            return Fail(L"Unexpected end of file.");
        }

        switch (Peek()) {
        case '{':
            return Parse_object();
        case '[':
            return Parse_array();
        case '"':
            return Parse_string();
        case 't':
            return Parse_literal("true", L"true");
        case 'f':
            return Parse_literal("false", L"false");
        case 'n':
            return Parse_literal("null", L"null");
        default:
            break;
        }

        if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
            return Parse_number();
        }

        return Fail(L"Unexpected character while parsing a JSON value.");
    }

    bool Parse_object() noexcept {
        (void)Advance();
        Skip_whitespace();
        if (At_end()) {
            return Fail(L"Unexpected end of file inside an object.");
        }
        if (Peek() == '}') {
            (void)Advance();
            return true;
        }

        while (true) {
            if (Peek() != '"') {
                return Fail(L"Expected a quoted object property name.");
            }
            if (!Parse_string()) {
                return false;
            }

            Skip_whitespace();
            if (At_end()) {
                return Fail(L"Unexpected end of file after an object property name.");
            }
            if (Peek() != ':') {
                return Fail(L"Expected ':' after an object property name.");
            }
            (void)Advance();

            if (!Parse_value()) {
                return false;
            }

            Skip_whitespace();
            if (At_end()) {
                return Fail(L"Unexpected end of file inside an object.");
            }
            if (Peek() == '}') {
                (void)Advance();
                return true;
            }
            if (Peek() != ',') {
                return Fail(L"Expected ',' or '}' after an object member.");
            }
            (void)Advance();
            Skip_whitespace();
            if (At_end()) {
                return Fail(L"Unexpected end of file after ','.");
            }
        }
    }

    bool Parse_array() noexcept {
        (void)Advance();
        Skip_whitespace();
        if (At_end()) {
            return Fail(L"Unexpected end of file inside an array.");
        }
        if (Peek() == ']') {
            (void)Advance();
            return true;
        }

        while (true) {
            if (!Parse_value()) {
                return false;
            }

            Skip_whitespace();
            if (At_end()) {
                return Fail(L"Unexpected end of file inside an array.");
            }
            if (Peek() == ']') {
                (void)Advance();
                return true;
            }
            if (Peek() != ',') {
                return Fail(L"Expected ',' or ']' after an array element.");
            }
            (void)Advance();
            Skip_whitespace();
            if (At_end()) {
                return Fail(L"Unexpected end of file after ','.");
            }
        }
    }

    bool Parse_string() noexcept {
        if (Peek() != '"') {
            return Fail(L"Expected a string.");
        }

        (void)Advance();
        while (!At_end()) {
            char const ch = Advance();
            if (ch == '"') {
                return true;
            }
            if (static_cast<unsigned char>(ch) < 0x20u) {
                return Fail(L"Unescaped control character inside a string.");
            }
            if (ch != '\\') {
                continue;
            }

            if (At_end()) {
                return Fail(L"Unexpected end of file in a string escape sequence.");
            }

            char const escaped = Advance();
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                break;
            case 'u':
                for (int i = 0; i < 4; ++i) {
                    if (At_end()) {
                        return Fail(
                            L"Unexpected end of file in a unicode escape sequence.");
                    }
                    if (std::isxdigit(static_cast<unsigned char>(Peek())) == 0) {
                        return Fail(L"Expected a hexadecimal digit in a unicode escape "
                                    L"sequence.");
                    }
                    (void)Advance();
                }
                break;
            default:
                return Fail(L"Invalid escape sequence inside a string.");
            }
        }

        return Fail(L"Unexpected end of file inside a string.");
    }

    bool Parse_number() noexcept {
        if (Peek() == '-') {
            (void)Advance();
            if (At_end()) {
                return Fail(L"Unexpected end of file after '-'.");
            }
        }

        if (Peek() == '0') {
            (void)Advance();
        } else if (Peek() >= '1' && Peek() <= '9') {
            do {
                (void)Advance();
            } while (!At_end() &&
                     std::isdigit(static_cast<unsigned char>(Peek())) != 0);
        } else {
            return Fail(L"Expected a digit while parsing a number.");
        }

        if (!At_end() && Peek() == '.') {
            (void)Advance();
            if (At_end() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
                return Fail(L"Expected a digit after the decimal point.");
            }
            do {
                (void)Advance();
            } while (!At_end() &&
                     std::isdigit(static_cast<unsigned char>(Peek())) != 0);
        }

        if (!At_end() && (Peek() == 'e' || Peek() == 'E')) {
            (void)Advance();
            if (!At_end() && (Peek() == '+' || Peek() == '-')) {
                (void)Advance();
            }
            if (At_end() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
                return Fail(L"Expected an exponent digit.");
            }
            do {
                (void)Advance();
            } while (!At_end() &&
                     std::isdigit(static_cast<unsigned char>(Peek())) != 0);
        }

        return true;
    }

    bool Parse_literal(std::string_view literal, std::wstring_view expected) noexcept {
        for (char const expected_char : literal) {
            if (At_end()) {
                return Fail(L"Unexpected end of file while parsing a literal.");
            }
            if (Peek() != expected_char) {
                std::wstring message = L"Expected literal '";
                message += expected;
                message += L"'.";
                return Fail(message);
            }
            (void)Advance();
        }
        return true;
    }

    std::string_view text_;
    size_t index_ = 0;
    size_t line_ = 1;
    size_t column_ = 1;
    std::optional<AppConfigDiagnostic> diagnostic_ = std::nullopt;
};

class QuietCerrCapture final {
  public:
    QuietCerrCapture() : old_buffer_(std::cerr.rdbuf(stream_.rdbuf())) {}

    QuietCerrCapture(QuietCerrCapture const &) = delete;
    QuietCerrCapture &operator=(QuietCerrCapture const &) = delete;

    ~QuietCerrCapture() { std::cerr.rdbuf(old_buffer_); }

  private:
    std::ostringstream stream_ = {};
    std::streambuf *old_buffer_ = nullptr;
};

[[nodiscard]] Json Load_json_silently(std::string_view json_text) noexcept {
    if (json_text.empty()) {
        return Json{};
    }

    QuietCerrCapture quiet_cerr;
    try {
        std::string const json_copy(json_text);
        return Json::load(json_copy);
    } catch (...) {
        return Json{};
    }
}

struct ParseContext final {
    AppConfigParseResult result = {};

    void Report_schema_error(std::wstring_view path,
                             std::wstring_view detail) noexcept {
        if (result.diagnostic.has_value()) {
            return;
        }

        std::wstring message = {};
        if (!path.empty()) {
            message += path;
            message += L": ";
        }
        message += detail;
        result.diagnostic =
            AppConfigDiagnostic{AppConfigDiagnosticKind::Schema, std::move(message),
                                std::nullopt, std::nullopt};
    }
};

void Report_unknown_keys(Json const &object,
                         std::span<const std::string_view> allowed_keys,
                         std::wstring_view path, ParseContext &ctx) {
    if (object.JSON_type() != JsonClass::Object) {
        return;
    }

    for (auto const &[key, _] : object.object_range()) {
        if (!Contains_key(allowed_keys, key)) {
            ctx.Report_schema_error(Join_path(path, key), L"Unknown property.");
        }
    }
}

[[nodiscard]] std::wstring Integer_range_message(int32_t min_value, int32_t max_value) {
    std::wstring message = L"Must be an integer between ";
    message += std::to_wstring(min_value);
    message += L" and ";
    message += std::to_wstring(max_value);
    message += L".";
    return message;
}

[[nodiscard]] std::wstring Max_length_message(size_t max_chars) {
    std::wstring message = L"Must be ";
    message += std::to_wstring(max_chars);
    message += L" characters or fewer.";
    return message;
}

void Apply_integer_property(Json const &object, char const *key, std::wstring_view path,
                            int32_t min_value, int32_t max_value, int32_t &target,
                            ParseContext &ctx) {
    if (!object.has_key(key)) {
        return;
    }

    int32_t parsed = 0;
    if (!Try_read_int32(object[key], parsed)) {
        ctx.Report_schema_error(Join_path(path, key), L"Must be an integer.");
        return;
    }
    if (parsed < min_value || parsed > max_value) {
        ctx.Report_schema_error(Join_path(path, key),
                                Integer_range_message(min_value, max_value));
        return;
    }

    target = parsed;
}

void Apply_bool_property(Json const &object, char const *key, std::wstring_view path,
                         bool &target, ParseContext &ctx) {
    if (!object.has_key(key)) {
        return;
    }
    if (object[key].JSON_type() != JsonClass::Boolean) {
        ctx.Report_schema_error(Join_path(path, key), L"Must be a boolean.");
        return;
    }

    target = object[key].to_bool();
}

void Apply_non_empty_string_property(Json const &object, char const *key,
                                     std::wstring_view path, size_t max_chars,
                                     std::wstring &target, bool require_non_whitespace,
                                     ParseContext &ctx) {
    if (!object.has_key(key)) {
        return;
    }
    if (object[key].JSON_type() != JsonClass::String) {
        ctx.Report_schema_error(Join_path(path, key), L"Must be a string.");
        return;
    }

    std::string const utf8_value = Get_json_string(object[key]);
    if (utf8_value.empty()) {
        ctx.Report_schema_error(Join_path(path, key), L"Must not be empty.");
        return;
    }

    std::wstring decoded = {};
    if (!Try_decode_utf8(utf8_value, decoded)) {
        ctx.Report_schema_error(Join_path(path, key), L"Must be valid UTF-8.");
        return;
    }
    if (decoded.empty()) {
        ctx.Report_schema_error(Join_path(path, key), L"Must not be empty.");
        return;
    }
    if (decoded.size() > max_chars) {
        ctx.Report_schema_error(Join_path(path, key), Max_length_message(max_chars));
        return;
    }
    if (require_non_whitespace && !Has_non_whitespace(decoded)) {
        ctx.Report_schema_error(Join_path(path, key),
                                L"Must contain at least one non-whitespace character.");
        return;
    }

    target = std::move(decoded);
}

void Apply_font_choice_property(Json const &object, char const *key,
                                std::wstring_view path, TextFontChoice &target,
                                ParseContext &ctx) {
    if (!object.has_key(key)) {
        return;
    }
    if (object[key].JSON_type() != JsonClass::String) {
        ctx.Report_schema_error(Join_path(path, key), L"Must be a string.");
        return;
    }

    std::string const value = Get_json_string(object[key]);
    if (value == "sans") {
        target = TextFontChoice::Sans;
        return;
    }
    if (value == "serif") {
        target = TextFontChoice::Serif;
        return;
    }
    if (value == "mono") {
        target = TextFontChoice::Mono;
        return;
    }
    if (value == "art") {
        target = TextFontChoice::Art;
        return;
    }

    ctx.Report_schema_error(Join_path(path, key),
                            L"Must be one of: sans, serif, mono, art.");
}

template <size_t SlotCount>
void Apply_color_map_property(Json const &object, char const *key,
                              std::wstring_view path,
                              std::array<COLORREF, SlotCount> &target,
                              ParseContext &ctx) {
    if (!object.has_key(key)) {
        return;
    }

    Json const &colors = object[key];
    std::wstring const colors_path = Join_path(path, key);
    if (colors.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(colors_path, L"Must be an object.");
        return;
    }

    for (auto const &[slot_key, slot_value] : colors.object_range()) {
        if (slot_key.size() != 1 || slot_key[0] < '0' ||
            slot_key[0] >= static_cast<char>('0' + SlotCount)) {
            ctx.Report_schema_error(Join_path(colors_path, slot_key),
                                    L"Unknown color slot.");
            continue;
        }
        if (slot_value.JSON_type() != JsonClass::String) {
            ctx.Report_schema_error(Join_path(colors_path, slot_key),
                                    L"Must be a #rrggbb color string.");
            continue;
        }

        std::string const color_text = Get_json_string(slot_value);
        COLORREF parsed = 0;
        if (!Try_parse_color(color_text, parsed)) {
            ctx.Report_schema_error(Join_path(colors_path, slot_key),
                                    L"Must be a #rrggbb color string.");
            continue;
        }

        target[static_cast<size_t>(slot_key[0] - '0')] = parsed;
    }
}

void Apply_size_only_object(Json const &object, std::wstring_view path, int32_t &target,
                            ParseContext &ctx) {
    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kSizeOnlyKeys, path, ctx);
    Apply_integer_property(object, "size", path, kMinToolSize, kMaxToolSize, target,
                           ctx);
}

void Apply_obfuscate_object(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"tools.obfuscate";

    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(k_path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kObfuscateKeys, k_path, ctx);
    Apply_integer_property(object, "block_size", k_path, kMinToolSize, kMaxToolSize,
                           ctx.result.config.obfuscate_block_size, ctx);
    Apply_bool_property(object, "risk_acknowledged", k_path,
                        ctx.result.config.obfuscate_risk_acknowledged, ctx);
}

void Apply_capture_object(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"capture";

    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(k_path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kCaptureKeys, k_path, ctx);
    Apply_bool_property(object, "include_cursor", k_path,
                        ctx.result.config.include_cursor, ctx);
}

void Apply_ui_object(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"ui";

    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(k_path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kUiKeys, k_path, ctx);
    Apply_bool_property(object, "show_balloons", k_path,
                        ctx.result.config.show_balloons, ctx);
    Apply_bool_property(object, "show_selection_size_side_labels", k_path,
                        ctx.result.config.show_selection_size_side_labels, ctx);
    Apply_bool_property(object, "show_selection_size_center_label", k_path,
                        ctx.result.config.show_selection_size_center_label, ctx);
    Apply_integer_property(object, "tool_size_overlay_duration_ms", k_path, 0,
                           std::numeric_limits<int32_t>::max(),
                           ctx.result.config.tool_size_overlay_duration_ms, ctx);
}

void Apply_font_object(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"tools.font";

    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(k_path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kFontKeys, k_path, ctx);
    Apply_non_empty_string_property(object, "sans", k_path, kMaxTextFontFamilyChars,
                                    ctx.result.config.text_font_sans, true, ctx);
    Apply_non_empty_string_property(object, "serif", k_path, kMaxTextFontFamilyChars,
                                    ctx.result.config.text_font_serif, true, ctx);
    Apply_non_empty_string_property(object, "mono", k_path, kMaxTextFontFamilyChars,
                                    ctx.result.config.text_font_mono, true, ctx);
    Apply_non_empty_string_property(object, "art", k_path, kMaxTextFontFamilyChars,
                                    ctx.result.config.text_font_art, true, ctx);
}

void Apply_highlighter_object(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"tools.highlighter";

    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(k_path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kHighlighterKeys, k_path, ctx);
    Apply_integer_property(object, "size", k_path, kMinToolSize, kMaxToolSize,
                           ctx.result.config.highlighter_size, ctx);
    Apply_color_map_property(object, "colors", k_path,
                             ctx.result.config.highlighter_colors, ctx);
    Apply_integer_property(object, "current_color", k_path, kMinColorIndex,
                           kMaxHighlighterColorIndex,
                           ctx.result.config.current_highlighter_color_index, ctx);
    Apply_integer_property(object, "opacity_percent", k_path,
                           StrokeStyle::kMinOpacityPercent,
                           StrokeStyle::kMaxOpacityPercent,
                           ctx.result.config.highlighter_opacity_percent, ctx);
    Apply_integer_property(object, "pause_straighten_ms", k_path, 0,
                           std::numeric_limits<int32_t>::max(),
                           ctx.result.config.highlighter_pause_straighten_ms, ctx);
    Apply_integer_property(object, "pause_straighten_deadzone_px", k_path, 0,
                           std::numeric_limits<int32_t>::max(),
                           ctx.result.config.highlighter_pause_straighten_deadzone_px,
                           ctx);
}

void Apply_text_tool_object(Json const &object, std::wstring_view path,
                            int32_t &size_target, TextFontChoice &font_target,
                            ParseContext &ctx) {
    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kTextToolKeys, path, ctx);
    Apply_integer_property(object, "size", path, kMinToolSize, kMaxToolSize,
                           size_target, ctx);
    Apply_font_choice_property(object, "current_font", path, font_target, ctx);
}

void Apply_tools_object(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"tools";

    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(k_path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kToolsKeys, k_path, ctx);

    if (object.has_key("brush")) {
        Apply_size_only_object(object["brush"], L"tools.brush",
                               ctx.result.config.brush_size, ctx);
    }
    if (object.has_key("line")) {
        Apply_size_only_object(object["line"], L"tools.line",
                               ctx.result.config.line_size, ctx);
    }
    if (object.has_key("arrow")) {
        Apply_size_only_object(object["arrow"], L"tools.arrow",
                               ctx.result.config.arrow_size, ctx);
    }
    if (object.has_key("rect")) {
        Apply_size_only_object(object["rect"], L"tools.rect",
                               ctx.result.config.rect_size, ctx);
    }
    if (object.has_key("ellipse")) {
        Apply_size_only_object(object["ellipse"], L"tools.ellipse",
                               ctx.result.config.ellipse_size, ctx);
    }

    Apply_color_map_property(object, "colors", k_path,
                             ctx.result.config.annotation_colors, ctx);
    Apply_integer_property(object, "current_color", k_path, kMinColorIndex,
                           kMaxAnnotationColorIndex,
                           ctx.result.config.current_annotation_color_index, ctx);

    if (object.has_key("font")) {
        Apply_font_object(object["font"], ctx);
    }
    if (object.has_key("highlighter")) {
        Apply_highlighter_object(object["highlighter"], ctx);
    }
    if (object.has_key("text")) {
        Apply_text_tool_object(object["text"], L"tools.text",
                               ctx.result.config.text_size,
                               ctx.result.config.text_current_font, ctx);
    }
    if (object.has_key("bubble")) {
        Apply_text_tool_object(object["bubble"], L"tools.bubble",
                               ctx.result.config.bubble_size,
                               ctx.result.config.bubble_current_font, ctx);
    }
    if (object.has_key("obfuscate")) {
        Apply_obfuscate_object(object["obfuscate"], ctx);
    }
}

void Apply_default_save_format_property(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"save.default_save_format";

    if (!object.has_key("default_save_format")) {
        return;
    }
    if (object["default_save_format"].JSON_type() != JsonClass::String) {
        ctx.Report_schema_error(k_path, L"Must be a string.");
        return;
    }

    std::string const format = Get_json_string(object["default_save_format"]);
    if (format != "png" && format != "jpg" && format != "bmp") {
        ctx.Report_schema_error(k_path, L"Must be one of: png, jpg, bmp.");
        return;
    }

    if (!Try_decode_utf8(format, ctx.result.config.default_save_format)) {
        ctx.Report_schema_error(k_path, L"Must be valid UTF-8.");
    }
}

void Apply_padding_color_property(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"save.padding_color";

    if (!object.has_key("padding_color")) {
        return;
    }
    if (object["padding_color"].JSON_type() != JsonClass::String) {
        ctx.Report_schema_error(k_path, L"Must be a #rrggbb color string.");
        return;
    }

    std::string const color_text = Get_json_string(object["padding_color"]);
    COLORREF parsed = 0;
    if (!Try_parse_color(color_text, parsed)) {
        ctx.Report_schema_error(k_path, L"Must be a #rrggbb color string.");
        return;
    }

    ctx.result.config.padding_color = parsed;
}

void Apply_save_object(Json const &object, ParseContext &ctx) {
    constexpr std::wstring_view k_path = L"save";

    if (object.JSON_type() != JsonClass::Object) {
        ctx.Report_schema_error(k_path, L"Must be an object.");
        return;
    }

    Report_unknown_keys(object, kSaveKeys, k_path, ctx);
    Apply_non_empty_string_property(object, "default_save_dir", k_path,
                                    kMaxConfigPathChars,
                                    ctx.result.config.default_save_dir, false, ctx);
    Apply_non_empty_string_property(object, "last_save_as_dir", k_path,
                                    kMaxConfigPathChars,
                                    ctx.result.config.last_save_as_dir, false, ctx);
    Apply_non_empty_string_property(
        object, "filename_pattern_region", k_path, kMaxFilenamePatternChars,
        ctx.result.config.filename_pattern_region, false, ctx);
    Apply_non_empty_string_property(
        object, "filename_pattern_desktop", k_path, kMaxFilenamePatternChars,
        ctx.result.config.filename_pattern_desktop, false, ctx);
    Apply_non_empty_string_property(
        object, "filename_pattern_monitor", k_path, kMaxFilenamePatternChars,
        ctx.result.config.filename_pattern_monitor, false, ctx);
    Apply_non_empty_string_property(
        object, "filename_pattern_window", k_path, kMaxFilenamePatternChars,
        ctx.result.config.filename_pattern_window, false, ctx);
    Apply_default_save_format_property(object, ctx);
    Apply_padding_color_property(object, ctx);
}

} // namespace

AppConfigParseResult
Parse_app_config_json_with_diagnostics(std::string_view json_text) noexcept {
    ParseContext ctx = {};
    if (std::optional<AppConfigDiagnostic> parse_error =
            JsonSyntaxChecker(json_text).Check()) {
        ctx.result.diagnostic = std::move(parse_error);
    }

    Json const root = Load_json_silently(json_text);
    if (root.JSON_type() != JsonClass::Object) {
        if (!ctx.result.diagnostic.has_value()) {
            ctx.Report_schema_error(L"", L"Top-level JSON value must be an object.");
        }
        return ctx.result;
    }

    Report_unknown_keys(root, kRootKeys, L"", ctx);
    if (root.has_key("$schema") && root["$schema"].JSON_type() != JsonClass::String) {
        ctx.Report_schema_error(L"$schema", L"Must be a string.");
    }
    if (root.has_key("capture")) {
        Apply_capture_object(root["capture"], ctx);
    }
    if (root.has_key("ui")) {
        Apply_ui_object(root["ui"], ctx);
    }
    if (root.has_key("tools")) {
        Apply_tools_object(root["tools"], ctx);
    }
    if (root.has_key("save")) {
        Apply_save_object(root["save"], ctx);
    }

    return ctx.result;
}

std::optional<AppConfig> Parse_app_config_json(std::string_view json_text) noexcept {
    AppConfigParseResult const result =
        Parse_app_config_json_with_diagnostics(json_text);
    if (result.Has_error()) {
        return std::nullopt;
    }
    return result.config;
}

std::string Serialize_app_config_json(AppConfig const &config) {
    AppConfig const defaults{};
    easyjson::JSON root = easyjson::object();

    if (config.include_cursor != defaults.include_cursor) {
        root["capture"] = easyjson::object();
        root["capture"]["include_cursor"] = config.include_cursor;
    }

    if (!config.show_balloons || !config.show_selection_size_side_labels ||
        !config.show_selection_size_center_label ||
        config.tool_size_overlay_duration_ms !=
            defaults.tool_size_overlay_duration_ms) {
        root["ui"] = easyjson::object();
        if (!config.show_balloons) {
            root["ui"]["show_balloons"] = false;
        }
        if (!config.show_selection_size_side_labels) {
            root["ui"]["show_selection_size_side_labels"] = false;
        }
        if (!config.show_selection_size_center_label) {
            root["ui"]["show_selection_size_center_label"] = false;
        }
        if (config.tool_size_overlay_duration_ms !=
            defaults.tool_size_overlay_duration_ms) {
            root["ui"]["tool_size_overlay_duration_ms"] =
                config.tool_size_overlay_duration_ms;
        }
    }

    bool const wrote_tools =
        config.brush_size != defaults.brush_size ||
        config.line_size != defaults.line_size ||
        config.arrow_size != defaults.arrow_size ||
        config.rect_size != defaults.rect_size ||
        config.ellipse_size != defaults.ellipse_size ||
        config.highlighter_size != defaults.highlighter_size ||
        config.bubble_size != defaults.bubble_size ||
        config.obfuscate_block_size != defaults.obfuscate_block_size ||
        config.obfuscate_risk_acknowledged !=
            defaults.obfuscate_risk_acknowledged ||
        config.text_size != defaults.text_size ||
        config.current_annotation_color_index !=
            defaults.current_annotation_color_index ||
        config.annotation_colors != defaults.annotation_colors ||
        config.current_highlighter_color_index !=
            defaults.current_highlighter_color_index ||
        config.highlighter_opacity_percent != defaults.highlighter_opacity_percent ||
        config.highlighter_pause_straighten_ms !=
            defaults.highlighter_pause_straighten_ms ||
        config.highlighter_pause_straighten_deadzone_px !=
            defaults.highlighter_pause_straighten_deadzone_px ||
        config.highlighter_colors != defaults.highlighter_colors ||
        config.text_current_font != defaults.text_current_font ||
        config.bubble_current_font != defaults.bubble_current_font ||
        config.text_font_sans != defaults.text_font_sans ||
        config.text_font_serif != defaults.text_font_serif ||
        config.text_font_mono != defaults.text_font_mono ||
        config.text_font_art != defaults.text_font_art;

    if (wrote_tools) {
        root["tools"] = easyjson::object();
        if (config.brush_size != defaults.brush_size) {
            root["tools"]["brush"]["size"] = config.brush_size;
        }
        if (config.line_size != defaults.line_size) {
            root["tools"]["line"]["size"] = config.line_size;
        }
        if (config.arrow_size != defaults.arrow_size) {
            root["tools"]["arrow"]["size"] = config.arrow_size;
        }
        if (config.rect_size != defaults.rect_size) {
            root["tools"]["rect"]["size"] = config.rect_size;
        }
        if (config.ellipse_size != defaults.ellipse_size) {
            root["tools"]["ellipse"]["size"] = config.ellipse_size;
        }
        if (config.annotation_colors != defaults.annotation_colors) {
            easyjson::JSON colors_obj = easyjson::object();
            for (size_t i = 0; i < kAnnotationColorSlotCount; ++i) {
                if (config.annotation_colors[i] != defaults.annotation_colors[i]) {
                    colors_obj[std::to_string(i)] =
                        To_hex_color(config.annotation_colors[i]);
                }
            }
            root["tools"]["colors"] = colors_obj;
        }
        if (config.current_annotation_color_index !=
            defaults.current_annotation_color_index) {
            root["tools"]["current_color"] = config.current_annotation_color_index;
        }
        if (config.text_font_sans != defaults.text_font_sans ||
            config.text_font_serif != defaults.text_font_serif ||
            config.text_font_mono != defaults.text_font_mono ||
            config.text_font_art != defaults.text_font_art) {
            root["tools"]["font"] = easyjson::object();
            root["tools"]["font"]["sans"] = To_utf8(config.text_font_sans);
            root["tools"]["font"]["serif"] = To_utf8(config.text_font_serif);
            root["tools"]["font"]["mono"] = To_utf8(config.text_font_mono);
            root["tools"]["font"]["art"] = To_utf8(config.text_font_art);
        }
        if (config.highlighter_size != defaults.highlighter_size ||
            config.highlighter_colors != defaults.highlighter_colors ||
            config.current_highlighter_color_index !=
                defaults.current_highlighter_color_index ||
            config.highlighter_opacity_percent !=
                defaults.highlighter_opacity_percent ||
            config.highlighter_pause_straighten_ms !=
                defaults.highlighter_pause_straighten_ms ||
            config.highlighter_pause_straighten_deadzone_px !=
                defaults.highlighter_pause_straighten_deadzone_px) {
            root["tools"]["highlighter"] = easyjson::object();
            if (config.highlighter_size != defaults.highlighter_size) {
                root["tools"]["highlighter"]["size"] = config.highlighter_size;
            }
            if (config.highlighter_colors != defaults.highlighter_colors) {
                easyjson::JSON highlighter_colors = easyjson::object();
                for (size_t i = 0; i < kHighlighterColorSlotCount; ++i) {
                    if (config.highlighter_colors[i] !=
                        defaults.highlighter_colors[i]) {
                        highlighter_colors[std::to_string(i)] =
                            To_hex_color(config.highlighter_colors[i]);
                    }
                }
                root["tools"]["highlighter"]["colors"] = highlighter_colors;
            }
            if (config.current_highlighter_color_index !=
                defaults.current_highlighter_color_index) {
                root["tools"]["highlighter"]["current_color"] =
                    config.current_highlighter_color_index;
            }
            if (config.highlighter_opacity_percent !=
                defaults.highlighter_opacity_percent) {
                root["tools"]["highlighter"]["opacity_percent"] =
                    config.highlighter_opacity_percent;
            }
            if (config.highlighter_pause_straighten_ms !=
                defaults.highlighter_pause_straighten_ms) {
                root["tools"]["highlighter"]["pause_straighten_ms"] =
                    config.highlighter_pause_straighten_ms;
            }
            if (config.highlighter_pause_straighten_deadzone_px !=
                defaults.highlighter_pause_straighten_deadzone_px) {
                root["tools"]["highlighter"]["pause_straighten_deadzone_px"] =
                    config.highlighter_pause_straighten_deadzone_px;
            }
        }
        if (config.text_size != defaults.text_size ||
            config.text_current_font != defaults.text_current_font) {
            root["tools"]["text"] = easyjson::object();
            if (config.text_size != defaults.text_size) {
                root["tools"]["text"]["size"] = config.text_size;
            }
            if (config.text_current_font != defaults.text_current_font) {
                root["tools"]["text"]["current_font"] =
                    std::string(Text_font_choice_token(config.text_current_font));
            }
        }
        if (config.bubble_size != defaults.bubble_size ||
            config.bubble_current_font != defaults.bubble_current_font) {
            root["tools"]["bubble"] = easyjson::object();
            if (config.bubble_size != defaults.bubble_size) {
                root["tools"]["bubble"]["size"] = config.bubble_size;
            }
            if (config.bubble_current_font != defaults.bubble_current_font) {
                root["tools"]["bubble"]["current_font"] =
                    std::string(Text_font_choice_token(config.bubble_current_font));
            }
        }
        if (config.obfuscate_block_size != defaults.obfuscate_block_size ||
            config.obfuscate_risk_acknowledged !=
                defaults.obfuscate_risk_acknowledged) {
            root["tools"]["obfuscate"] = easyjson::object();
            if (config.obfuscate_block_size != defaults.obfuscate_block_size) {
                root["tools"]["obfuscate"]["block_size"] =
                    config.obfuscate_block_size;
            }
            if (config.obfuscate_risk_acknowledged !=
                defaults.obfuscate_risk_acknowledged) {
                root["tools"]["obfuscate"]["risk_acknowledged"] =
                    config.obfuscate_risk_acknowledged;
            }
        }
    }

    bool const wrote_save = !config.default_save_dir.empty() ||
                            !config.last_save_as_dir.empty() ||
                            !config.filename_pattern_region.empty() ||
                            !config.filename_pattern_desktop.empty() ||
                            !config.filename_pattern_monitor.empty() ||
                            !config.filename_pattern_window.empty() ||
                            !config.default_save_format.empty() ||
                            config.padding_color != defaults.padding_color;

    if (wrote_save) {
        root["save"] = easyjson::object();
        if (!config.default_save_dir.empty()) {
            root["save"]["default_save_dir"] = To_utf8(config.default_save_dir);
        }
        if (!config.last_save_as_dir.empty()) {
            root["save"]["last_save_as_dir"] = To_utf8(config.last_save_as_dir);
        }
        if (!config.filename_pattern_region.empty()) {
            root["save"]["filename_pattern_region"] =
                To_utf8(config.filename_pattern_region);
        }
        if (!config.filename_pattern_desktop.empty()) {
            root["save"]["filename_pattern_desktop"] =
                To_utf8(config.filename_pattern_desktop);
        }
        if (!config.filename_pattern_monitor.empty()) {
            root["save"]["filename_pattern_monitor"] =
                To_utf8(config.filename_pattern_monitor);
        }
        if (!config.filename_pattern_window.empty()) {
            root["save"]["filename_pattern_window"] =
                To_utf8(config.filename_pattern_window);
        }
        if (!config.default_save_format.empty()) {
            root["save"]["default_save_format"] = To_utf8(config.default_save_format);
        }
        if (config.padding_color != defaults.padding_color) {
            root["save"]["padding_color"] = To_hex_color(config.padding_color);
        }
    }

    return root.dump();
}

} // namespace greenflame::core
