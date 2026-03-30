#include "greenflame_core/text_html.h"

namespace greenflame::core {

namespace {

// ============================================================
// Constants
// ============================================================

constexpr size_t kMaxHtmlSize = 4 * 1024 * 1024;
constexpr size_t kMaxEntityLength = 16;
constexpr size_t kMaxStyleDepth = 200;

// UTF-8 byte-range boundaries and masks.
constexpr unsigned char kUtf8TwoByteMin = 0xC2u;
constexpr unsigned char kUtf8TwoByteMax = 0xDFu;
constexpr unsigned char kUtf8ThreeByteMin = 0xE0u;
constexpr unsigned char kUtf8ThreeByteMax = 0xEFu;
constexpr unsigned char kUtf8FourByteMin = 0xF0u;
constexpr unsigned char kUtf8FourByteMax = 0xF4u;
constexpr unsigned char kUtf8ContMask = 0xC0u;
constexpr unsigned char kUtf8ContTag = 0x80u;
constexpr unsigned char kUtf8TwoPayload = 0x1Fu;
constexpr unsigned char kUtf8ThreePayload = 0x0Fu;
constexpr unsigned char kUtf8FourPayload = 0x07u;
constexpr unsigned char kUtf8ContPayload = 0x3Fu;
constexpr char32_t kUtf8Replacement = 0xFFFDu;

// Surrogate constants for encoding supplementary code points in UTF-16.
constexpr char32_t kSupplementaryBase = 0x10000u;
constexpr char32_t kHighSurrogateBase = 0xD800u;
constexpr char32_t kLowSurrogateBase = 0xDC00u;
constexpr uint32_t kSurrogatePayloadMask = 0x3FFu;
constexpr char32_t kUnicodeMax = 0x10FFFFu;

// Number of payload bits in each UTF-8 continuation byte.
constexpr uint32_t kUtf8ContBits = 6u;

// CSS font-weight threshold above which text is considered bold.
// CSS weights run 100-900; 700 is the canonical "bold" weight.
constexpr int kBoldWeightThreshold = 600;

// ============================================================
// Named HTML entity table
// ============================================================

struct HtmlNamedEntity {
    std::string_view name;
    char32_t codepoint;
};

// Covers the basic XML entities, Latin-1 supplement (U+00A0..U+00FF),
// common punctuation/typography, and basic arrows/operators.
// Searched linearly. Unknown entities fall through to kUtf8Replacement.
constexpr HtmlNamedEntity kHtmlNamedEntities[] = {
    // Basic XML / HTML entities.
    {"amp", 0x0026u},
    {"lt", 0x003Cu},
    {"gt", 0x003Eu},
    {"quot", 0x0022u},
    {"apos", 0x0027u},
    // Latin-1 supplement.
    {"nbsp", 0x00A0u},
    {"iexcl", 0x00A1u},
    {"cent", 0x00A2u},
    {"pound", 0x00A3u},
    {"curren", 0x00A4u},
    {"yen", 0x00A5u},
    {"brvbar", 0x00A6u},
    {"sect", 0x00A7u},
    {"uml", 0x00A8u},
    {"copy", 0x00A9u},
    {"ordf", 0x00AAu},
    {"laquo", 0x00ABu},
    {"not", 0x00ACu},
    {"shy", 0x00ADu},
    {"reg", 0x00AEu},
    {"macr", 0x00AFu},
    {"deg", 0x00B0u},
    {"plusmn", 0x00B1u},
    {"sup2", 0x00B2u},
    {"sup3", 0x00B3u},
    {"acute", 0x00B4u},
    {"micro", 0x00B5u},
    {"para", 0x00B6u},
    {"middot", 0x00B7u},
    {"cedil", 0x00B8u},
    {"sup1", 0x00B9u},
    {"ordm", 0x00BAu},
    {"raquo", 0x00BBu},
    {"frac14", 0x00BCu},
    {"frac12", 0x00BDu},
    {"frac34", 0x00BEu},
    {"iquest", 0x00BFu},
    {"Agrave", 0x00C0u},
    {"Aacute", 0x00C1u},
    {"Acirc", 0x00C2u},
    {"Atilde", 0x00C3u},
    {"Auml", 0x00C4u},
    {"Aring", 0x00C5u},
    {"AElig", 0x00C6u},
    {"Ccedil", 0x00C7u},
    {"Egrave", 0x00C8u},
    {"Eacute", 0x00C9u},
    {"Ecirc", 0x00CAu},
    {"Euml", 0x00CBu},
    {"Igrave", 0x00CCu},
    {"Iacute", 0x00CDu},
    {"Icirc", 0x00CEu},
    {"Iuml", 0x00CFu},
    {"ETH", 0x00D0u},
    {"Ntilde", 0x00D1u},
    {"Ograve", 0x00D2u},
    {"Oacute", 0x00D3u},
    {"Ocirc", 0x00D4u},
    {"Otilde", 0x00D5u},
    {"Ouml", 0x00D6u},
    {"times", 0x00D7u},
    {"Oslash", 0x00D8u},
    {"Ugrave", 0x00D9u},
    {"Uacute", 0x00DAu},
    {"Ucirc", 0x00DBu},
    {"Uuml", 0x00DCu},
    {"Yacute", 0x00DDu},
    {"THORN", 0x00DEu},
    {"szlig", 0x00DFu},
    {"agrave", 0x00E0u},
    {"aacute", 0x00E1u},
    {"acirc", 0x00E2u},
    {"atilde", 0x00E3u},
    {"auml", 0x00E4u},
    {"aring", 0x00E5u},
    {"aelig", 0x00E6u},
    {"ccedil", 0x00E7u},
    {"egrave", 0x00E8u},
    {"eacute", 0x00E9u},
    {"ecirc", 0x00EAu},
    {"euml", 0x00EBu},
    {"igrave", 0x00ECu},
    {"iacute", 0x00EDu},
    {"icirc", 0x00EEu},
    {"iuml", 0x00EFu},
    {"eth", 0x00F0u},
    {"ntilde", 0x00F1u},
    {"ograve", 0x00F2u},
    {"oacute", 0x00F3u},
    {"ocirc", 0x00F4u},
    {"otilde", 0x00F5u},
    {"ouml", 0x00F6u},
    {"divide", 0x00F7u},
    {"oslash", 0x00F8u},
    {"ugrave", 0x00F9u},
    {"uacute", 0x00FAu},
    {"ucirc", 0x00FBu},
    {"uuml", 0x00FCu},
    {"yacute", 0x00FDu},
    {"thorn", 0x00FEu},
    {"yuml", 0x00FFu},
    // General Punctuation — typography and word-processor output.
    {"ensp", 0x2002u},
    {"emsp", 0x2003u},
    {"thinsp", 0x2009u},
    {"zwnj", 0x200Cu},
    {"zwj", 0x200Du},
    {"lrm", 0x200Eu},
    {"rlm", 0x200Fu},
    {"ndash", 0x2013u},
    {"mdash", 0x2014u},
    {"lsquo", 0x2018u},
    {"rsquo", 0x2019u},
    {"sbquo", 0x201Au},
    {"ldquo", 0x201Cu},
    {"rdquo", 0x201Du},
    {"bdquo", 0x201Eu},
    {"dagger", 0x2020u},
    {"Dagger", 0x2021u},
    {"bull", 0x2022u},
    {"hellip", 0x2026u},
    {"permil", 0x2030u},
    {"prime", 0x2032u},
    {"Prime", 0x2033u},
    {"lsaquo", 0x2039u},
    {"rsaquo", 0x203Au},
    {"frasl", 0x2044u},
    {"euro", 0x20ACu},
    {"trade", 0x2122u},
    // Arrows and basic operators.
    {"larr", 0x2190u},
    {"uarr", 0x2191u},
    {"rarr", 0x2192u},
    {"darr", 0x2193u},
    {"harr", 0x2194u},
    {"ne", 0x2260u},
    {"le", 0x2264u},
    {"ge", 0x2265u},
    {"minus", 0x2212u},
    {"infin", 0x221Eu},
};

// ============================================================
// UTF-8 decoder
// ============================================================

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

// Decode one UTF-8 code unit starting at html[pos], advancing pos.
// Returns kUtf8Replacement on any invalid sequence.
[[nodiscard]] char32_t Decode_utf8(std::string_view html, size_t &pos) noexcept {
    auto const b0 = static_cast<unsigned char>(html[pos++]);
    if (b0 < kUtf8ContTag) {
        return static_cast<char32_t>(b0);
    }

    auto read_cont = [&](unsigned char &out) noexcept -> bool {
        if (pos >= html.size()) {
            return false;
        }
        auto const b = static_cast<unsigned char>(html[pos]);
        if ((b & kUtf8ContMask) != kUtf8ContTag) {
            return false;
        }
        out = static_cast<unsigned char>(b & kUtf8ContPayload);
        ++pos;
        return true;
    };

    unsigned char c1 = 0;
    unsigned char c2 = 0;
    unsigned char c3 = 0;

    if (b0 >= kUtf8TwoByteMin && b0 <= kUtf8TwoByteMax) {
        if (!read_cont(c1)) {
            return kUtf8Replacement;
        }
        return static_cast<char32_t>(
            (static_cast<uint32_t>(b0 & kUtf8TwoPayload) << kUtf8ContBits) | c1);
    }
    if (b0 >= kUtf8ThreeByteMin && b0 <= kUtf8ThreeByteMax) {
        if (!read_cont(c1) || !read_cont(c2)) {
            return kUtf8Replacement;
        }
        return static_cast<char32_t>(
            (static_cast<uint32_t>(b0 & kUtf8ThreePayload) << (2 * kUtf8ContBits)) |
            (static_cast<uint32_t>(c1) << kUtf8ContBits) | c2);
    }
    if (b0 >= kUtf8FourByteMin && b0 <= kUtf8FourByteMax) {
        if (!read_cont(c1) || !read_cont(c2) || !read_cont(c3)) {
            return kUtf8Replacement;
        }
        return static_cast<char32_t>(
            (static_cast<uint32_t>(b0 & kUtf8FourPayload) << (3 * kUtf8ContBits)) |
            (static_cast<uint32_t>(c1) << (2 * kUtf8ContBits)) |
            (static_cast<uint32_t>(c2) << kUtf8ContBits) | c3);
    }
    return kUtf8Replacement;
}

// ============================================================
// Run builder
// ============================================================

void Emit_codepoint(std::vector<TextRun> &runs, TextStyleFlags flags,
                    char32_t cp) noexcept {
    if (cp == 0 || cp > kUnicodeMax) {
        return;
    }

    auto push_wchar = [&](wchar_t wch) noexcept {
        if (runs.empty() || runs.back().flags != flags) {
            runs.push_back(TextRun{std::wstring(1, wch), flags});
        } else {
            runs.back().text.push_back(wch);
        }
    };

    if (cp < kSupplementaryBase) {
        push_wchar(static_cast<wchar_t>(cp));
    } else {
        char32_t const adjusted = cp - kSupplementaryBase;
        push_wchar(static_cast<wchar_t>(kHighSurrogateBase | (adjusted >> 10u)));
        push_wchar(static_cast<wchar_t>(kLowSurrogateBase |
                                        (adjusted & kSurrogatePayloadMask)));
    }
}

// ============================================================
// CSS inline-style parser
// ============================================================

// Strips leading whitespace from a string_view in place.
void Trim_leading(std::string_view &sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
}

// Strips trailing whitespace from a string_view in place.
void Trim_trailing(std::string_view &sv) noexcept {
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) {
        sv.remove_suffix(1);
    }
}

// Applies CSS property-value pairs from a style attribute to base flags.
// Only font-weight, font-style, and text-decoration are processed.
[[nodiscard]] TextStyleFlags Apply_css_style(TextStyleFlags base,
                                             std::string_view style) noexcept {
    TextStyleFlags flags = base;
    while (!style.empty()) {
        size_t const colon = style.find(':');
        if (colon == std::string_view::npos) {
            break;
        }
        std::string_view prop = style.substr(0, colon);
        Trim_leading(prop);
        Trim_trailing(prop);
        style.remove_prefix(colon + 1);

        size_t const semi = style.find(';');
        std::string_view val =
            (semi != std::string_view::npos) ? style.substr(0, semi) : style;
        Trim_leading(val);
        Trim_trailing(val);
        if (semi != std::string_view::npos) {
            style.remove_prefix(semi + 1);
        } else {
            style = {};
        }

        if (prop == "font-weight") {
            if (val == "bold" || val == "bolder") {
                flags.bold = true;
            } else if (val == "normal" || val == "lighter") {
                flags.bold = false;
            } else {
                // Numeric weight: values >= kBoldWeightThreshold map to bold.
                int weight = 0;
                for (char const c : val) {
                    if (c >= '0' && c <= '9') {
                        weight = weight * 10 + (c - '0');
                    }
                }
                if (weight > 0) {
                    flags.bold = (weight >= kBoldWeightThreshold);
                }
            }
        } else if (prop == "font-style") {
            if (val == "italic" || val == "oblique") {
                flags.italic = true;
            } else if (val == "normal") {
                flags.italic = false;
            }
        } else if (prop == "text-decoration") {
            // CSS text-decoration is a shorthand that fully replaces the value;
            // reset both flags then set from the (possibly space-separated) value.
            flags.underline = (val.find("underline") != std::string_view::npos);
            flags.strikethrough = (val.find("line-through") != std::string_view::npos);
        }
    }
    return flags;
}

// ============================================================
// Tag name helper
// ============================================================

// Returns true if 'name' (already lowercase) is a block-level element that
// should cause a paragraph break before its opening tag.
[[nodiscard]] bool Is_block_tag(std::string_view name) noexcept {
    return name == "p" || name == "div" || name == "h1" || name == "h2" ||
           name == "h3" || name == "h4" || name == "h5" || name == "h6" ||
           name == "li" || name == "tr" || name == "blockquote" || name == "pre" ||
           name == "article" || name == "section" || name == "header" ||
           name == "footer" || name == "figure" || name == "figcaption" ||
           name == "aside" || name == "main" || name == "nav";
}

// Returns true if 'name' is an inline semantic style tag we push/pop.
[[nodiscard]] bool Is_inline_style_tag(std::string_view name) noexcept {
    return name == "b" || name == "strong" || name == "i" || name == "em" ||
           name == "u" || name == "s" || name == "del" || name == "strike" ||
           name == "span" || name == "a" || name == "mark" || name == "small" ||
           name == "font";
}

// Returns true if 'name' is a tag whose text content should be suppressed.
[[nodiscard]] bool Is_suppress_tag(std::string_view name) noexcept {
    return name == "script" || name == "style" || name == "head";
}

// ============================================================
// Encoder constants and helpers
// ============================================================

// Field width for byte offsets in the Windows "HTML Format" header.
// Each of the four offset fields is exactly kHtmlOffsetWidth decimal digits.
// 8 = 2^3 (power of two).
constexpr size_t kHtmlOffsetWidth = 8;

// Each header line: tag + kHtmlOffsetWidth digits + "\r\n".
// "Version:0.9\r\n"            = 13 bytes
// "StartHTML:00000000\r\n"     = 20 bytes
// "EndHTML:00000000\r\n"       = 18 bytes
// "StartFragment:00000000\r\n" = 24 bytes
// "EndFragment:00000000\r\n"   = 22 bytes
// Total header                 = 97 bytes
constexpr std::string_view kHtmlTagVersion = "Version:0.9\r\n";
constexpr std::string_view kHtmlTagStartHtml = "StartHTML:";
constexpr std::string_view kHtmlTagEndHtml = "EndHTML:";
constexpr std::string_view kHtmlTagStartFrag = "StartFragment:";
constexpr std::string_view kHtmlTagEndFrag = "EndFragment:";
constexpr std::string_view kHtmlLineEnd = "\r\n";

constexpr size_t kHtmlHeaderSize =
    kHtmlTagVersion.size() +
    (kHtmlTagStartHtml.size() + kHtmlOffsetWidth + kHtmlLineEnd.size()) +
    (kHtmlTagEndHtml.size() + kHtmlOffsetWidth + kHtmlLineEnd.size()) +
    (kHtmlTagStartFrag.size() + kHtmlOffsetWidth + kHtmlLineEnd.size()) +
    (kHtmlTagEndFrag.size() + kHtmlOffsetWidth + kHtmlLineEnd.size());

constexpr std::string_view kHtmlDocPrefix = "<html><body>\r\n<!--StartFragment-->";
constexpr std::string_view kHtmlDocSuffix = "<!--EndFragment-->\r\n</body></html>";

// Absolute byte offset where the HTML document begins (= header size).
constexpr size_t kHtmlStartHtml = kHtmlHeaderSize;
// Absolute byte offset where the fragment content begins.
constexpr size_t kHtmlStartFrag = kHtmlStartHtml + kHtmlDocPrefix.size();

// Header bytes for multi-byte UTF-8 sequences (leading byte ORed with payload).
constexpr unsigned char kUtf8EncTwoHdr = 0xC0u;   // 110x_xxxx
constexpr unsigned char kUtf8EncThreeHdr = 0xE0u; // 1110_xxxx
constexpr unsigned char kUtf8EncFourHdr = 0xF0u;  // 1111_0xxx

// Surrogate pair range extremes (derived from named base and payload mask).
constexpr char32_t kHighSurrogateMax =
    kHighSurrogateBase + kSurrogatePayloadMask; // = 0xDBFF
constexpr char32_t kLowSurrogateMax =
    kLowSurrogateBase + kSurrogatePayloadMask; // = 0xDFFF

// Write n as a zero-padded kHtmlOffsetWidth-digit decimal into out.
void Append_html_offset(std::string &out, size_t n) {
    size_t const insert_pos = out.size();
    out.append(kHtmlOffsetWidth, '0');
    size_t remaining = n;
    for (size_t i = kHtmlOffsetWidth; i-- > 0;) {
        out[insert_pos + i] = static_cast<char>('0' + remaining % 10);
        remaining /= 10;
    }
}

// Encode a Unicode code point to UTF-8 and append to out.
void Append_utf8(std::string &out, char32_t cp) noexcept {
    if (cp < 0x80u) { // power of 2: safe
        out += static_cast<char>(cp);
    } else if (cp < 0x800u) { // power of 2: safe
        out += static_cast<char>(kUtf8EncTwoHdr | (cp >> kUtf8ContBits));
        out += static_cast<char>(kUtf8ContTag | (cp & kUtf8ContPayload));
    } else if (cp < kSupplementaryBase) {
        out += static_cast<char>(kUtf8EncThreeHdr | (cp >> (2 * kUtf8ContBits)));
        out += static_cast<char>(kUtf8ContTag |
                                 ((cp >> kUtf8ContBits) & kUtf8ContPayload));
        out += static_cast<char>(kUtf8ContTag | (cp & kUtf8ContPayload));
    } else if (cp <= kUnicodeMax) {
        out += static_cast<char>(kUtf8EncFourHdr | (cp >> (3 * kUtf8ContBits)));
        out += static_cast<char>(kUtf8ContTag |
                                 ((cp >> (2 * kUtf8ContBits)) & kUtf8ContPayload));
        out += static_cast<char>(kUtf8ContTag |
                                 ((cp >> kUtf8ContBits) & kUtf8ContPayload));
        out += static_cast<char>(kUtf8ContTag | (cp & kUtf8ContPayload));
    }
    // Code points > kUnicodeMax are silently dropped.
}

// Build the CSS style string for the given flags. Returns "" when all flags off.
[[nodiscard]] std::string Build_css_style(TextStyleFlags const &flags) {
    std::string style;
    if (flags.bold) {
        style += "font-weight:700;";
    }
    if (flags.italic) {
        style += "font-style:italic;";
    }
    if (flags.underline && flags.strikethrough) {
        style += "text-decoration:underline line-through;";
    } else if (flags.underline) {
        style += "text-decoration:underline;";
    } else if (flags.strikethrough) {
        style += "text-decoration:line-through;";
    }
    return style;
}

// Append the HTML-escaped, UTF-8-encoded text of a run to fragment.
// Handles surrogate pairs in the UTF-16 wstring source.
void Encode_run_text(std::string &fragment, std::wstring const &text) {
    for (size_t i = 0; i < text.size(); ++i) {
        auto const wch = static_cast<char32_t>(text[i]);
        char32_t cp;

        if (wch >= kHighSurrogateBase && wch <= kHighSurrogateMax &&
            i + 1 < text.size()) {
            auto const low = static_cast<char32_t>(text[i + 1]);
            if (low >= kLowSurrogateBase && low <= kLowSurrogateMax) {
                ++i;
                cp = kSupplementaryBase + ((wch - kHighSurrogateBase) << 10u) +
                     (low - kLowSurrogateBase);
            } else {
                cp = wch; // unpaired high surrogate — emit as-is
            }
        } else {
            cp = wch;
        }

        if (cp == U'&') {
            fragment += "&amp;";
        } else if (cp == U'<') {
            fragment += "&lt;";
        } else if (cp == U'>') {
            fragment += "&gt;";
        } else if (cp == U'\n') {
            fragment += "<br/>";
        } else if (cp == U'\t') {
            fragment += "&#9;";
        } else if (cp < 0x20u) { /* skip other control chars */
        } else {
            Append_utf8(fragment, cp);
        }
    }
}

} // anonymous namespace

// ============================================================
// Public API
// ============================================================

std::string Encode_html_clipboard(std::span<const TextRun> runs) {
    std::string fragment;
    for (TextRun const &run : runs) {
        std::string const style = Build_css_style(run.flags);
        bool const has_style = !style.empty();
        if (has_style) {
            fragment += "<span style=\"";
            fragment += style;
            fragment += "\">";
        }
        Encode_run_text(fragment, run.text);
        if (has_style) {
            fragment += "</span>";
        }
    }

    size_t const end_frag = kHtmlStartFrag + fragment.size();
    size_t const end_html = end_frag + kHtmlDocSuffix.size();

    std::string out;
    out.reserve(end_html);
    out += kHtmlTagVersion;
    out += kHtmlTagStartHtml;
    Append_html_offset(out, kHtmlStartHtml);
    out += kHtmlLineEnd;
    out += kHtmlTagEndHtml;
    Append_html_offset(out, end_html);
    out += kHtmlLineEnd;
    out += kHtmlTagStartFrag;
    Append_html_offset(out, kHtmlStartFrag);
    out += kHtmlLineEnd;
    out += kHtmlTagEndFrag;
    Append_html_offset(out, end_frag);
    out += kHtmlLineEnd;
    out += kHtmlDocPrefix;
    out += fragment;
    out += kHtmlDocSuffix;
    return out;
}

std::vector<TextRun> Decode_html_clipboard(std::string_view html) {
    if (html.size() > kMaxHtmlSize) {
        return {};
    }
    if (html.empty()) {
        return {};
    }

    // Extract the fragment between <!--StartFragment--> / <!--EndFragment-->
    // markers if present; otherwise decode the whole input.
    constexpr std::string_view start_marker = "<!--StartFragment-->";
    constexpr std::string_view end_marker = "<!--EndFragment-->";
    size_t const frag_start = html.find(start_marker);
    size_t const frag_end = html.find(end_marker);
    if (frag_start != std::string_view::npos && frag_end != std::string_view::npos &&
        frag_end > frag_start + start_marker.size()) {
        size_t const content_start = frag_start + start_marker.size();
        html = html.substr(content_start, frag_end - content_start);
    }

    std::vector<TextRun> runs;
    std::vector<TextStyleFlags> style_stack; // one entry per inline styled open tag
    TextStyleFlags current_flags{};
    int pending_newlines = 0;
    bool has_content = false;
    int suppress_depth = 0; // inside script/style/head

    size_t pos = 0;
    constexpr int max_pending_newlines = 2;

    auto emit_newline = [&]() noexcept {
        if (suppress_depth > 0) {
            return;
        }
        if (pending_newlines < max_pending_newlines) {
            ++pending_newlines;
        }
    };

    auto emit_char = [&](char32_t cp) {
        if (suppress_depth > 0) {
            return;
        }
        // Flush pending newlines with no styling — newlines are structural
        // separators and do not inherit the style of adjacent content.
        TextStyleFlags const plain_flags{};
        for (int i = 0; i < pending_newlines; ++i) {
            Emit_codepoint(runs, plain_flags, U'\n');
        }
        pending_newlines = 0;
        has_content = true;
        Emit_codepoint(runs, current_flags, cp);
    };

    while (pos < html.size()) {
        unsigned char const byte = static_cast<unsigned char>(html[pos]);

        // ---- Comment or DOCTYPE ----
        if (byte == '<') {
            std::string_view const rest = html.substr(pos);
            if (rest.starts_with("<!--")) {
                size_t const end = html.find("-->", pos + 4);
                pos = (end != std::string_view::npos) ? end + 3 : html.size();
                continue;
            }
            if (rest.starts_with("<!")) {
                size_t const end = html.find('>', pos);
                pos = (end != std::string_view::npos) ? end + 1 : html.size();
                continue;
            }
        }

        // ---- Tag ----
        if (byte == '<') {
            ++pos; // skip '<'

            bool is_closing = false;
            if (pos < html.size() && html[pos] == '/') {
                is_closing = true;
                ++pos;
            }

            // Read tag name (ASCII only; lowercase it in place).
            size_t const name_start = pos;
            while (pos < html.size()) {
                char const c = html[pos];
                if (c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' ||
                    c == '\n') {
                    break;
                }
                ++pos;
            }
            std::string tag_name(html.substr(name_start, pos - name_start));
            for (char &c : tag_name) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c + ('a' - 'A'));
                }
            }

            // Scan to end of tag, collecting style attribute value.
            size_t const attrs_start = pos;
            bool self_closing = false;
            while (pos < html.size() && html[pos] != '>') {
                ++pos;
            }
            // Check for trailing '/' before '>'.
            if (pos > 0 && html[pos - 1] == '/') {
                self_closing = true;
            }
            std::string_view const attrs = html.substr(attrs_start, pos - attrs_start);
            if (pos < html.size()) {
                ++pos;
            } // skip '>'

            // Extract style="..." from attrs.
            std::string_view style_val;
            constexpr std::string_view style_eq = "style=\"";
            size_t const style_pos = attrs.find(style_eq);
            if (style_pos != std::string_view::npos) {
                size_t const val_start = style_pos + style_eq.size();
                size_t const val_end = attrs.find('"', val_start);
                if (val_end != std::string_view::npos) {
                    style_val = attrs.substr(val_start, val_end - val_start);
                }
            }

            if (is_closing) {
                if (Is_suppress_tag(tag_name)) {
                    if (suppress_depth > 0) {
                        --suppress_depth;
                    }
                } else if (Is_inline_style_tag(tag_name)) {
                    if (!style_stack.empty()) {
                        current_flags = style_stack.back();
                        style_stack.pop_back();
                    }
                }
                // Block close: no action — next block-open emits the separator.
            } else {
                if (Is_suppress_tag(tag_name)) {
                    ++suppress_depth;
                } else if (tag_name == "br") {
                    emit_newline();
                } else if (Is_block_tag(tag_name)) {
                    if (has_content) {
                        emit_newline();
                    }
                } else if (Is_inline_style_tag(tag_name)) {
                    if (style_stack.size() < kMaxStyleDepth) {
                        style_stack.push_back(current_flags);
                    }
                    // Apply semantic tag effects, then inline CSS (CSS may augment or
                    // override).
                    if (tag_name == "b" || tag_name == "strong") {
                        current_flags.bold = true;
                    } else if (tag_name == "i" || tag_name == "em") {
                        current_flags.italic = true;
                    } else if (tag_name == "u") {
                        current_flags.underline = true;
                    } else if (tag_name == "s" || tag_name == "del" ||
                               tag_name == "strike") {
                        current_flags.strikethrough = true;
                    }
                    if (!style_val.empty()) {
                        current_flags = Apply_css_style(current_flags, style_val);
                    }
                    // Self-closing inline tag: immediately undo the push.
                    if (self_closing && !style_stack.empty()) {
                        current_flags = style_stack.back();
                        style_stack.pop_back();
                    }
                }
                // meta, img, link, etc.: ignored (no push, no content).
            }
            continue;
        }

        // ---- Entity ----
        if (byte == '&') {
            ++pos;
            size_t const entity_start = pos;
            while (pos < html.size() && html[pos] != ';' && html[pos] != '<' &&
                   (pos - entity_start) < kMaxEntityLength) {
                ++pos;
            }
            std::string_view const entity =
                html.substr(entity_start, pos - entity_start);
            if (pos < html.size() && html[pos] == ';') {
                ++pos;
            }

            char32_t cp = kUtf8Replacement;
            if (!entity.empty() && entity[0] == '#') {
                std::string_view const num = entity.substr(1);
                if (!num.empty() && (num[0] == 'x' || num[0] == 'X')) {
                    char32_t val = 0;
                    for (char const c : num.substr(1)) {
                        val = val * 16u + Parse_hex_digit(c);
                    }
                    cp = val;
                } else {
                    char32_t val = 0;
                    for (char const c : num) {
                        if (c >= '0' && c <= '9') {
                            val = val * 10u + static_cast<char32_t>(c - '0');
                        }
                    }
                    cp = val;
                }
            } else {
                for (auto const &e : kHtmlNamedEntities) {
                    if (e.name == entity) {
                        cp = e.codepoint;
                        break;
                    }
                }
            }
            emit_char(cp);
            continue;
        }

        // ---- Bare CR/LF in HTML source: whitespace, not content ----
        if (byte == '\r' || byte == '\n') {
            ++pos;
            continue;
        }

        // ---- Regular UTF-8 character ----
        char32_t const cp = Decode_utf8(html, pos);
        emit_char(cp);
    }

    return runs;
}

} // namespace greenflame::core
