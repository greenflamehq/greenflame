#include "greenflame_core/text_html.h"

using namespace greenflame::core;

namespace {

[[nodiscard]] TextRun Plain(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{}};
}
[[nodiscard]] TextRun Bold(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.bold = true}};
}
[[nodiscard]] TextRun Italic(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.italic = true}};
}
[[nodiscard]] TextRun Underline(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.underline = true}};
}
[[nodiscard]] TextRun Strike(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.strikethrough = true}};
}
[[nodiscard]] TextRun Bold_italic(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.bold = true, .italic = true}};
}
[[nodiscard]] TextRun Underline_strike(std::wstring text) {
    return TextRun{std::move(text),
                   TextStyleFlags{.underline = true, .strikethrough = true}};
}
bool Runs_equal(std::vector<TextRun> const &a, std::vector<TextRun> const &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].text != b[i].text) {
            return false;
        }
        if (a[i].flags.bold != b[i].flags.bold) {
            return false;
        }
        if (a[i].flags.italic != b[i].flags.italic) {
            return false;
        }
        if (a[i].flags.underline != b[i].flags.underline) {
            return false;
        }
        if (a[i].flags.strikethrough != b[i].flags.strikethrough) {
            return false;
        }
    }
    return true;
}

// Concatenate all run texts into a single wstring.
[[nodiscard]] std::wstring All_text(std::vector<TextRun> const &runs) {
    std::wstring result;
    for (auto const &r : runs) {
        result += r.text;
    }
    return result;
}

} // namespace

// ============================================================
// Basic decode
// ============================================================

TEST(text_html, Decode_EmptyInput_ReturnsEmpty) {
    EXPECT_TRUE(Decode_html_clipboard("").empty());
}

TEST(text_html, Decode_PlainText_NoTags) {
    auto const runs = Decode_html_clipboard("Hello");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, L"Hello");
    EXPECT_FALSE(runs[0].flags.bold);
}

TEST(text_html, Decode_PlainParagraph) {
    auto const runs = Decode_html_clipboard("<p>Hello</p>");
    ASSERT_FALSE(runs.empty());
    EXPECT_EQ(All_text(runs), L"Hello");
}

// ============================================================
// CSS style attributes
// ============================================================

TEST(text_html, Decode_BoldSpan_FontWeight700) {
    auto const runs =
        Decode_html_clipboard("<span style=\"font-weight:700\">Bold</span>");
    ASSERT_TRUE(Runs_equal(runs, {Bold(L"Bold")}));
}

TEST(text_html, Decode_BoldSpan_FontWeightBold) {
    auto const runs =
        Decode_html_clipboard("<span style=\"font-weight:bold\">Bold</span>");
    ASSERT_TRUE(Runs_equal(runs, {Bold(L"Bold")}));
}

TEST(text_html, Decode_NoBold_FontWeight400) {
    // font-weight:400 should NOT produce a bold run.
    auto const runs =
        Decode_html_clipboard("<span style=\"font-weight:400\">Normal</span>");
    ASSERT_TRUE(Runs_equal(runs, {Plain(L"Normal")}));
}

TEST(text_html, Decode_ItalicSpan) {
    auto const runs =
        Decode_html_clipboard("<span style=\"font-style:italic\">Ital</span>");
    ASSERT_TRUE(Runs_equal(runs, {Italic(L"Ital")}));
}

TEST(text_html, Decode_UnderlineSpan) {
    auto const runs =
        Decode_html_clipboard("<span style=\"text-decoration:underline\">U</span>");
    ASSERT_TRUE(Runs_equal(runs, {Underline(L"U")}));
}

TEST(text_html, Decode_StrikethroughSpan) {
    auto const runs =
        Decode_html_clipboard("<span style=\"text-decoration:line-through\">S</span>");
    ASSERT_TRUE(Runs_equal(runs, {Strike(L"S")}));
}

TEST(text_html, Decode_UnderlineAndStrikethrough_SpaceSeparated) {
    auto const runs = Decode_html_clipboard(
        "<span style=\"text-decoration:underline line-through\">US</span>");
    ASSERT_TRUE(Runs_equal(runs, {Underline_strike(L"US")}));
}

TEST(text_html, Decode_BoldItalicCombined) {
    auto const runs = Decode_html_clipboard(
        "<span style=\"font-weight:700;font-style:italic\">BI</span>");
    ASSERT_TRUE(Runs_equal(runs, {Bold_italic(L"BI")}));
}

// ============================================================
// Semantic tags
// ============================================================

TEST(text_html, Decode_SemanticB) {
    auto const runs = Decode_html_clipboard("<b>Bold</b>");
    ASSERT_TRUE(Runs_equal(runs, {Bold(L"Bold")}));
}

TEST(text_html, Decode_SemanticStrong) {
    auto const runs = Decode_html_clipboard("<strong>Strong</strong>");
    ASSERT_TRUE(Runs_equal(runs, {Bold(L"Strong")}));
}

TEST(text_html, Decode_SemanticI) {
    auto const runs = Decode_html_clipboard("<i>Italic</i>");
    ASSERT_TRUE(Runs_equal(runs, {Italic(L"Italic")}));
}

TEST(text_html, Decode_SemanticU) {
    auto const runs = Decode_html_clipboard("<u>Under</u>");
    ASSERT_TRUE(Runs_equal(runs, {Underline(L"Under")}));
}

TEST(text_html, Decode_SemanticS) {
    auto const runs = Decode_html_clipboard("<s>Strike</s>");
    ASSERT_TRUE(Runs_equal(runs, {Strike(L"Strike")}));
}

// ============================================================
// Style stack / nesting
// ============================================================

TEST(text_html, Decode_NestedSpans_InheritBold) {
    // Inner span adds italic; outer bold must survive.
    auto const runs =
        Decode_html_clipboard("<span style=\"font-weight:700\">"
                              "<span style=\"font-style:italic\">BI</span>B</span>");
    EXPECT_EQ(All_text(runs), L"BIB");
    ASSERT_GE(runs.size(), 2u);
    EXPECT_TRUE(runs[0].flags.bold && runs[0].flags.italic);  // "BI"
    EXPECT_TRUE(runs[1].flags.bold && !runs[1].flags.italic); // "B"
}

TEST(text_html, Decode_StyleRestoredAfterClose) {
    auto const runs = Decode_html_clipboard("<b>A</b>B");
    ASSERT_GE(runs.size(), 2u);
    EXPECT_TRUE(runs[0].flags.bold);
    EXPECT_FALSE(runs[1].flags.bold);
}

// ============================================================
// Block element paragraph breaks
// ============================================================

TEST(text_html, Decode_TwoParagraphs_OneNewlineBetween) {
    auto const runs = Decode_html_clipboard("<p>A</p><p>B</p>");
    EXPECT_EQ(All_text(runs), L"A\nB");
}

TEST(text_html, Decode_LineBreak) {
    auto const runs = Decode_html_clipboard("A<br/>B");
    EXPECT_EQ(All_text(runs), L"A\nB");
}

TEST(text_html, Decode_LineBreak_NoSlash) {
    auto const runs = Decode_html_clipboard("A<br>B");
    EXPECT_EQ(All_text(runs), L"A\nB");
}

TEST(text_html, Decode_ExtraBreaksProduceBlankLines) {
    // <p>A</p><br/><br/><p>B</p> → 2 newlines max (pending_newlines cap = 2).
    auto const runs = Decode_html_clipboard("<p>A</p><br/><br/><p>B</p>");
    EXPECT_EQ(All_text(runs), L"A\n\nB");
}

// ============================================================
// HTML entities
// ============================================================

TEST(text_html, Decode_Entities_AmpLtGt) {
    auto const runs = Decode_html_clipboard("&amp;&lt;&gt;");
    EXPECT_EQ(All_text(runs), L"&<>");
}

TEST(text_html, Decode_Entity_Nbsp_BecomesNbsp) {
    auto const runs = Decode_html_clipboard("A&nbsp;B");
    EXPECT_EQ(All_text(runs), L"A\u00A0B");
}

TEST(text_html, Decode_NumericEntity_Decimal) {
    // &#65; = 'A'
    auto const runs = Decode_html_clipboard("&#65;");
    EXPECT_EQ(All_text(runs), L"A");
}

TEST(text_html, Decode_NumericEntity_Hex) {
    // &#x41; = 'A'
    auto const runs = Decode_html_clipboard("&#x41;");
    EXPECT_EQ(All_text(runs), L"A");
}

// ============================================================
// StartFragment / EndFragment extraction
// ============================================================

TEST(text_html, Decode_ExtractsFragment) {
    // Content outside the markers must be ignored.
    std::string const html = "Version:0.9\r\nStartHTML:00000097\r\n"
                             "<html><body>\r\n"
                             "<!--StartFragment--><b>Bold</b><!--EndFragment-->\r\n"
                             "</body></html>";
    auto const runs = Decode_html_clipboard(html);
    ASSERT_TRUE(Runs_equal(runs, {Bold(L"Bold")}));
}

TEST(text_html, Decode_NoMarkers_UsesFullInput) {
    // Without markers the whole input is decoded.
    auto const runs = Decode_html_clipboard("<b>X</b>");
    ASSERT_TRUE(Runs_equal(runs, {Bold(L"X")}));
}

// ============================================================
// Content suppression
// ============================================================

TEST(text_html, Decode_ScriptContentSuppressed) {
    auto const runs = Decode_html_clipboard("A<script>var x=1;</script>B");
    EXPECT_EQ(All_text(runs), L"AB");
}

TEST(text_html, Decode_StyleTagContentSuppressed) {
    auto const runs = Decode_html_clipboard("A<style>.foo{color:red}</style>B");
    EXPECT_EQ(All_text(runs), L"AB");
}

// ============================================================
// Google Docs pattern (realistic fragment)
// ============================================================

TEST(text_html, Decode_GoogleDocsPattern) {
    // Simplified but representative Google Docs clipboard fragment:
    // paragraph 1: "This is " (plain)
    // paragraph 2: "Bold" (bold), ", " (plain), "italic" (italic)
    // Two <br/> for blank line
    // paragraph 3: "Bbbb" (bold), "iiii" (italic), "uuuu" (underline),
    //              "ssss" (strikethrough)
    std::string const html = "<!--StartFragment-->"
                             "<p>This is </p>"
                             "<p><span style=\"font-weight:700\">Bold</span>"
                             "<span>, </span>"
                             "<span style=\"font-style:italic\">italic</span></p>"
                             "<br/><br/>"
                             "<p>"
                             "<span style=\"font-weight:700\">Bbbb</span>"
                             "<span style=\"font-style:italic\">iiii</span>"
                             "<span style=\"text-decoration:underline\">uuuu</span>"
                             "<span style=\"text-decoration:line-through\">ssss</span>"
                             "</p>"
                             "<!--EndFragment-->";

    auto const runs = Decode_html_clipboard(html);
    std::wstring const text = All_text(runs);
    EXPECT_EQ(text, L"This is \nBold, italic\n\nBbbbiiiiuuuussss");

    // Spot-check styling
    bool found_bold = false;
    bool found_italic = false;
    bool found_under = false;
    bool found_strike = false;
    for (auto const &r : runs) {
        if (r.text == L"Bbbb") {
            found_bold = r.flags.bold;
        }
        if (r.text == L"iiii") {
            found_italic = r.flags.italic;
        }
        if (r.text == L"uuuu") {
            found_under = r.flags.underline;
        }
        if (r.text == L"ssss") {
            found_strike = r.flags.strikethrough;
        }
    }
    EXPECT_TRUE(found_bold);
    EXPECT_TRUE(found_italic);
    EXPECT_TRUE(found_under);
    EXPECT_TRUE(found_strike);
}

// ============================================================
// UTF-8 multi-byte characters
// ============================================================

TEST(text_html, Decode_Utf8_TwoByteChar) {
    // U+00E9 = é, encoded in UTF-8 as 0xC3 0xA9
    std::string const html = "\xC3\xA9";
    auto const runs = Decode_html_clipboard(html);
    ASSERT_FALSE(runs.empty());
    EXPECT_EQ(runs[0].text, L"\u00E9");
}

// ============================================================
// Encode tests
// ============================================================

TEST(text_html, Encode_EmptyRuns_ProducesHeaderOnly) {
    std::vector<TextRun> const runs;
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_TRUE(out.starts_with("Version:0.9\r\n"));
    EXPECT_NE(out.find("<!--StartFragment--><!--EndFragment-->"), std::string::npos);
}

TEST(text_html, Encode_ContainsWindowsHtmlFormatHeader) {
    auto const out = Encode_html_clipboard(std::span<TextRun const>{});
    EXPECT_NE(out.find("StartHTML:"), std::string::npos);
    EXPECT_NE(out.find("StartFragment:"), std::string::npos);
    EXPECT_NE(out.find("EndFragment:"), std::string::npos);
}

TEST(text_html, Encode_PlainRun_EmittedWithoutSpan) {
    std::vector<TextRun> const runs = {Plain(L"Hello")};
    std::string const out = Encode_html_clipboard(runs);
    // Plain text should not be wrapped in a <span>.
    EXPECT_NE(out.find("Hello"), std::string::npos);
    EXPECT_EQ(out.find("<span"), std::string::npos);
}

TEST(text_html, Encode_BoldRun_HasFontWeight700) {
    std::vector<TextRun> const runs = {Bold(L"Bold")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("font-weight:700"), std::string::npos);
    EXPECT_NE(out.find("Bold"), std::string::npos);
}

TEST(text_html, Encode_ItalicRun_HasFontStyleItalic) {
    std::vector<TextRun> const runs = {Italic(L"Ital")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("font-style:italic"), std::string::npos);
}

TEST(text_html, Encode_UnderlineRun_HasTextDecorationUnderline) {
    std::vector<TextRun> const runs = {Underline(L"U")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("text-decoration:underline"), std::string::npos);
}

TEST(text_html, Encode_StrikeRun_HasTextDecorationLineThrough) {
    std::vector<TextRun> const runs = {Strike(L"S")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("text-decoration:line-through"), std::string::npos);
}

TEST(text_html, Encode_UnderlineAndStrike_CombinedInOneProperty) {
    std::vector<TextRun> const runs = {Underline_strike(L"US")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("text-decoration:underline line-through"), std::string::npos);
}

TEST(text_html, Encode_Newline_BecomesBrTag) {
    std::vector<TextRun> const runs = {Plain(L"A\nB")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("<br/>"), std::string::npos);
}

TEST(text_html, Encode_AmpersandEscaped) {
    std::vector<TextRun> const runs = {Plain(L"A&B")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("&amp;"), std::string::npos);
    EXPECT_EQ(out.find(" &B"), std::string::npos); // raw & must not appear in content
}

TEST(text_html, Encode_LtGtEscaped) {
    std::vector<TextRun> const runs = {Plain(L"<tag>")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("&lt;"), std::string::npos);
    EXPECT_NE(out.find("&gt;"), std::string::npos);
}

TEST(text_html, Encode_Utf8_TwoByteChar) {
    // U+00E9 (é) should appear as UTF-8 bytes 0xC3 0xA9 in the output.
    std::vector<TextRun> const runs = {Plain(L"\u00E9")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("\xC3\xA9"), std::string::npos);
}

TEST(text_html, Encode_Decode_RoundTrip_PlainText) {
    std::vector<TextRun> const original = {Plain(L"Hello world")};
    std::string const encoded = Encode_html_clipboard(original);
    auto const decoded = Decode_html_clipboard(encoded);
    EXPECT_EQ(All_text(decoded), L"Hello world");
}

TEST(text_html, Encode_Decode_RoundTrip_MixedStyles) {
    std::vector<TextRun> const original = {
        Bold(L"Bold"),           Plain(L" "), Italic(L"italic"), Plain(L" "),
        Underline_strike(L"us"),
    };
    std::string const encoded = Encode_html_clipboard(original);
    auto const decoded = Decode_html_clipboard(encoded);

    EXPECT_EQ(All_text(decoded), L"Bold italic us");

    bool found_bold = false;
    bool found_ital = false;
    bool found_under = false;
    bool found_strike = false;
    for (auto const &r : decoded) {
        if (r.text == L"Bold") {
            found_bold = r.flags.bold;
        }
        if (r.text == L"italic") {
            found_ital = r.flags.italic;
        }
        if (r.text == L"us") {
            found_under = r.flags.underline;
            found_strike = r.flags.strikethrough;
        }
    }
    EXPECT_TRUE(found_bold);
    EXPECT_TRUE(found_ital);
    EXPECT_TRUE(found_under);
    EXPECT_TRUE(found_strike);
}

TEST(text_html, Encode_Decode_RoundTrip_Newline) {
    std::vector<TextRun> const original = {Plain(L"A\nB")};
    std::string const encoded = Encode_html_clipboard(original);
    auto const decoded = Decode_html_clipboard(encoded);
    EXPECT_EQ(All_text(decoded), L"A\nB");
}

TEST(text_html, Encode_OffsetsAreConsistent) {
    // The StartHTML and StartFragment offsets in the header must point to the
    // correct positions within the payload.
    std::vector<TextRun> const runs = {Bold(L"Test")};
    std::string const out = Encode_html_clipboard(runs);

    // Parse StartFragment offset from the header.
    size_t const sf_label = out.find("StartFragment:");
    ASSERT_NE(sf_label, std::string::npos);
    size_t const sf_val_start = sf_label + std::string_view("StartFragment:").size();
    size_t const sf_offset = std::stoull(out.substr(sf_val_start, 8));

    // The byte at sf_offset should be the first character after <!--StartFragment-->.
    ASSERT_LT(sf_offset, out.size());
    EXPECT_EQ(out.substr(sf_offset, 6), "<span ");
}

// ============================================================
// Named HTML entity decode
// ============================================================

TEST(text_html, Decode_NamedEntity_Mdash) {
    EXPECT_EQ(All_text(Decode_html_clipboard("&mdash;")), L"\u2014");
}

TEST(text_html, Decode_NamedEntity_Ndash) {
    EXPECT_EQ(All_text(Decode_html_clipboard("&ndash;")), L"\u2013");
}

TEST(text_html, Decode_NamedEntity_Copy) {
    EXPECT_EQ(All_text(Decode_html_clipboard("&copy;")), L"\u00A9");
}

TEST(text_html, Decode_NamedEntity_Euro) {
    EXPECT_EQ(All_text(Decode_html_clipboard("&euro;")), L"\u20AC");
}

TEST(text_html, Decode_NamedEntity_Rsquo_Apostrophe) {
    // Google Docs uses &rsquo; for apostrophes in English text.
    EXPECT_EQ(All_text(Decode_html_clipboard("don&rsquo;t")), L"don\u2019t");
}

TEST(text_html, Decode_NamedEntity_CurlyQuotes) {
    auto const runs = Decode_html_clipboard("&ldquo;hi&rdquo;");
    ASSERT_FALSE(runs.empty());
    std::wstring const text = All_text(runs);
    ASSERT_GE(text.size(), 4u);
    EXPECT_EQ(text.front(), L'\u201C'); // left double quotation mark
    EXPECT_EQ(text.back(), L'\u201D');  // right double quotation mark
}

TEST(text_html, Decode_NamedEntity_AccentedLatin) {
    EXPECT_EQ(All_text(Decode_html_clipboard("caf&eacute;")), L"caf\u00E9");
}

TEST(text_html, Decode_NamedEntity_NbspIsNonBreaking) {
    // &nbsp; maps to U+00A0 (non-breaking space), not U+0020.
    EXPECT_EQ(All_text(Decode_html_clipboard("a&nbsp;b")), L"a\u00A0b");
}

TEST(text_html, Decode_NamedEntity_Hellip) {
    EXPECT_EQ(All_text(Decode_html_clipboard("&hellip;")), L"\u2026");
}

TEST(text_html, Decode_UnknownEntity_EmitsReplacement) {
    auto const runs = Decode_html_clipboard("&unknownxyz;");
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].text, std::wstring(1, L'\uFFFD'));
}

// ============================================================
// Semantic tag + inline CSS interaction
// ============================================================

TEST(text_html, Decode_SemanticBold_WithUnderlineStyle_AppliesBoth) {
    // <b style="text-decoration:underline"> should set bold AND underline.
    auto const runs =
        Decode_html_clipboard("<b style=\"text-decoration:underline\">text</b>");
    ASSERT_FALSE(runs.empty());
    bool found = false;
    for (auto const &r : runs) {
        if (r.text == L"text") {
            EXPECT_TRUE(r.flags.bold);
            EXPECT_TRUE(r.flags.underline);
            EXPECT_FALSE(r.flags.italic);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(text_html, Decode_Utf8_ThreeByteChar) {
    std::string const html = "\xE2\x82\xAC";
    auto const runs = Decode_html_clipboard(html);
    ASSERT_FALSE(runs.empty());
    EXPECT_EQ(runs[0].text, L"\u20AC");
}

TEST(text_html, Decode_Utf8_FourByteChar) {
    std::string const html = "\xF0\x9F\x98\x80";
    auto const runs = Decode_html_clipboard(html);
    ASSERT_FALSE(runs.empty());
    std::wstring const expected = {static_cast<wchar_t>(0xD83D),
                                   static_cast<wchar_t>(0xDE00)};
    EXPECT_EQ(runs[0].text, expected);
}

TEST(text_html, Decode_InvalidUtf8Sequence_ProducesReplacementCharacter) {
    auto const runs = Decode_html_clipboard("\xC3");
    ASSERT_EQ(runs.size(), 1u);
    ASSERT_EQ(runs[0].text.size(), 1u);
    EXPECT_EQ(runs[0].text[0], L'\uFFFD');
}

TEST(text_html, Decode_NumericEntity_HexWithUppercaseDigit) {
    auto const runs = Decode_html_clipboard("&#x4A;");
    EXPECT_EQ(All_text(runs), L"J");
}

TEST(text_html, Decode_DoctypeCommentAndHeadContentAreIgnored) {
    auto const runs = Decode_html_clipboard(
        "<!DOCTYPE html><!--comment--><head><title>ignored</title></head><p>A</p>");
    EXPECT_EQ(All_text(runs), L"A");
}

TEST(text_html, Decode_SelfClosingInlineTagRestoresPriorFlags) {
    auto const runs =
        Decode_html_clipboard("<b>X<span style=\"font-weight:normal\"/>Y</b>Z");
    EXPECT_EQ(All_text(runs), L"XYZ");
    ASSERT_GE(runs.size(), 2u);
    EXPECT_TRUE(runs[0].flags.bold);
    EXPECT_FALSE(runs.back().flags.bold);
}

TEST(text_html, Decode_CssNormalResetsInheritedBoldWithTrimmedWhitespace) {
    auto const runs = Decode_html_clipboard(
        "<b><span style=\"\tfont-weight:\tnormal\t\">x</span>y</b>");
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].text, L"x");
    EXPECT_FALSE(runs[0].flags.bold);
    EXPECT_EQ(runs[1].text, L"y");
    EXPECT_TRUE(runs[1].flags.bold);
}

TEST(text_html, Decode_CssNormalResetsInheritedItalic) {
    auto const runs =
        Decode_html_clipboard("<i><span style=\"font-style:normal\">x</span>y</i>");
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].text, L"x");
    EXPECT_FALSE(runs[0].flags.italic);
    EXPECT_EQ(runs[1].text, L"y");
    EXPECT_TRUE(runs[1].flags.italic);
}

TEST(text_html, Encode_Tab_BecomesNumericEntityAndRoundTrips) {
    std::vector<TextRun> const runs = {Plain(L"A\tB")};
    std::string const encoded = Encode_html_clipboard(runs);
    EXPECT_NE(encoded.find("&#9;"), std::string::npos);

    auto const decoded = Decode_html_clipboard(encoded);
    EXPECT_EQ(All_text(decoded), L"A\tB");
}

TEST(text_html, Encode_Utf8_ThreeByteChar) {
    std::vector<TextRun> const runs = {Plain(L"\u20AC")};
    std::string const out = Encode_html_clipboard(runs);
    EXPECT_NE(out.find("\xE2\x82\xAC"), std::string::npos);
}

TEST(text_html, Encode_NonBmpCharacter_RoundTrips) {
    std::wstring const emoji = {static_cast<wchar_t>(0xD83D),
                                static_cast<wchar_t>(0xDE00)};
    std::vector<TextRun> const runs = {Plain(emoji)};

    std::string const encoded = Encode_html_clipboard(runs);
    EXPECT_NE(encoded.find("\xF0\x9F\x98\x80"), std::string::npos);

    auto const decoded = Decode_html_clipboard(encoded);
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].text, emoji);
}
