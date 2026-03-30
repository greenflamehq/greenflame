#include "greenflame_core/text_rtf.h"

using namespace greenflame::core;

namespace {

[[nodiscard]] TextRun Plain(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{}};
}

[[nodiscard]] TextRun Bold(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.bold = true}};
}

[[nodiscard]] TextRun Strike(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.strikethrough = true}};
}

[[nodiscard]] TextRun All_flags(std::wstring text) {
    return TextRun{std::move(text), TextStyleFlags{.bold = true,
                                                   .italic = true,
                                                   .underline = true,
                                                   .strikethrough = true}};
}

} // namespace

// ============================================================
// Encode tests
// ============================================================

TEST(text_rtf, Encode_EmptyRuns_ProducesValidHeader) {
    std::string const rtf = Encode_rtf(std::span<TextRun const>{});
    EXPECT_TRUE(rtf.find("{\\rtf1") == 0);
    EXPECT_EQ(rtf.back(), '}');
}

TEST(text_rtf, Encode_PlainText_RoundTrips) {
    std::vector<TextRun> const runs = {Plain(L"hello")};
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].text, L"hello");
    EXPECT_EQ(decoded[0].flags, TextStyleFlags{});
}

TEST(text_rtf, Encode_BoldRun_ContainsBoldControlWord) {
    std::vector<TextRun> const runs = {Bold(L"hi")};
    std::string const rtf = Encode_rtf(runs);
    EXPECT_NE(rtf.find("\\b "), std::string::npos);
}

TEST(text_rtf, Encode_AllFourFlags_RoundTrip) {
    std::vector<TextRun> const runs = {All_flags(L"x")};
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_TRUE(decoded[0].flags.bold);
    EXPECT_TRUE(decoded[0].flags.italic);
    EXPECT_TRUE(decoded[0].flags.underline);
    EXPECT_TRUE(decoded[0].flags.strikethrough);
}

TEST(text_rtf, Encode_MixedRuns_RoundTrip) {
    std::vector<TextRun> const runs = {
        Plain(L"plain"),
        Bold(L"bold"),
        TextRun{L"both", TextStyleFlags{.bold = true, .italic = true}},
    };
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 3u);
    EXPECT_EQ(decoded[0].text, L"plain");
    EXPECT_EQ(decoded[0].flags, TextStyleFlags{});
    EXPECT_EQ(decoded[1].text, L"bold");
    EXPECT_TRUE(decoded[1].flags.bold);
    EXPECT_FALSE(decoded[1].flags.italic);
    EXPECT_EQ(decoded[2].text, L"both");
    EXPECT_TRUE(decoded[2].flags.bold);
    EXPECT_TRUE(decoded[2].flags.italic);
}

TEST(text_rtf, Encode_Newline_RoundTrip) {
    std::vector<TextRun> const runs = {Plain(L"line1\nline2")};
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].text, L"line1\nline2");
}

TEST(text_rtf, Encode_RtfSpecialChars_RoundTrip) {
    // Backslash, open-brace, and close-brace must survive the round-trip.
    std::vector<TextRun> const runs = {Plain(L"a\\b{c}d")};
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].text, L"a\\b{c}d");
}

TEST(text_rtf, Encode_HighUnicode_RoundTrip) {
    // Non-ASCII BMP characters must survive via the Unicode escape mechanism.
    std::vector<TextRun> const runs = {Plain(L"\u00E9\u4E2D")};
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].text, L"\u00E9\u4E2D");
}

// ============================================================
// Decode tests
// ============================================================

TEST(text_rtf, Decode_PlainText) {
    std::string const rtf = "{\\rtf1\\ansi\\pard hello}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    ASSERT_FALSE(runs.empty());
    std::wstring text;
    for (TextRun const &run : runs) {
        text += run.text;
    }
    EXPECT_EQ(text, L"hello");
    for (TextRun const &run : runs) {
        EXPECT_EQ(run.flags, TextStyleFlags{});
    }
}

TEST(text_rtf, Decode_BoldItalicUnderlineStrikethrough) {
    std::string const rtf = "{\\rtf1\\pard \\b bold\\b0  \\i italic\\i0  \\ul "
                            "under\\ulnone  \\strike struck\\strike0 }";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    // Find each run by text content
    auto find_run = [&](std::wstring_view text) -> TextStyleFlags {
        for (TextRun const &run : runs) {
            if (run.text == text) {
                return run.flags;
            }
        }
        return TextStyleFlags{};
    };
    EXPECT_TRUE(find_run(L"bold").bold);
    EXPECT_TRUE(find_run(L"italic").italic);
    EXPECT_TRUE(find_run(L"under").underline);
    EXPECT_TRUE(find_run(L"struck").strikethrough);
}

TEST(text_rtf, Decode_PlainResetsFlags) {
    std::string const rtf = "{\\rtf1\\pard \\b bold\\plain normal}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    ASSERT_FALSE(runs.empty());
    TextRun const &last = runs.back();
    EXPECT_FALSE(last.flags.bold);
    // The last run's text should contain the word "normal".
    EXPECT_NE(last.text.find(L'n'), std::wstring::npos);
}

TEST(text_rtf, Decode_UlnoneResetsUnderline) {
    std::string const rtf = "{\\rtf1\\pard \\ul underlined\\ulnone  normal}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    ASSERT_GE(runs.size(), 2u);
    bool saw_underlined = false;
    bool saw_plain = false;
    for (TextRun const &run : runs) {
        if (run.flags.underline && run.text.find(L'u') != std::wstring::npos) {
            saw_underlined = true;
        }
        if (!run.flags.underline && run.text.find(L'n') != std::wstring::npos) {
            saw_plain = true;
        }
    }
    EXPECT_TRUE(saw_underlined);
    EXPECT_TRUE(saw_plain);
}

TEST(text_rtf, Decode_NestedGroupsRestoreFlags) {
    // Entering a group saves flags; leaving restores them.
    std::string const rtf = "{\\rtf1\\pard outer{\\b inner}outer_again}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    // Find a run that contains 'i' (from "inner") — should be bold.
    // Find a run after the group — should not be bold.
    bool inner_bold = false;
    bool outer_again_not_bold = false;
    bool past_inner = false;
    for (TextRun const &run : runs) {
        if (run.text.find(L'i') != std::wstring::npos && !past_inner) {
            inner_bold = run.flags.bold;
            past_inner = true;
        } else if (past_inner) {
            outer_again_not_bold = !run.flags.bold;
        }
    }
    EXPECT_TRUE(inner_bold);
    EXPECT_TRUE(outer_again_not_bold);
}

TEST(text_rtf, Decode_UnknownControlWords_Ignored) {
    // Font size, color, and highlight control words must be stripped; text kept.
    std::string const rtf = "{\\rtf1\\pard \\fs24\\cf1\\highlight3\\b bold\\b0  plain}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    ASSERT_FALSE(runs.empty());
    bool found_bold = false;
    bool found_plain = false;
    for (TextRun const &run : runs) {
        if (run.flags.bold && run.text.find(L'b') != std::wstring::npos) {
            found_bold = true;
        }
        if (!run.flags.bold && run.text.find(L'p') != std::wstring::npos) {
            found_plain = true;
        }
    }
    EXPECT_TRUE(found_bold);
    EXPECT_TRUE(found_plain);
}

TEST(text_rtf, Decode_StarDestinationGroupSkipped) {
    // Content inside {\* ...} extended destinations must not be emitted.
    std::string const rtf = "{\\rtf1\\pard {\\*\\generator Greenflame;}plain}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    ASSERT_FALSE(runs.empty());
    std::wstring combined;
    for (TextRun const &run : runs) {
        combined += run.text;
    }
    EXPECT_EQ(combined, L"plain");
}

TEST(text_rtf, Decode_MalformedInput_ReturnsEmpty) {
    EXPECT_TRUE(Decode_rtf("not rtf at all").empty());
    EXPECT_TRUE(Decode_rtf("").empty());
}

TEST(text_rtf, Decode_ParBecomesNewline) {
    std::string const rtf = "{\\rtf1\\pard line1\\par line2}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    std::wstring combined;
    for (TextRun const &run : runs) {
        combined += run.text;
    }
    EXPECT_NE(combined.find(L'\n'), std::wstring::npos);
    EXPECT_NE(combined.find(L"line1"), std::wstring::npos);
    EXPECT_NE(combined.find(L"line2"), std::wstring::npos);
}

TEST(text_rtf, Decode_StrikethroughRoundTrip) {
    std::vector<TextRun> const runs = {Strike(L"struck")};
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].text, L"struck");
    EXPECT_TRUE(decoded[0].flags.strikethrough);
}

TEST(text_rtf, Decode_Strips_FontColorSize_KeepsStyle) {
    // RTF with font size and color controls: only bold/italic should survive.
    std::string const rtf =
        "{\\rtf1\\pard \\fs48\\cf1\\b bold\\b0 \\i italic\\i0 plain}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    bool found_bold = false;
    bool found_italic = false;
    bool found_plain = false;
    for (TextRun const &run : runs) {
        if (run.flags.bold) {
            found_bold = true;
        }
        if (run.flags.italic) {
            found_italic = true;
        }
        if (!run.flags.bold && !run.flags.italic &&
            run.text.find(L'p') != std::wstring::npos) {
            found_plain = true;
        }
    }
    EXPECT_TRUE(found_bold);
    EXPECT_TRUE(found_italic);
    EXPECT_TRUE(found_plain);
}

TEST(text_rtf, Decode_FonttblGroupSkipped) {
    // Text inside the font-table group must not appear in the output.
    std::string const rtf = "{\\rtf1{\\fonttbl{\\f0\\fnil Segoe UI;}}\\pard content}";
    std::vector<TextRun> const runs = Decode_rtf(rtf);
    std::wstring combined;
    for (TextRun const &run : runs) {
        combined += run.text;
    }
    // "Segoe UI" and ";" must not appear in the decoded text.
    EXPECT_EQ(combined.find(L"Segoe"), std::wstring::npos);
    EXPECT_NE(combined.find(L"content"), std::wstring::npos);
}

TEST(text_rtf, Encode_NonBmpCharacter_RoundTrip) {
    // U+1F600 GRINNING FACE — encoded as a surrogate pair in UTF-16.
    // The RTF encoder emits two \uN? escapes (one per surrogate half);
    // the decoder reconstructs the pair.
    std::wstring const emoji = {static_cast<wchar_t>(0xD83D),
                                static_cast<wchar_t>(0xDE00)};
    std::vector<TextRun> const runs = {Plain(emoji)};
    std::vector<TextRun> const decoded = Decode_rtf(Encode_rtf(runs));
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].text, emoji);
}

TEST(text_rtf, Decode_Uc0_ZeroReplacementChars) {
    // \uc0 means zero replacement chars follow each \u escape,
    // so the '?' and 'e' immediately after \u233 are not skipped.
    std::vector<TextRun> const decoded = Decode_rtf("{\\rtf1\\uc0 \\u233?e}");
    ASSERT_FALSE(decoded.empty());
    std::wstring text;
    for (TextRun const &r : decoded) {
        text += r.text;
    }
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text[0], L'\u00E9'); // é from \u233
    EXPECT_EQ(text[1], L'?');      // not skipped (uc_count = 0)
    EXPECT_EQ(text[2], L'e');
}
