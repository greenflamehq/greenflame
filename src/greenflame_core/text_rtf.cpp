#include "greenflame_core/text_rtf.h"

namespace greenflame::core {

namespace {

// ============================================================
// Encoder helpers
// ============================================================

constexpr std::string_view kRtfHeader =
    "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil Segoe UI;}}{\\colortbl ;}\\pard\\plain ";

void Emit_flag_changes(std::string &out, TextStyleFlags const &prev,
                       TextStyleFlags const &curr) {
    if (curr.bold != prev.bold) {
        out += curr.bold ? "\\b " : "\\b0 ";
    }
    if (curr.italic != prev.italic) {
        out += curr.italic ? "\\i " : "\\i0 ";
    }
    if (curr.underline != prev.underline) {
        out += curr.underline ? "\\ul " : "\\ulnone ";
    }
    if (curr.strikethrough != prev.strikethrough) {
        out += curr.strikethrough ? "\\strike " : "\\strike0 ";
    }
}

// ============================================================
// Decoder helpers
// ============================================================

struct GroupFrame final {
    TextStyleFlags saved_flags = {};
    int saved_uc_count = 1;
    bool is_ignore_frame = false;
};

[[nodiscard]] bool Is_rtf_letter(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

[[nodiscard]] bool Is_rtf_digit(char ch) noexcept { return ch >= '0' && ch <= '9'; }

[[nodiscard]] uint8_t Parse_hex_digit(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<uint8_t>(10 + (ch - 'a'));
    }
    if (ch >= 'A' && ch <= 'F') {
        return static_cast<uint8_t>(10 + (ch - 'A'));
    }
    return 0;
}

// Groups whose content is header metadata, not paragraph text.
[[nodiscard]] bool Is_header_destination(std::string_view word) noexcept {
    return word == "fonttbl" || word == "colortbl" || word == "stylesheet" ||
           word == "info" || word == "pict" || word == "object" || word == "fldinst" ||
           word == "themedata" || word == "colorschememapping";
}

} // namespace

// ============================================================
// Encode_rtf
// ============================================================

std::string Encode_rtf(std::span<const TextRun> runs) {
    // Upper bound of printable ASCII characters (U+007E = '~').
    constexpr wchar_t last_printable_ascii = 0x007E;

    std::string out;
    out.reserve(256);
    out += kRtfHeader;

    TextStyleFlags prev_flags;
    for (TextRun const &run : runs) {
        Emit_flag_changes(out, prev_flags, run.flags);
        prev_flags = run.flags;

        for (wchar_t const ch : run.text) {
            if (ch == L'\\') {
                out += "\\\\";
            } else if (ch == L'{') {
                out += "\\{";
            } else if (ch == L'}') {
                out += "\\}";
            } else if (ch == L'\n') {
                out += "\\par\n";
            } else if (ch == L'\t') {
                out += "\\tab ";
            } else if (ch >= 0x0020 && ch <= last_printable_ascii) {
                out += static_cast<char>(ch);
            } else if (ch >= 0x0080) {
                // Non-ASCII BMP (includes UTF-16 surrogate halves 0xD800-0xDFFF).
                // The RTF spec uses a signed 16-bit integer for the Unicode escape
                // control word parameter; values 0x8000-0xFFFF become negative.
                out += "\\u";
                out += std::to_string(static_cast<int>(static_cast<int16_t>(ch)));
                out += '?';
            }
            // 0x00-0x1F control chars (except LF and TAB): silently skipped.
        }
    }

    out += '}';
    return out;
}

// ============================================================
// Decode_rtf
// ============================================================

std::vector<TextRun> Decode_rtf(std::string_view rtf) {
    constexpr size_t max_input_size = 4 * 1024 * 1024; // 4 MB safety guard
    constexpr int max_group_depth = 200;

    if (rtf.size() > max_input_size) {
        return {};
    }
    if (!rtf.starts_with("{\\rtf")) {
        return {};
    }

    std::vector<TextRun> runs;
    TextStyleFlags current_flags;
    std::vector<GroupFrame> group_stack;
    int ignore_count = 0; // >0 while inside a header/destination group
    int skip_pending = 0; // replacement chars to skip after a Unicode escape
    int uc_count = 1;     // replacement chars per Unicode escape (default 1)

    size_t pos = 0;

    // Emit a single wide character into the run list (respecting ignore/skip state).
    auto emit_char = [&](wchar_t ch) {
        if (ignore_count > 0) {
            return;
        }
        if (skip_pending > 0) {
            --skip_pending;
            return;
        }
        if (runs.empty() || runs.back().flags != current_flags) {
            runs.push_back(TextRun{std::wstring(1, ch), current_flags});
        } else {
            runs.back().text.push_back(ch);
        }
    };

    while (pos < rtf.size()) {
        char const ch = rtf[pos];

        // ---- group open ----
        if (ch == '{') {
            ++pos;
            if (static_cast<int>(group_stack.size()) >= max_group_depth) {
                return {};
            }
            GroupFrame frame;
            frame.saved_flags = current_flags;
            frame.saved_uc_count = uc_count;
            frame.is_ignore_frame = false;

            // Check for extended destination marker (backslash-asterisk):
            // content inside such groups must be skipped.
            if (pos + 1 < rtf.size() && rtf[pos] == '\\' && rtf[pos + 1] == '*') {
                pos += 2; // consume the two-character marker
                frame.is_ignore_frame = true;
                ++ignore_count;
            } else {
                // Peek ahead (without consuming) to detect known header destinations
                // such as fonttbl and colortbl that lack the extended destination
                // marker.
                size_t peek = pos;
                while (peek < rtf.size() && rtf[peek] == ' ') {
                    ++peek;
                }
                if (peek < rtf.size() && rtf[peek] == '\\') {
                    ++peek; // skip backslash
                    std::string word;
                    while (peek < rtf.size() && Is_rtf_letter(rtf[peek])) {
                        word += rtf[peek++];
                    }
                    if (Is_header_destination(word)) {
                        frame.is_ignore_frame = true;
                        ++ignore_count;
                    }
                }
                // pos is not advanced; the control word will be re-parsed in the
                // normal flow below (and ignored if ignore_count > 0).
            }

            group_stack.push_back(frame);
            continue;
        }

        // ---- group close ----
        if (ch == '}') {
            ++pos;
            if (!group_stack.empty()) {
                GroupFrame const &frame = group_stack.back();
                current_flags = frame.saved_flags;
                uc_count = frame.saved_uc_count;
                if (frame.is_ignore_frame) {
                    --ignore_count;
                }
                group_stack.pop_back();
            }
            continue;
        }

        // ---- control sequence ----
        if (ch == '\\') {
            ++pos;
            if (pos >= rtf.size()) {
                break;
            }
            char const next = rtf[pos];

            // Single-character escapes
            if (next == '\\') {
                ++pos;
                emit_char(L'\\');
                continue;
            }
            if (next == '{') {
                ++pos;
                emit_char(L'{');
                continue;
            }
            if (next == '}') {
                ++pos;
                emit_char(L'}');
                continue;
            }
            if (next == '\n' || next == '\r') {
                ++pos;
                continue;
            } // ignored newline
            if (next == '-') {
                ++pos;
                continue;
            } // optional hyphen
            if (next == '~') {
                ++pos;
                emit_char(L'\u00A0');
                continue;
            } // non-breaking space
            if (next == '_') {
                ++pos;
                emit_char(L'\u2011');
                continue;
            } // non-breaking hyphen

            // Hex-encoded byte: backslash-apostrophe followed by two hex digits
            if (next == '\'') {
                ++pos; // consume apostrophe
                uint8_t byte_val = 0;
                if (pos + 1 < rtf.size()) {
                    byte_val = static_cast<uint8_t>((Parse_hex_digit(rtf[pos]) << 4) |
                                                    Parse_hex_digit(rtf[pos + 1]));
                    pos += 2;
                }
                emit_char(static_cast<wchar_t>(byte_val));
                continue;
            }

            // Must be a control word (starts with a letter)
            if (!Is_rtf_letter(next)) {
                ++pos; // skip unrecognised single-char escape
                continue;
            }

            // Read the control word (letters only)
            std::string word;
            while (pos < rtf.size() && Is_rtf_letter(rtf[pos])) {
                word += rtf[pos++];
            }

            // Read optional signed decimal parameter (capped at 6 digits to prevent
            // overflow).
            bool has_param = false;
            int param = 0;
            bool param_neg = false;
            if (pos < rtf.size() && (rtf[pos] == '-' || Is_rtf_digit(rtf[pos]))) {
                has_param = true;
                if (rtf[pos] == '-') {
                    param_neg = true;
                    ++pos;
                }
                constexpr int max_param_digits = 6;
                int digit_count = 0;
                while (pos < rtf.size() && Is_rtf_digit(rtf[pos])) {
                    if (digit_count < max_param_digits) {
                        param = param * 10 + (rtf[pos] - '0');
                        ++digit_count;
                    }
                    ++pos;
                }
                if (param_neg) {
                    param = -param;
                }
            }

            // Consume the optional trailing space delimiter
            if (pos < rtf.size() && rtf[pos] == ' ') {
                ++pos;
            }

            // Dispatch: only handle the four style flags, line breaks, and
            // Unicode escapes. All other control words are silently ignored.
            // Formatting updates are gated on ignore_count == 0.

            if (word == "b") {
                if (ignore_count == 0) {
                    current_flags.bold = !has_param || param != 0;
                }
            } else if (word == "i") {
                if (ignore_count == 0) {
                    current_flags.italic = !has_param || param != 0;
                }
            } else if (word == "ul") {
                if (ignore_count == 0) {
                    current_flags.underline = !has_param || param != 0;
                }
            } else if (word == "ulnone") {
                if (ignore_count == 0) {
                    current_flags.underline = false;
                }
            } else if (word == "strike") {
                if (ignore_count == 0) {
                    current_flags.strikethrough = !has_param || param != 0;
                }
            } else if (word == "striked") {
                // Some RTF writers use striked1 / striked0 for strikethrough.
                if (ignore_count == 0 && has_param) {
                    current_flags.strikethrough = (param != 0);
                }
            } else if (word == "plain") {
                if (ignore_count == 0) {
                    current_flags = TextStyleFlags{};
                }
            } else if (word == "par" || word == "line") {
                emit_char(L'\n');
            } else if (word == "tab") {
                emit_char(L'\t');
            } else if (word == "u") {
                // RTF Unicode escape: signed 16-bit code point, followed by
                // uc_count replacement characters that the decoder must skip.
                if (has_param && ignore_count == 0) {
                    wchar_t const wch =
                        static_cast<wchar_t>(static_cast<int16_t>(param));
                    if (runs.empty() || runs.back().flags != current_flags) {
                        runs.push_back(TextRun{std::wstring(1, wch), current_flags});
                    } else {
                        runs.back().text.push_back(wch);
                    }
                    skip_pending += uc_count;
                }
            } else if (word == "uc") {
                // Sets the number of replacement bytes that follow each Unicode escape.
                if (has_param && ignore_count == 0) {
                    uc_count = param;
                }
            }
            continue;
        }

        // ---- plain character ----
        ++pos;
        if (ch == '\r' || ch == '\n') {
            // Bare CR/LF in the RTF body is whitespace, not a paragraph break.
            continue;
        }
        emit_char(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    }

    return runs;
}

} // namespace greenflame::core
