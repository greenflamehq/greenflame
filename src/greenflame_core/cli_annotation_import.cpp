#include "greenflame_core/cli_annotation_import.h"

#include "greenflame_core/color_wheel.h"

namespace greenflame::core {

namespace {

using Json = easyjson::JSON;
using JsonClass = easyjson::JSON::Class;

constexpr std::array<std::string_view, 6> kRootKeys = {
    {"$schema", "annotations", "coordinate_space", "color",
     "highlighter_opacity_percent", "font"}};
constexpr std::array<std::string_view, 2> kPointKeys = {{"x", "y"}};
constexpr std::array<std::string_view, 2> kFontKeys = {{"preset", "family"}};
constexpr std::array<std::string_view, 5> kLineKeys = {
    {"type", "start", "end", "size", "color"}};
constexpr std::array<std::string_view, 5> kArrowKeys = {
    {"type", "start", "end", "size", "color"}};
constexpr std::array<std::string_view, 4> kBrushKeys = {
    {"type", "points", "size", "color"}};
constexpr std::array<std::string_view, 7> kHighlighterKeys = {
    {"type", "start", "end", "points", "size", "color", "opacity_percent"}};
constexpr std::array<std::string_view, 7> kRectangleKeys = {
    {"type", "left", "top", "width", "height", "size", "color"}};
constexpr std::array<std::string_view, 6> kFilledRectangleKeys = {
    {"type", "left", "top", "width", "height", "color"}};
constexpr std::array<std::string_view, 6> kEllipseKeys = {
    {"type", "center", "width", "height", "size", "color"}};
constexpr std::array<std::string_view, 5> kFilledEllipseKeys = {
    {"type", "center", "width", "height", "color"}};
constexpr std::array<std::string_view, 6> kObfuscateKeys = {
    {"type", "left", "top", "width", "height", "size"}};
constexpr std::array<std::string_view, 7> kTextKeys = {
    {"type", "origin", "text", "spans", "font", "size", "color"}};
constexpr std::array<std::string_view, 5> kBubbleKeys = {
    {"type", "center", "font", "size", "color"}};
constexpr std::array<std::string_view, 5> kTextSpanKeys = {
    {"text", "bold", "italic", "underline", "strikethrough"}};
constexpr std::wstring_view kRootPath = L"$";
constexpr int32_t kMinOpacityPercent = 0;
constexpr int32_t kMaxOpacityPercent = 100;
constexpr size_t kHexPrefixChars = 1;
constexpr size_t kHexByteChars = 2;
constexpr size_t kRgbChannelCount = 3;
constexpr unsigned int kHexDigitBits = 4u;
constexpr char kColorPrefix = '#';
constexpr size_t kRedHexOffset = kHexPrefixChars;
constexpr size_t kGreenHexOffset = kRedHexOffset + kHexByteChars;
constexpr size_t kBlueHexOffset = kGreenHexOffset + kHexByteChars;
constexpr size_t kRgbColorStringChars =
    kHexPrefixChars + (kRgbChannelCount * kHexByteChars);
constexpr int32_t kHighlighterWidthStepOffsetPx = 10;
constexpr int32_t kBubbleDiameterStepOffsetPx = 20;

struct QuietCerrCapture final {
    QuietCerrCapture() : old_buffer_(std::cerr.rdbuf(stream_.rdbuf())) {}

    QuietCerrCapture(QuietCerrCapture const &) = delete;
    QuietCerrCapture &operator=(QuietCerrCapture const &) = delete;

    ~QuietCerrCapture() { std::cerr.rdbuf(old_buffer_); }

  private:
    std::ostringstream stream_ = {};
    std::streambuf *old_buffer_ = nullptr;
};

struct FontSpec final {
    std::optional<TextFontChoice> preset = std::nullopt;
    std::wstring family = {};
};

enum class CoordinateSpace : uint8_t {
    Local = 0,
    Global = 1,
};

struct Defaults final {
    std::optional<COLORREF> color = std::nullopt;
    std::optional<int32_t> highlighter_opacity_percent = std::nullopt;
    std::optional<FontSpec> font = std::nullopt;
};

struct ParseState final {
    CliAnnotationParseResult result = {};
    AppConfig const *config = nullptr;
    RectPx capture_rect_screen = {};
    RectPx virtual_desktop_bounds = {};
    CliAnnotationTargetKind target_kind = CliAnnotationTargetKind::Capture;
    CoordinateSpace coordinate_space = CoordinateSpace::Local;
    Defaults defaults = {};

    void Fail(std::wstring_view path, std::wstring_view message) noexcept {
        if (!result.ok && !result.error_message.empty()) {
            return;
        }

        std::wstring full = L"--annotate: ";
        if (!path.empty()) {
            full += path;
            if (!message.empty()) {
                full += L" ";
            }
        }
        full += message;
        result.error_message = std::move(full);
    }
};

class JsonSyntaxChecker final {
  public:
    explicit JsonSyntaxChecker(std::string_view text) : text_(text) {}

    [[nodiscard]] std::wstring Check_error() noexcept {
        Skip_whitespace();
        if (!Parse_value()) {
            return error_;
        }

        Skip_whitespace();
        if (!At_end() && error_.empty()) {
            error_ = L"Unexpected trailing characters after the root JSON value.";
        }
        return error_;
    }

  private:
    [[nodiscard]] bool At_end() const noexcept { return index_ >= text_.size(); }
    [[nodiscard]] char Peek() const noexcept { return At_end() ? '\0' : text_[index_]; }

    char Advance() noexcept {
        char const ch = Peek();
        if (!At_end()) {
            ++index_;
        }
        return ch;
    }

    void Skip_whitespace() noexcept {
        while (!At_end() && std::isspace(static_cast<unsigned char>(Peek())) != 0) {
            (void)Advance();
        }
    }

    [[nodiscard]] bool Fail(std::wstring_view message) noexcept {
        if (error_.empty()) {
            error_ = std::wstring(message);
        }
        return false;
    }

    [[nodiscard]] bool Parse_value() noexcept {
        Skip_whitespace();
        if (At_end()) {
            return Fail(L"Unexpected end of JSON input.");
        }

        switch (Peek()) {
        case '{':
            return Parse_object();
        case '[':
            return Parse_array();
        case '"':
            return Parse_string();
        case 't':
            return Parse_literal("true");
        case 'f':
            return Parse_literal("false");
        case 'n':
            return Parse_literal("null");
        default:
            break;
        }

        if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
            return Parse_number();
        }

        return Fail(L"Unexpected character while parsing a JSON value.");
    }

    [[nodiscard]] bool Parse_object() noexcept {
        (void)Advance();
        Skip_whitespace();
        if (At_end()) {
            return Fail(L"Unexpected end of JSON input inside an object.");
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
            if (At_end() || Peek() != ':') {
                return Fail(L"Expected ':' after an object property name.");
            }
            (void)Advance();
            if (!Parse_value()) {
                return false;
            }
            Skip_whitespace();
            if (At_end()) {
                return Fail(L"Unexpected end of JSON input inside an object.");
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
        }
    }

    [[nodiscard]] bool Parse_array() noexcept {
        (void)Advance();
        Skip_whitespace();
        if (At_end()) {
            return Fail(L"Unexpected end of JSON input inside an array.");
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
                return Fail(L"Unexpected end of JSON input inside an array.");
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
        }
    }

    [[nodiscard]] bool Parse_string() noexcept {
        if (Advance() != '"') {
            return Fail(L"Expected '\"' to begin a string.");
        }
        while (!At_end()) {
            char const ch = Advance();
            if (ch == '"') {
                return true;
            }
            if (static_cast<unsigned char>(ch) < 0x20u) {
                return Fail(L"Control characters are not allowed in JSON strings.");
            }
            if (ch != '\\') {
                continue;
            }
            if (At_end()) {
                return Fail(L"Incomplete escape sequence in JSON string.");
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
                for (int32_t digit = 0; digit < 4; ++digit) {
                    if (At_end() ||
                        std::isxdigit(static_cast<unsigned char>(Peek())) == 0) {
                        return Fail(L"Invalid \\u escape sequence in JSON string.");
                    }
                    (void)Advance();
                }
                break;
            default:
                return Fail(L"Invalid escape sequence in JSON string.");
            }
        }
        return Fail(L"Unterminated JSON string.");
    }

    [[nodiscard]] bool Parse_literal(char const *literal) noexcept {
        std::string_view const literal_text(literal);
        for (char const expected : literal_text) {
            if (At_end() || Advance() != expected) {
                return Fail(L"Invalid JSON literal.");
            }
        }
        return true;
    }

    [[nodiscard]] bool Parse_number() noexcept {
        if (Peek() == '-') {
            (void)Advance();
        }
        if (At_end()) {
            return Fail(L"Incomplete JSON number.");
        }
        if (Peek() == '0') {
            (void)Advance();
        } else if (std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
            while (!At_end() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
                (void)Advance();
            }
        } else {
            return Fail(L"Invalid JSON number.");
        }

        if (!At_end() && Peek() == '.') {
            (void)Advance();
            if (At_end() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
                return Fail(L"Invalid JSON number.");
            }
            while (!At_end() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
                (void)Advance();
            }
        }

        if (!At_end() && (Peek() == 'e' || Peek() == 'E')) {
            (void)Advance();
            if (!At_end() && (Peek() == '+' || Peek() == '-')) {
                (void)Advance();
            }
            if (At_end() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
                return Fail(L"Invalid JSON number.");
            }
            while (!At_end() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
                (void)Advance();
            }
        }

        return true;
    }

    std::string_view text_ = {};
    size_t index_ = 0;
    std::wstring error_ = {};
};

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
    std::wstring widened = {};
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

[[nodiscard]] std::wstring Join_index(std::wstring_view base, size_t index) {
    std::wstring path(base);
    path += L"[";
    path += std::to_wstring(index);
    path += L"]";
    return path;
}

void Report_unknown_keys(Json const &object,
                         std::span<const std::string_view> allowed_keys,
                         std::wstring_view path, ParseState &state) {
    if (object.JSON_type() != JsonClass::Object) {
        return;
    }
    for (auto const &[key, value] : object.object_range()) {
        (void)value;
        if (!Contains_key(allowed_keys, key)) {
            state.Fail(Join_path(path, key), L"contains an unknown property.");
            return;
        }
    }
}

[[nodiscard]] Json Load_json_silently(std::string_view json_text) noexcept {
    if (json_text.empty()) {
        return Json{};
    }

    QuietCerrCapture quiet_cerr;
    try {
        return Json::load(json_text);
    } catch (...) {
        return Json{};
    }
}

[[nodiscard]] std::string Get_json_string(Json const &value) noexcept {
    return value.Internal.String.has_value() ? *(value.Internal.String.value())
                                             : std::string{};
}

[[nodiscard]] bool Has_json_key(Json const &object, std::string_view key) {
    return object.has_key(std::string(key));
}

[[nodiscard]] Json const &Json_member(Json const &object, std::string_view key) {
    return object.at(std::string(key));
}

[[nodiscard]] Json const &Json_element(Json const &array, size_t index) {
    return array.at(static_cast<unsigned>(index));
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

[[nodiscard]] std::wstring Trim_copy(std::wstring_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::iswspace(value[begin]) != 0) {
        ++begin;
    }
    while (end > begin && std::iswspace(value[end - 1]) != 0) {
        --end;
    }
    return std::wstring(value.substr(begin, end - begin));
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
    if (value.size() != kHexByteChars) {
        return false;
    }
    uint8_t high = 0;
    uint8_t low = 0;
    if (!Try_parse_hex_digit(value[0], high) || !Try_parse_hex_digit(value[1], low)) {
        return false;
    }
    out = static_cast<uint8_t>((high << kHexDigitBits) | low);
    return true;
}

[[nodiscard]] bool Try_parse_color_string(std::string_view value,
                                          COLORREF &out) noexcept {
    if (value.size() != kRgbColorStringChars || value[0] != kColorPrefix) {
        return false;
    }

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (!Try_parse_hex_byte(value.substr(kRedHexOffset, kHexByteChars), red) ||
        !Try_parse_hex_byte(value.substr(kGreenHexOffset, kHexByteChars), green) ||
        !Try_parse_hex_byte(value.substr(kBlueHexOffset, kHexByteChars), blue)) {
        return false;
    }

    out = Make_colorref(red, green, blue);
    return true;
}

[[nodiscard]] std::wstring Normalize_text_newlines(std::wstring_view value) {
    std::wstring normalized = {};
    normalized.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        wchar_t const ch = value[index];
        if (ch == L'\r') {
            normalized.push_back(L'\n');
            if (index + 1 < value.size() && value[index + 1] == L'\n') {
                ++index;
            }
            continue;
        }
        normalized.push_back(ch);
    }
    return normalized;
}

[[nodiscard]] bool Try_add_int32(int32_t left, int32_t right, int32_t &out) noexcept {
    int64_t const sum = static_cast<int64_t>(left) + static_cast<int64_t>(right);
    if (sum < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
        sum > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
        return false;
    }
    out = static_cast<int32_t>(sum);
    return true;
}

[[nodiscard]] COLORREF Default_annotation_color(AppConfig const &config) noexcept {
    return config.annotation_colors[static_cast<size_t>(
        Clamp_annotation_color_index(config.current_annotation_color_index))];
}

[[nodiscard]] COLORREF Default_highlighter_color(AppConfig const &config) noexcept {
    return config.highlighter_colors[static_cast<size_t>(
        Clamp_highlighter_color_index(config.current_highlighter_color_index))];
}

[[nodiscard]] int32_t Default_size_step_for_type(std::wstring_view type,
                                                 AppConfig const &config) noexcept {
    if (type == L"brush") {
        return config.brush_size;
    }
    if (type == L"highlighter") {
        return config.highlighter_size;
    }
    if (type == L"line") {
        return config.line_size;
    }
    if (type == L"arrow") {
        return config.arrow_size;
    }
    if (type == L"rectangle" || type == L"filled_rectangle") {
        return config.rect_size;
    }
    if (type == L"ellipse" || type == L"filled_ellipse") {
        return config.ellipse_size;
    }
    if (type == L"obfuscate") {
        return config.obfuscate_block_size;
    }
    if (type == L"bubble") {
        return config.bubble_size;
    }
    if (type == L"text") {
        return config.text_size;
    }
    return 1;
}

[[nodiscard]] COLORREF Resolve_color(std::wstring_view type,
                                     std::optional<COLORREF> annotation_color,
                                     Defaults const &defaults,
                                     AppConfig const &config) noexcept {
    if (annotation_color.has_value()) {
        return *annotation_color;
    }
    if (defaults.color.has_value()) {
        return *defaults.color;
    }
    return type == L"highlighter" ? Default_highlighter_color(config)
                                  : Default_annotation_color(config);
}

[[nodiscard]] int32_t
Resolve_highlighter_opacity(std::optional<int32_t> annotation_opacity,
                            Defaults const &defaults,
                            AppConfig const &config) noexcept {
    if (annotation_opacity.has_value()) {
        return *annotation_opacity;
    }
    if (defaults.highlighter_opacity_percent.has_value()) {
        return *defaults.highlighter_opacity_percent;
    }
    return config.highlighter_opacity_percent;
}

[[nodiscard]] TextFontChoice
Default_font_choice_for_type(std::wstring_view type, AppConfig const &config) noexcept {
    if (type == L"bubble") {
        return Normalize_text_font_choice(config.bubble_current_font);
    }
    return Normalize_text_font_choice(config.text_current_font);
}

void Apply_font_override(TextAnnotationBaseStyle &style,
                         std::optional<FontSpec> const &annotation_font,
                         Defaults const &defaults, AppConfig const &config) {
    style.font_choice = Default_font_choice_for_type(L"text", config);
    style.font_family.clear();

    auto apply_font = [&](FontSpec const &font) {
        if (!font.family.empty()) {
            style.font_family = font.family;
        } else if (font.preset.has_value()) {
            style.font_choice = Normalize_text_font_choice(*font.preset);
        }
    };

    if (defaults.font.has_value()) {
        apply_font(*defaults.font);
    }
    if (annotation_font.has_value()) {
        apply_font(*annotation_font);
    }
}

void Apply_font_override(BubbleAnnotation &bubble,
                         std::optional<FontSpec> const &annotation_font,
                         Defaults const &defaults, AppConfig const &config) {
    bubble.font_choice = Default_font_choice_for_type(L"bubble", config);
    bubble.font_family.clear();

    auto apply_font = [&](FontSpec const &font) {
        if (!font.family.empty()) {
            bubble.font_family = font.family;
        } else if (font.preset.has_value()) {
            bubble.font_choice = Normalize_text_font_choice(*font.preset);
        }
    };

    if (defaults.font.has_value()) {
        apply_font(*defaults.font);
    }
    if (annotation_font.has_value()) {
        apply_font(*annotation_font);
    }
}

[[nodiscard]] bool Try_parse_color_property(Json const &object, std::string_view key,
                                            std::wstring_view path,
                                            std::optional<COLORREF> &out,
                                            ParseState &state) {
    if (!Has_json_key(object, key)) {
        out.reset();
        return true;
    }
    Json const &member = Json_member(object, key);
    if (member.JSON_type() != JsonClass::String) {
        state.Fail(Join_path(path, key), L"must be a #rrggbb color string.");
        return false;
    }
    COLORREF color = static_cast<COLORREF>(0);
    if (!Try_parse_color_string(Get_json_string(member), color)) {
        state.Fail(Join_path(path, key), L"must be a #rrggbb color string.");
        return false;
    }
    out = color;
    return true;
}

[[nodiscard]] bool Try_parse_opacity_property(Json const &object, std::string_view key,
                                              std::wstring_view path,
                                              std::optional<int32_t> &out,
                                              ParseState &state) {
    if (!Has_json_key(object, key)) {
        out.reset();
        return true;
    }

    Json const &member = Json_member(object, key);
    int32_t value = 0;
    if (!Try_read_int32(member, value) || value < kMinOpacityPercent ||
        value > kMaxOpacityPercent) {
        state.Fail(Join_path(path, key), L"must be an integer in 0..100.");
        return false;
    }
    out = value;
    return true;
}

[[nodiscard]] bool Try_parse_size_property(Json const &object, std::string_view key,
                                           std::wstring_view path,
                                           std::optional<int32_t> &out,
                                           ParseState &state) {
    if (!Has_json_key(object, key)) {
        out.reset();
        return true;
    }

    Json const &member = Json_member(object, key);
    int32_t value = 0;
    if (!Try_read_int32(member, value) || value < kMinToolSizeStep ||
        value > kMaxToolSizeStep) {
        state.Fail(Join_path(path, key), L"must be an integer in " +
                                             std::to_wstring(kMinToolSizeStep) + L".." +
                                             std::to_wstring(kMaxToolSizeStep) + L".");
        return false;
    }
    out = value;
    return true;
}

[[nodiscard]] bool Try_parse_required_int32(Json const &object, std::string_view key,
                                            std::wstring_view path, int32_t &out,
                                            ParseState &state) {
    if (!Has_json_key(object, key)) {
        state.Fail(Join_path(path, key), L"is required.");
        return false;
    }
    if (!Try_read_int32(Json_member(object, key), out)) {
        state.Fail(Join_path(path, key), L"must be an integer.");
        return false;
    }
    return true;
}

[[nodiscard]] bool Try_parse_bool_property(Json const &object, std::string_view key,
                                           std::wstring_view path, bool &out,
                                           ParseState &state) {
    if (!Has_json_key(object, key)) {
        out = false;
        return true;
    }
    Json const &member = Json_member(object, key);
    if (member.JSON_type() != JsonClass::Boolean) {
        state.Fail(Join_path(path, key), L"must be a boolean.");
        return false;
    }
    out = member.to_bool();
    return true;
}

[[nodiscard]] bool Try_parse_string_property(Json const &object, std::string_view key,
                                             std::wstring_view path, std::wstring &out,
                                             ParseState &state) {
    if (!Has_json_key(object, key)) {
        state.Fail(Join_path(path, key), L"is required.");
        return false;
    }
    Json const &member = Json_member(object, key);
    if (member.JSON_type() != JsonClass::String) {
        state.Fail(Join_path(path, key), L"must be a string.");
        return false;
    }
    if (!Try_decode_utf8(Get_json_string(member), out)) {
        state.Fail(Join_path(path, key), L"must be valid UTF-8.");
        return false;
    }
    return true;
}

[[nodiscard]] bool Try_parse_font_spec(Json const &value, std::wstring_view path,
                                       FontSpec &out, ParseState &state) {
    if (value.JSON_type() != JsonClass::Object) {
        state.Fail(path, L"must be an object.");
        return false;
    }
    Report_unknown_keys(value, kFontKeys, path, state);
    if (!state.result.error_message.empty()) {
        return false;
    }

    bool const has_preset = Has_json_key(value, "preset");
    bool const has_family = Has_json_key(value, "family");
    if (has_preset == has_family) {
        state.Fail(path, L"must contain exactly one of \"preset\" or \"family\".");
        return false;
    }

    out = {};
    if (has_preset) {
        Json const &preset_value = Json_member(value, "preset");
        if (preset_value.JSON_type() != JsonClass::String) {
            state.Fail(Join_path(path, "preset"), L"must be a string.");
            return false;
        }
        std::string const token = Get_json_string(preset_value);
        if (token == "sans") {
            out.preset = TextFontChoice::Sans;
            return true;
        }
        if (token == "serif") {
            out.preset = TextFontChoice::Serif;
            return true;
        }
        if (token == "mono") {
            out.preset = TextFontChoice::Mono;
            return true;
        }
        if (token == "art") {
            out.preset = TextFontChoice::Art;
            return true;
        }
        state.Fail(Join_path(path, "preset"),
                   L"must be one of: sans, serif, mono, art.");
        return false;
    }

    Json const &family_value = Json_member(value, "family");
    if (family_value.JSON_type() != JsonClass::String) {
        state.Fail(Join_path(path, "family"), L"must be a string.");
        return false;
    }

    std::wstring decoded = {};
    if (!Try_decode_utf8(Get_json_string(family_value), decoded)) {
        state.Fail(Join_path(path, "family"), L"must be valid UTF-8.");
        return false;
    }

    out.family = Trim_copy(decoded);
    if (out.family.empty()) {
        state.Fail(Join_path(path, "family"), L"must not be empty.");
        return false;
    }
    if (out.family.size() > kMaxTextFontFamilyChars) {
        state.Fail(Join_path(path, "family"),
                   L"must be at most 128 UTF-16 code units.");
        return false;
    }
    return true;
}

[[nodiscard]] bool Try_parse_optional_font_property(Json const &object,
                                                    std::string_view key,
                                                    std::wstring_view path,
                                                    std::optional<FontSpec> &out,
                                                    ParseState &state) {
    if (!Has_json_key(object, key)) {
        out.reset();
        return true;
    }

    FontSpec font = {};
    if (!Try_parse_font_spec(Json_member(object, key), Join_path(path, key), font,
                             state)) {
        return false;
    }
    out = std::move(font);
    return true;
}

[[nodiscard]] bool Try_translate_point(PointPx const &point, CoordinateSpace space,
                                       ParseState const &state, PointPx &out) noexcept {
    PointPx const origin = space == CoordinateSpace::Local
                               ? state.capture_rect_screen.Top_left()
                               : state.virtual_desktop_bounds.Top_left();
    return Try_add_int32(origin.x, point.x, out.x) &&
           Try_add_int32(origin.y, point.y, out.y);
}

[[nodiscard]] bool Try_parse_point(Json const &value, std::wstring_view path,
                                   PointPx &out, ParseState &state) {
    if (value.JSON_type() != JsonClass::Object) {
        state.Fail(path, L"must be an object.");
        return false;
    }
    Report_unknown_keys(value, kPointKeys, path, state);
    if (!state.result.error_message.empty()) {
        return false;
    }

    if (!Try_parse_required_int32(value, "x", path, out.x, state) ||
        !Try_parse_required_int32(value, "y", path, out.y, state)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool Try_parse_points_array(Json const &value, std::wstring_view path,
                                          std::vector<PointPx> &points,
                                          ParseState &state) {
    if (value.JSON_type() != JsonClass::Array) {
        state.Fail(path, L"must be an array.");
        return false;
    }

    points.clear();
    points.reserve(value.length());
    for (size_t index = 0; index < value.length(); ++index) {
        PointPx point{};
        if (!Try_parse_point(Json_element(value, index), Join_index(path, index), point,
                             state)) {
            return false;
        }
        points.push_back(point);
    }

    if (points.empty()) {
        state.Fail(path, L"must not be empty.");
        return false;
    }
    return true;
}

[[nodiscard]] bool Try_parse_line_like(Json const &object, std::wstring_view path,
                                       std::wstring_view type, bool arrow_head,
                                       ParseState &state, Annotation &out) {
    std::optional<COLORREF> annotation_color = std::nullopt;
    std::optional<int32_t> size_step = std::nullopt;
    PointPx start{};
    PointPx end{};

    if (!Try_parse_color_property(object, "color", path, annotation_color, state) ||
        !Try_parse_size_property(object, "size", path, size_step, state)) {
        return false;
    }
    if (!Has_json_key(object, "start") || !Has_json_key(object, "end")) {
        state.Fail(path, L"must contain both \"start\" and \"end\".");
        return false;
    }
    if (!Try_parse_point(Json_member(object, "start"), Join_path(path, "start"), start,
                         state) ||
        !Try_parse_point(Json_member(object, "end"), Join_path(path, "end"), end,
                         state)) {
        return false;
    }

    PointPx screen_start{};
    PointPx screen_end{};
    if (!Try_translate_point(start, state.coordinate_space, state, screen_start) ||
        !Try_translate_point(end, state.coordinate_space, state, screen_end)) {
        state.Fail(path, L"overflows screen-space coordinates.");
        return false;
    }

    LineAnnotation line{};
    line.start = screen_start;
    line.end = screen_end;
    line.style.width_px =
        size_step.value_or(Default_size_step_for_type(type, *state.config));
    line.style.color =
        Resolve_color(type, annotation_color, state.defaults, *state.config);
    line.style.opacity_percent = StrokeStyle::kDefaultOpacityPercent;
    line.arrow_head = arrow_head;
    out.data = line;
    return true;
}

[[nodiscard]] bool Try_parse_brush(Json const &object, std::wstring_view path,
                                   ParseState &state, Annotation &out) {
    std::optional<COLORREF> annotation_color = std::nullopt;
    std::optional<int32_t> size_step = std::nullopt;
    std::vector<PointPx> points = {};
    if (!Try_parse_color_property(object, "color", path, annotation_color, state) ||
        !Try_parse_size_property(object, "size", path, size_step, state)) {
        return false;
    }
    if (!Has_json_key(object, "points")) {
        state.Fail(Join_path(path, "points"), L"is required.");
        return false;
    }
    if (!Try_parse_points_array(Json_member(object, "points"),
                                Join_path(path, "points"), points, state)) {
        return false;
    }

    FreehandStrokeAnnotation stroke{};
    stroke.points.reserve(points.size());
    for (PointPx const &point : points) {
        PointPx translated{};
        if (!Try_translate_point(point, state.coordinate_space, state, translated)) {
            state.Fail(path, L"overflows screen-space coordinates.");
            return false;
        }
        stroke.points.push_back(translated);
    }
    stroke.style.width_px =
        size_step.value_or(Default_size_step_for_type(L"brush", *state.config));
    stroke.style.color =
        Resolve_color(L"brush", annotation_color, state.defaults, *state.config);
    stroke.style.opacity_percent = StrokeStyle::kDefaultOpacityPercent;
    stroke.freehand_tip_shape = FreehandTipShape::Round;
    out.data = std::move(stroke);
    return true;
}

[[nodiscard]] bool Try_parse_highlighter(Json const &object, std::wstring_view path,
                                         ParseState &state, Annotation &out) {
    std::optional<COLORREF> annotation_color = std::nullopt;
    std::optional<int32_t> opacity_percent = std::nullopt;
    std::optional<int32_t> size_step = std::nullopt;
    if (!Try_parse_color_property(object, "color", path, annotation_color, state) ||
        !Try_parse_opacity_property(object, "opacity_percent", path, opacity_percent,
                                    state) ||
        !Try_parse_size_property(object, "size", path, size_step, state)) {
        return false;
    }

    bool const has_start = Has_json_key(object, "start");
    bool const has_end = Has_json_key(object, "end");
    bool const has_points = Has_json_key(object, "points");
    if ((has_start || has_end) == has_points || has_start != has_end) {
        state.Fail(path,
                   L"must contain exactly one geometry form: \"start\"+\"end\" or "
                   L"\"points\".");
        return false;
    }

    std::vector<PointPx> points = {};
    if (has_points) {
        if (!Try_parse_points_array(Json_member(object, "points"),
                                    Join_path(path, "points"), points, state)) {
            return false;
        }
    } else {
        PointPx start{};
        PointPx end{};
        if (!Try_parse_point(Json_member(object, "start"), Join_path(path, "start"),
                             start, state) ||
            !Try_parse_point(Json_member(object, "end"), Join_path(path, "end"), end,
                             state)) {
            return false;
        }
        points = {start, end};
    }

    FreehandStrokeAnnotation stroke{};
    stroke.points.reserve(points.size());
    for (PointPx const &point : points) {
        PointPx translated{};
        if (!Try_translate_point(point, state.coordinate_space, state, translated)) {
            state.Fail(path, L"overflows screen-space coordinates.");
            return false;
        }
        stroke.points.push_back(translated);
    }

    int32_t const step =
        size_step.value_or(Default_size_step_for_type(L"highlighter", *state.config));
    stroke.style.width_px = step + kHighlighterWidthStepOffsetPx;
    stroke.style.color =
        Resolve_color(L"highlighter", annotation_color, state.defaults, *state.config);
    stroke.style.opacity_percent =
        Resolve_highlighter_opacity(opacity_percent, state.defaults, *state.config);
    stroke.freehand_tip_shape = FreehandTipShape::Square;
    out.data = std::move(stroke);
    return true;
}

[[nodiscard]] bool Try_parse_rectangle(Json const &object, std::wstring_view path,
                                       std::wstring_view type, bool filled,
                                       ParseState &state, Annotation &out) {
    std::optional<COLORREF> annotation_color = std::nullopt;
    std::optional<int32_t> size_step = std::nullopt;
    int32_t left = 0;
    int32_t top = 0;
    int32_t width = 0;
    int32_t height = 0;

    if (!Try_parse_color_property(object, "color", path, annotation_color, state) ||
        !Try_parse_required_int32(object, "left", path, left, state) ||
        !Try_parse_required_int32(object, "top", path, top, state) ||
        !Try_parse_required_int32(object, "width", path, width, state) ||
        !Try_parse_required_int32(object, "height", path, height, state)) {
        return false;
    }
    if (filled) {
        if (Has_json_key(object, "size")) {
            state.Fail(Join_path(path, "size"),
                       L"must not be present for filled shapes.");
            return false;
        }
    } else if (!Try_parse_size_property(object, "size", path, size_step, state)) {
        return false;
    }

    if (width <= 0 || height <= 0) {
        state.Fail(path, L"requires positive width and height.");
        return false;
    }

    PointPx translated_top_left{};
    if (!Try_translate_point(PointPx{left, top}, state.coordinate_space, state,
                             translated_top_left)) {
        state.Fail(path, L"overflows screen-space coordinates.");
        return false;
    }
    int32_t right = 0;
    int32_t bottom = 0;
    if (!Try_add_int32(translated_top_left.x, width, right) ||
        !Try_add_int32(translated_top_left.y, height, bottom)) {
        state.Fail(path, L"overflows rectangle bounds.");
        return false;
    }

    RectangleAnnotation rect{};
    rect.outer_bounds =
        RectPx::From_ltrb(translated_top_left.x, translated_top_left.y, right, bottom);
    rect.style.width_px =
        size_step.value_or(Default_size_step_for_type(type, *state.config));
    rect.style.color =
        Resolve_color(type, annotation_color, state.defaults, *state.config);
    rect.style.opacity_percent = StrokeStyle::kDefaultOpacityPercent;
    rect.filled = filled;
    out.data = rect;
    return true;
}

[[nodiscard]] bool Try_parse_ellipse(Json const &object, std::wstring_view path,
                                     std::wstring_view type, bool filled,
                                     ParseState &state, Annotation &out) {
    std::optional<COLORREF> annotation_color = std::nullopt;
    std::optional<int32_t> size_step = std::nullopt;
    PointPx center{};
    int32_t width = 0;
    int32_t height = 0;

    if (!Try_parse_color_property(object, "color", path, annotation_color, state) ||
        !Has_json_key(object, "center") ||
        !Try_parse_point(Json_member(object, "center"), Join_path(path, "center"),
                         center, state) ||
        !Try_parse_required_int32(object, "width", path, width, state) ||
        !Try_parse_required_int32(object, "height", path, height, state)) {
        return false;
    }
    if (filled) {
        if (Has_json_key(object, "size")) {
            state.Fail(Join_path(path, "size"),
                       L"must not be present for filled shapes.");
            return false;
        }
    } else if (!Try_parse_size_property(object, "size", path, size_step, state)) {
        return false;
    }

    if (width <= 0 || height <= 0) {
        state.Fail(path, L"requires positive width and height.");
        return false;
    }

    PointPx screen_center{};
    if (!Try_translate_point(center, state.coordinate_space, state, screen_center)) {
        state.Fail(path, L"overflows screen-space coordinates.");
        return false;
    }

    int32_t left = 0;
    int32_t top = 0;
    if (!Try_add_int32(screen_center.x, -(width / 2), left) ||
        !Try_add_int32(screen_center.y, -(height / 2), top)) {
        state.Fail(path, L"overflows ellipse bounds.");
        return false;
    }

    int32_t right = 0;
    int32_t bottom = 0;
    if (!Try_add_int32(left, width, right) || !Try_add_int32(top, height, bottom)) {
        state.Fail(path, L"overflows ellipse bounds.");
        return false;
    }

    EllipseAnnotation ellipse{};
    ellipse.outer_bounds = RectPx::From_ltrb(left, top, right, bottom);
    ellipse.style.width_px =
        size_step.value_or(Default_size_step_for_type(type, *state.config));
    ellipse.style.color =
        Resolve_color(type, annotation_color, state.defaults, *state.config);
    ellipse.style.opacity_percent = StrokeStyle::kDefaultOpacityPercent;
    ellipse.filled = filled;
    out.data = ellipse;
    return true;
}

[[nodiscard]] bool Try_parse_obfuscate(Json const &object, std::wstring_view path,
                                       ParseState &state, Annotation &out) {
    std::optional<int32_t> size_step = std::nullopt;
    int32_t left = 0;
    int32_t top = 0;
    int32_t width = 0;
    int32_t height = 0;
    if (!Try_parse_size_property(object, "size", path, size_step, state) ||
        !Try_parse_required_int32(object, "left", path, left, state) ||
        !Try_parse_required_int32(object, "top", path, top, state) ||
        !Try_parse_required_int32(object, "width", path, width, state) ||
        !Try_parse_required_int32(object, "height", path, height, state)) {
        return false;
    }

    if (width <= 0 || height <= 0) {
        state.Fail(path, L"requires positive width and height.");
        return false;
    }

    PointPx screen_top_left{};
    if (!Try_translate_point(PointPx{left, top}, state.coordinate_space, state,
                             screen_top_left)) {
        state.Fail(path, L"overflows screen-space coordinates.");
        return false;
    }

    int32_t right = 0;
    int32_t bottom = 0;
    if (!Try_add_int32(screen_top_left.x, width, right) ||
        !Try_add_int32(screen_top_left.y, height, bottom)) {
        state.Fail(path, L"overflows rectangle bounds.");
        return false;
    }

    ObfuscateAnnotation obfuscate{};
    obfuscate.bounds =
        RectPx::From_ltrb(screen_top_left.x, screen_top_left.y, right, bottom);
    obfuscate.block_size =
        size_step.value_or(Default_size_step_for_type(L"obfuscate", *state.config));
    out.data = std::move(obfuscate);
    return true;
}

[[nodiscard]] bool Try_parse_text(Json const &object, std::wstring_view path,
                                  ParseState &state, Annotation &out) {
    std::optional<COLORREF> annotation_color = std::nullopt;
    std::optional<FontSpec> annotation_font = std::nullopt;
    std::optional<int32_t> size_step = std::nullopt;
    PointPx origin{};
    if (!Try_parse_color_property(object, "color", path, annotation_color, state) ||
        !Try_parse_optional_font_property(object, "font", path, annotation_font,
                                          state) ||
        !Try_parse_size_property(object, "size", path, size_step, state)) {
        return false;
    }
    if (!Has_json_key(object, "origin")) {
        state.Fail(Join_path(path, "origin"), L"is required.");
        return false;
    }
    if (!Try_parse_point(Json_member(object, "origin"), Join_path(path, "origin"),
                         origin, state)) {
        return false;
    }

    bool const has_text = Has_json_key(object, "text");
    bool const has_spans = Has_json_key(object, "spans");
    if (has_text == has_spans) {
        state.Fail(path, L"must contain exactly one of \"text\" or \"spans\".");
        return false;
    }

    std::vector<TextRun> runs = {};
    if (has_text) {
        std::wstring text = {};
        if (!Try_parse_string_property(object, "text", path, text, state)) {
            return false;
        }
        text = Normalize_text_newlines(text);
        if (text.empty()) {
            state.Fail(Join_path(path, "text"), L"must not be empty.");
            return false;
        }
        runs.push_back(TextRun{std::move(text), {}});
    } else {
        Json const &spans = Json_member(object, "spans");
        if (spans.JSON_type() != JsonClass::Array) {
            state.Fail(Join_path(path, "spans"), L"must be an array.");
            return false;
        }
        if (spans.length() == 0) {
            state.Fail(Join_path(path, "spans"), L"must not be empty.");
            return false;
        }
        runs.reserve(spans.length());
        for (size_t index = 0; index < spans.length(); ++index) {
            Json const &span = Json_element(spans, index);
            std::wstring const span_path = Join_index(Join_path(path, "spans"), index);
            if (span.JSON_type() != JsonClass::Object) {
                state.Fail(span_path, L"must be an object.");
                return false;
            }
            Report_unknown_keys(span, kTextSpanKeys, span_path, state);
            if (!state.result.error_message.empty()) {
                return false;
            }

            std::wstring text = {};
            if (!Try_parse_string_property(span, "text", span_path, text, state)) {
                return false;
            }
            text = Normalize_text_newlines(text);
            if (text.empty()) {
                state.Fail(Join_path(span_path, "text"), L"must not be empty.");
                return false;
            }

            TextStyleFlags flags{};
            if (!Try_parse_bool_property(span, "bold", span_path, flags.bold, state) ||
                !Try_parse_bool_property(span, "italic", span_path, flags.italic,
                                         state) ||
                !Try_parse_bool_property(span, "underline", span_path, flags.underline,
                                         state) ||
                !Try_parse_bool_property(span, "strikethrough", span_path,
                                         flags.strikethrough, state)) {
                return false;
            }

            if (!runs.empty() && runs.back().flags == flags) {
                runs.back().text += text;
            } else {
                runs.push_back(TextRun{std::move(text), flags});
            }
        }
    }

    PointPx screen_origin{};
    if (!Try_translate_point(origin, state.coordinate_space, state, screen_origin)) {
        state.Fail(path, L"overflows screen-space coordinates.");
        return false;
    }

    TextAnnotation text{};
    text.origin = screen_origin;
    text.base_style.color =
        Resolve_color(L"text", annotation_color, state.defaults, *state.config);
    text.base_style.point_size = Text_point_size_from_step(
        size_step.value_or(Default_size_step_for_type(L"text", *state.config)));
    Apply_font_override(text.base_style, annotation_font, state.defaults,
                        *state.config);
    text.runs = std::move(runs);
    out.data = std::move(text);
    return true;
}

[[nodiscard]] bool Try_parse_bubble(Json const &object, std::wstring_view path,
                                    ParseState &state, Annotation &out) {
    std::optional<COLORREF> annotation_color = std::nullopt;
    std::optional<FontSpec> annotation_font = std::nullopt;
    std::optional<int32_t> size_step = std::nullopt;
    PointPx center{};
    if (!Try_parse_color_property(object, "color", path, annotation_color, state) ||
        !Try_parse_optional_font_property(object, "font", path, annotation_font,
                                          state) ||
        !Try_parse_size_property(object, "size", path, size_step, state)) {
        return false;
    }
    if (!Has_json_key(object, "center")) {
        state.Fail(Join_path(path, "center"), L"is required.");
        return false;
    }
    if (!Try_parse_point(Json_member(object, "center"), Join_path(path, "center"),
                         center, state)) {
        return false;
    }

    PointPx screen_center{};
    if (!Try_translate_point(center, state.coordinate_space, state, screen_center)) {
        state.Fail(path, L"overflows screen-space coordinates.");
        return false;
    }

    BubbleAnnotation bubble{};
    bubble.center = screen_center;
    bubble.diameter_px =
        size_step.value_or(Default_size_step_for_type(L"bubble", *state.config)) +
        kBubbleDiameterStepOffsetPx;
    bubble.color =
        Resolve_color(L"bubble", annotation_color, state.defaults, *state.config);
    Apply_font_override(bubble, annotation_font, state.defaults, *state.config);
    out.data = std::move(bubble);
    return true;
}

[[nodiscard]] bool Try_parse_annotation(Json const &object, std::wstring_view path,
                                        ParseState &state, Annotation &out) {
    if (object.JSON_type() != JsonClass::Object) {
        state.Fail(path, L"must be an object.");
        return false;
    }
    if (!Has_json_key(object, "type")) {
        state.Fail(path, L"must contain \"type\".");
        return false;
    }
    Json const &type_value = Json_member(object, "type");
    if (type_value.JSON_type() != JsonClass::String) {
        state.Fail(Join_path(path, "type"), L"must be a string.");
        return false;
    }

    std::wstring type = {};
    if (!Try_decode_utf8(Get_json_string(type_value), type)) {
        state.Fail(Join_path(path, "type"), L"must be valid UTF-8.");
        return false;
    }

    out = {};
    if (type == L"brush") {
        Report_unknown_keys(object, kBrushKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_brush(object, path, state, out);
    }
    if (type == L"highlighter") {
        Report_unknown_keys(object, kHighlighterKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_highlighter(object, path, state, out);
    }
    if (type == L"line") {
        Report_unknown_keys(object, kLineKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_line_like(object, path, L"line", false, state, out);
    }
    if (type == L"arrow") {
        Report_unknown_keys(object, kArrowKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_line_like(object, path, L"arrow", true, state, out);
    }
    if (type == L"rectangle") {
        Report_unknown_keys(object, kRectangleKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_rectangle(object, path, L"rectangle", false, state, out);
    }
    if (type == L"filled_rectangle") {
        Report_unknown_keys(object, kFilledRectangleKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_rectangle(object, path, L"filled_rectangle", true, state, out);
    }
    if (type == L"ellipse") {
        Report_unknown_keys(object, kEllipseKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_ellipse(object, path, L"ellipse", false, state, out);
    }
    if (type == L"filled_ellipse") {
        Report_unknown_keys(object, kFilledEllipseKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_ellipse(object, path, L"filled_ellipse", true, state, out);
    }
    if (type == L"obfuscate") {
        Report_unknown_keys(object, kObfuscateKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_obfuscate(object, path, state, out);
    }
    if (type == L"text") {
        Report_unknown_keys(object, kTextKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_text(object, path, state, out);
    }
    if (type == L"bubble") {
        Report_unknown_keys(object, kBubbleKeys, path, state);
        return state.result.error_message.empty() &&
               Try_parse_bubble(object, path, state, out);
    }

    state.Fail(Join_path(path, "type"), L"must be a supported annotation type.");
    return false;
}

void Assign_bubble_numbers(std::vector<Annotation> &annotations) {
    int32_t next_bubble = 1;
    for (Annotation &annotation : annotations) {
        if (!std::holds_alternative<BubbleAnnotation>(annotation.data)) {
            continue;
        }
        std::get<BubbleAnnotation>(annotation.data).counter_value = next_bubble;
        ++next_bubble;
    }
}

} // namespace

CliAnnotationInputKind Classify_cli_annotation_input(std::wstring_view value) noexcept {
    size_t index = 0;
    while (index < value.size() && std::iswspace(value[index]) != 0) {
        ++index;
    }
    if (index < value.size() && value[index] == L'{') {
        return CliAnnotationInputKind::InlineJson;
    }
    return CliAnnotationInputKind::FilePath;
}

std::array<std::wstring, 4> Resolve_text_font_families(AppConfig const &config) {
    return {{config.text_font_sans, config.text_font_serif, config.text_font_mono,
             config.text_font_art}};
}

CliAnnotationParseResult
Parse_cli_annotations_json(std::string_view json_text,
                           CliAnnotationParseContext const &context) noexcept {
    ParseState state{};
    state.config = context.config;
    state.capture_rect_screen = context.capture_rect_screen;
    state.virtual_desktop_bounds = context.virtual_desktop_bounds;
    state.target_kind = context.target_kind;

    if (state.config == nullptr) {
        state.Fail(kRootPath, L"internal annotation parse context is missing config.");
        return state.result;
    }

    std::wstring const syntax_error = JsonSyntaxChecker(json_text).Check_error();
    if (!syntax_error.empty()) {
        state.Fail(kRootPath, syntax_error);
        return state.result;
    }

    Json const root = Load_json_silently(json_text);
    if (root.JSON_type() != JsonClass::Object) {
        state.Fail(kRootPath, L"top-level JSON value must be an object.");
        return state.result;
    }

    Report_unknown_keys(root, kRootKeys, kRootPath, state);
    if (!state.result.error_message.empty()) {
        return state.result;
    }

    if (Has_json_key(root, "$schema") &&
        Json_member(root, "$schema").JSON_type() != JsonClass::String) {
        state.Fail(Join_path(kRootPath, "$schema"), L"must be a string.");
        return state.result;
    }

    if (Has_json_key(root, "coordinate_space")) {
        Json const &coordinate_space_value = Json_member(root, "coordinate_space");
        if (coordinate_space_value.JSON_type() != JsonClass::String) {
            state.Fail(Join_path(kRootPath, "coordinate_space"),
                       L"must be \"local\" or \"global\".");
            return state.result;
        }
        std::string const coordinate_space = Get_json_string(coordinate_space_value);
        if (coordinate_space == "local") {
            state.coordinate_space = CoordinateSpace::Local;
        } else if (coordinate_space == "global") {
            if (state.target_kind == CliAnnotationTargetKind::InputImage) {
                state.Fail(Join_path(kRootPath, "coordinate_space"),
                           L"\"global\" is not supported with --input.");
                return state.result;
            }
            state.coordinate_space = CoordinateSpace::Global;
        } else {
            state.Fail(Join_path(kRootPath, "coordinate_space"),
                       L"must be \"local\" or \"global\".");
            return state.result;
        }
    }

    if (!Try_parse_color_property(root, "color", kRootPath, state.defaults.color,
                                  state) ||
        !Try_parse_opacity_property(root, "highlighter_opacity_percent", kRootPath,
                                    state.defaults.highlighter_opacity_percent,
                                    state) ||
        !Try_parse_optional_font_property(root, "font", kRootPath, state.defaults.font,
                                          state)) {
        return state.result;
    }

    if (!Has_json_key(root, "annotations")) {
        state.Fail(Join_path(kRootPath, "annotations"), L"is required.");
        return state.result;
    }
    Json const &annotations = Json_member(root, "annotations");
    if (annotations.JSON_type() != JsonClass::Array) {
        state.Fail(Join_path(kRootPath, "annotations"), L"must be an array.");
        return state.result;
    }

    std::vector<Annotation> translated = {};
    translated.reserve(annotations.length());
    for (size_t index = 0; index < annotations.length(); ++index) {
        Annotation annotation{};
        annotation.id = static_cast<uint64_t>(index + 1);
        if (!Try_parse_annotation(
                Json_element(annotations, index),
                Join_index(Join_path(kRootPath, "annotations"), index), state,
                annotation)) {
            return state.result;
        }
        translated.push_back(std::move(annotation));
    }

    Assign_bubble_numbers(translated);
    state.result.annotations = std::move(translated);
    state.result.ok = true;
    return state.result;
}

} // namespace greenflame::core
