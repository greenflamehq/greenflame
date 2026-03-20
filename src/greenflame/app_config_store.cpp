#include "app_config_store.h"

namespace greenflame {

namespace {

constexpr int64_t kInt32MaxValue = std::numeric_limits<int32_t>::max();
constexpr int64_t kInt32MinMagnitude = kInt32MaxValue + 1;
constexpr size_t kHexColorTextLength = 7;
constexpr size_t kHexColorComponentLength = 2;
constexpr size_t kHexColorRedOffset = 1;
constexpr size_t kHexColorGreenOffset = 3;
constexpr size_t kHexColorBlueOffset = 5;
constexpr COLORREF kColorChannelMask = 0xFF;
constexpr uint8_t kHexLowNibbleMask = 0x0F;

[[nodiscard]] std::filesystem::path Get_config_path() {
    wchar_t home[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, home))) {
        return {};
    }
    std::filesystem::path path(home);
    path /= L".config";
    path /= L"greenflame";
    path /= L"greenflame.json";
    return path;
}

[[nodiscard]] std::wstring To_wide(std::string const &value) {
    if (value.empty()) {
        return {};
    }
    int const required_chars =
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required_chars <= 1) {
        return {};
    }
    std::wstring out(static_cast<size_t>(required_chars - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), required_chars);
    return out;
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

[[nodiscard]] std::string_view Trim(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r')) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

[[nodiscard]] bool Try_parse_int32(std::string_view value, int32_t &out) {
    std::string_view const trimmed = Trim(value);
    if (trimmed.empty()) {
        return false;
    }

    bool negative = false;
    size_t pos = 0;
    if (trimmed[0] == '+' || trimmed[0] == '-') {
        negative = trimmed[0] == '-';
        pos = 1;
    }
    if (pos >= trimmed.size()) {
        return false;
    }

    int64_t parsed = 0;
    for (; pos < trimmed.size(); ++pos) {
        char const ch = trimmed[pos];
        if (ch < '0' || ch > '9') {
            return false;
        }
        parsed = parsed * 10 + static_cast<int64_t>(ch - '0');
        if (!negative && parsed > kInt32MaxValue) {
            out = std::numeric_limits<int32_t>::max();
            return true;
        }
        if (negative && parsed > kInt32MinMagnitude) {
            out = std::numeric_limits<int32_t>::min();
            return true;
        }
    }

    if (negative) {
        parsed = -parsed;
    }
    out = static_cast<int32_t>(parsed);
    return true;
}

[[nodiscard]] bool Try_parse_hex_digit(char ch, uint8_t &value) {
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

[[nodiscard]] bool Try_parse_hex_byte(std::string_view value, uint8_t &out) {
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

[[nodiscard]] bool Try_parse_color(std::string_view value, COLORREF &out) {
    std::string_view const trimmed = Trim(value);
    if (trimmed.size() != kHexColorTextLength || trimmed[0] != '#') {
        return false;
    }

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (!Try_parse_hex_byte(
            trimmed.substr(kHexColorRedOffset, kHexColorComponentLength), red) ||
        !Try_parse_hex_byte(
            trimmed.substr(kHexColorGreenOffset, kHexColorComponentLength), green) ||
        !Try_parse_hex_byte(
            trimmed.substr(kHexColorBlueOffset, kHexColorComponentLength), blue)) {
        return false;
    }

    out = core::Make_colorref(red, green, blue);
    return true;
}

[[nodiscard]] char Hex_digit(uint8_t value) {
    return static_cast<char>((value < 10) ? ('0' + value) : ('a' + (value - 10)));
}

[[nodiscard]] core::TextFontChoice Parse_text_font_choice(std::string_view value) {
    std::string_view const trimmed = Trim(value);
    if (trimmed == "serif") {
        return core::TextFontChoice::Serif;
    }
    if (trimmed == "mono") {
        return core::TextFontChoice::Mono;
    }
    if (trimmed == "art") {
        return core::TextFontChoice::Art;
    }
    return core::TextFontChoice::Sans;
}

[[nodiscard]] std::string_view Text_font_choice_token(core::TextFontChoice choice) {
    switch (choice) {
    case core::TextFontChoice::Sans:
        return "sans";
    case core::TextFontChoice::Serif:
        return "serif";
    case core::TextFontChoice::Mono:
        return "mono";
    case core::TextFontChoice::Art:
        return "art";
    }
    return "sans";
}

[[nodiscard]] bool Read_int_from_json(easyjson::JSON const &j, int32_t &out) {
    using Class = easyjson::JSON::Class;
    if (j.JSON_type() == Class::Integral) {
        int64_t const v = static_cast<int64_t>(j.to_int());
        if (v >= std::numeric_limits<int32_t>::min() &&
            v <= std::numeric_limits<int32_t>::max()) {
            out = static_cast<int32_t>(v);
            return true;
        }
        return false;
    }
    if (j.JSON_type() == Class::Floating) {
        double const v = j.to_float();
        if (v >= std::numeric_limits<int32_t>::min() &&
            v <= std::numeric_limits<int32_t>::max()) {
            out = static_cast<int32_t>(v);
            return true;
        }
        return false;
    }
    if (j.JSON_type() == Class::String) {
        return Try_parse_int32(j.to_string(), out);
    }
    return false;
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

void Load_colors_from_json(easyjson::JSON const &j, core::AnnotationColorPalette &out,
                           size_t max_count) {
    if (j.JSON_type() == easyjson::JSON::Class::Object) {
        for (auto const &[key, val] : j.object_range()) {
            int32_t index = 0;
            if (!Try_parse_int32(key, index) || index < 0 ||
                index >= static_cast<int32_t>(max_count)) {
                continue;
            }
            std::string const s = val.to_string();
            COLORREF parsed = 0;
            if (Try_parse_color(s, parsed)) {
                out[static_cast<size_t>(index)] = parsed;
            }
        }
        return;
    }
    if (j.JSON_type() == easyjson::JSON::Class::Array) {
        auto const items = j.to_deque();
        for (size_t i = 0; i < max_count && i < items.size(); ++i) {
            std::string const s = items[i].to_string();
            COLORREF parsed = 0;
            if (Try_parse_color(s, parsed)) {
                out[i] = parsed;
            }
        }
    }
}

void Load_colors_from_json(easyjson::JSON const &j, core::HighlighterColorPalette &out,
                           size_t max_count) {
    if (j.JSON_type() == easyjson::JSON::Class::Object) {
        for (auto const &[key, val] : j.object_range()) {
            int32_t index = 0;
            if (!Try_parse_int32(key, index) || index < 0 ||
                index >= static_cast<int32_t>(max_count)) {
                continue;
            }
            std::string const s = val.to_string();
            COLORREF parsed = 0;
            if (Try_parse_color(s, parsed)) {
                out[static_cast<size_t>(index)] = parsed;
            }
        }
        return;
    }
    if (j.JSON_type() == easyjson::JSON::Class::Array) {
        auto const items = j.to_deque();
        for (size_t i = 0; i < max_count && i < items.size(); ++i) {
            std::string const s = items[i].to_string();
            COLORREF parsed = 0;
            if (Try_parse_color(s, parsed)) {
                out[i] = parsed;
            }
        }
    }
}

} // namespace

std::filesystem::path Get_app_config_dir() {
    std::filesystem::path const path = Get_config_path();
    if (path.empty()) {
        return {};
    }
    return path.parent_path();
}

std::filesystem::path Get_config_file_path() { return Get_config_path(); }

core::AppConfig Load_app_config() {
    core::AppConfig config;
    std::filesystem::path const path = Get_config_path();
    if (path.empty() || !std::filesystem::exists(path)) {
        (void)Save_app_config(config); // creates dir + empty file on first run
        return config;
    }
    try {
        easyjson::JSON root = easyjson::JSON::load_file(path.string());
        if (root.JSON_type() != easyjson::JSON::Class::Object) {
            config.Normalize();
            return config;
        }

        if (root.has_key("ui")) {
            easyjson::JSON const &ui = root["ui"];
            if (ui.has_key("show_balloons") &&
                ui["show_balloons"].JSON_type() == easyjson::JSON::Class::Boolean) {
                config.show_balloons = ui["show_balloons"].to_bool();
            }
            if (ui.has_key("show_selection_size_side_labels") &&
                ui["show_selection_size_side_labels"].JSON_type() ==
                    easyjson::JSON::Class::Boolean) {
                config.show_selection_size_side_labels =
                    ui["show_selection_size_side_labels"].to_bool();
            }
            if (ui.has_key("show_selection_size_center_label") &&
                ui["show_selection_size_center_label"].JSON_type() ==
                    easyjson::JSON::Class::Boolean) {
                config.show_selection_size_center_label =
                    ui["show_selection_size_center_label"].to_bool();
            }
            if (ui.has_key("tool_size_overlay_duration_ms")) {
                int32_t parsed = 0;
                if (Read_int_from_json(ui["tool_size_overlay_duration_ms"], parsed)) {
                    config.tool_size_overlay_duration_ms = parsed;
                }
            }
        }

        if (root.has_key("tools")) {
            easyjson::JSON const &tools = root["tools"];
            if (tools.has_key("brush")) {
                easyjson::JSON const &freehand = tools["brush"];
                if (freehand.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(freehand["size"], parsed)) {
                        config.brush_size = parsed;
                    }
                }
            }
            if (tools.has_key("line")) {
                easyjson::JSON const &line = tools["line"];
                if (line.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(line["size"], parsed)) {
                        config.line_size = parsed;
                    }
                }
            }
            if (tools.has_key("arrow")) {
                easyjson::JSON const &arrow = tools["arrow"];
                if (arrow.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(arrow["size"], parsed)) {
                        config.arrow_size = parsed;
                    }
                }
            }
            if (tools.has_key("rect")) {
                easyjson::JSON const &rect = tools["rect"];
                if (rect.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(rect["size"], parsed)) {
                        config.rect_size = parsed;
                    }
                }
            }
            if (tools.has_key("ellipse")) {
                easyjson::JSON const &ellipse = tools["ellipse"];
                if (ellipse.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(ellipse["size"], parsed)) {
                        config.ellipse_size = parsed;
                    }
                }
            }
            if (tools.has_key("current_color")) {
                int32_t parsed = 0;
                if (Read_int_from_json(tools["current_color"], parsed)) {
                    config.current_annotation_color_index = parsed;
                }
            }
            if (tools.has_key("colors")) {
                Load_colors_from_json(tools["colors"], config.annotation_colors,
                                      core::kAnnotationColorSlotCount);
            }
            if (tools.has_key("font")) {
                easyjson::JSON const &font = tools["font"];
                if (font.has_key("sans")) {
                    config.text_font_sans = To_wide(font["sans"].to_string());
                }
                if (font.has_key("serif")) {
                    config.text_font_serif = To_wide(font["serif"].to_string());
                }
                if (font.has_key("mono")) {
                    config.text_font_mono = To_wide(font["mono"].to_string());
                }
                if (font.has_key("art")) {
                    config.text_font_art = To_wide(font["art"].to_string());
                }
            }
            if (tools.has_key("highlighter")) {
                easyjson::JSON const &hl = tools["highlighter"];
                if (hl.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(hl["size"], parsed)) {
                        config.highlighter_size = parsed;
                    }
                }
                if (hl.has_key("current_color")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(hl["current_color"], parsed)) {
                        config.current_highlighter_color_index = parsed;
                    }
                }
                if (hl.has_key("opacity_percent")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(hl["opacity_percent"], parsed)) {
                        config.highlighter_opacity_percent = parsed;
                    }
                }
                if (hl.has_key("pause_straighten_ms")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(hl["pause_straighten_ms"], parsed)) {
                        config.highlighter_pause_straighten_ms = parsed;
                    }
                }
                if (hl.has_key("pause_straighten_deadzone_px")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(hl["pause_straighten_deadzone_px"],
                                           parsed)) {
                        config.highlighter_pause_straighten_deadzone_px = parsed;
                    }
                }
                if (hl.has_key("colors")) {
                    Load_colors_from_json(hl["colors"], config.highlighter_colors,
                                          core::kHighlighterColorSlotCount);
                }
            }
            if (tools.has_key("text")) {
                easyjson::JSON const &text = tools["text"];
                if (text.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(text["size"], parsed)) {
                        config.text_size = parsed;
                    }
                }
                if (text.has_key("current_font")) {
                    config.text_current_font =
                        Parse_text_font_choice(text["current_font"].to_string());
                }
            }
            if (tools.has_key("bubble")) {
                easyjson::JSON const &bubble = tools["bubble"];
                if (bubble.has_key("size")) {
                    int32_t parsed = 0;
                    if (Read_int_from_json(bubble["size"], parsed)) {
                        config.bubble_size = parsed;
                    }
                }
                if (bubble.has_key("current_font")) {
                    config.bubble_current_font =
                        Parse_text_font_choice(bubble["current_font"].to_string());
                }
            }
        }

        if (root.has_key("save")) {
            easyjson::JSON const &save = root["save"];
            auto read_string = [&](char const *key, std::wstring &target) {
                if (save.has_key(key) &&
                    save[key].JSON_type() == easyjson::JSON::Class::String) {
                    std::string const s = save[key].to_string();
                    if (!s.empty()) {
                        target = To_wide(s);
                    }
                }
            };
            read_string("default_save_dir", config.default_save_dir);
            read_string("last_save_as_dir", config.last_save_as_dir);
            read_string("filename_pattern_region", config.filename_pattern_region);
            read_string("filename_pattern_desktop", config.filename_pattern_desktop);
            read_string("filename_pattern_monitor", config.filename_pattern_monitor);
            read_string("filename_pattern_window", config.filename_pattern_window);
            read_string("default_save_format", config.default_save_format);
            if (save.has_key("padding_color") &&
                save["padding_color"].JSON_type() == easyjson::JSON::Class::String) {
                COLORREF parsed = 0;
                if (Try_parse_color(save["padding_color"].to_string(), parsed)) {
                    config.padding_color = parsed;
                }
            }
        }
    } catch (...) {
        // Parse error or file read error: return default config.
    }
    config.Normalize();
    return config;
}

bool Save_app_config(core::AppConfig const &config) {
    std::filesystem::path const path = Get_config_path();
    if (path.empty()) {
        return false;
    }
    core::AppConfig const defaults{};
    try {
        std::filesystem::create_directories(path.parent_path());
        easyjson::JSON root = easyjson::object();

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

        bool wrote_tools = config.brush_size != defaults.brush_size ||
                           config.line_size != defaults.line_size ||
                           config.arrow_size != defaults.arrow_size ||
                           config.rect_size != defaults.rect_size ||
                           config.ellipse_size != defaults.ellipse_size ||
                           config.highlighter_size != defaults.highlighter_size ||
                           config.bubble_size != defaults.bubble_size ||
                           config.text_size != defaults.text_size ||
                           config.current_annotation_color_index !=
                               defaults.current_annotation_color_index ||
                           config.annotation_colors != defaults.annotation_colors ||
                           config.current_highlighter_color_index !=
                               defaults.current_highlighter_color_index ||
                           config.highlighter_opacity_percent !=
                               defaults.highlighter_opacity_percent ||
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
                for (size_t i = 0; i < core::kAnnotationColorSlotCount; ++i) {
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
                    easyjson::JSON hl_colors = easyjson::object();
                    for (size_t i = 0; i < core::kHighlighterColorSlotCount; ++i) {
                        if (config.highlighter_colors[i] !=
                            defaults.highlighter_colors[i]) {
                            hl_colors[std::to_string(i)] =
                                To_hex_color(config.highlighter_colors[i]);
                        }
                    }
                    root["tools"]["highlighter"]["colors"] = hl_colors;
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
        }

        bool wrote_save = !config.default_save_dir.empty() ||
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
                root["save"]["default_save_format"] =
                    To_utf8(config.default_save_format);
            }
            if (config.padding_color != defaults.padding_color) {
                root["save"]["padding_color"] = To_hex_color(config.padding_color);
            }
        }

        std::ofstream file(path);
        if (!file) {
            return false;
        }
        std::string const json_str = root.dump();
        file << json_str;
        return file.good();
    } catch (...) {
        return false;
    }
}

} // namespace greenflame
