#include "greenflame_core/string_utils.h"

using namespace greenflame::core;

TEST(string_utils, Contains_no_case_Exact) {
    EXPECT_TRUE(Contains_no_case(L"Greenflame", L"greenflame"));
}

TEST(string_utils, Contains_no_case_Substring) {
    EXPECT_TRUE(Contains_no_case(L"AlphaBetaGamma", L"BETA"));
}

TEST(string_utils, Contains_no_case_EmptyNeedleIsFalse) {
    EXPECT_FALSE(Contains_no_case(L"text", L""));
}

TEST(string_utils, Contains_no_case_NotFound) {
    EXPECT_FALSE(Contains_no_case(L"Greenflame", L"flamethrower"));
}

TEST(string_utils, Equals_no_case_TrueForDifferentCase) {
    EXPECT_TRUE(Equals_no_case(L"WindowTitle", L"windowtitle"));
}

TEST(string_utils, Equals_no_case_FalseForDifferentLength) {
    EXPECT_FALSE(Equals_no_case(L"abc", L"abcd"));
}

TEST(string_utils, Filename_from_path_WithDirectory) {
    EXPECT_EQ(Filename_from_path(L"C:\\shots\\capture.png"), L"capture.png");
}

TEST(string_utils, Filename_from_path_NoSlash) {
    EXPECT_EQ(Filename_from_path(L"capture.png"), L"capture.png");
}

TEST(string_utils, Filename_from_path_Empty) {
    EXPECT_EQ(Filename_from_path(L""), L"");
}

TEST(string_utils, Filename_from_path_TrailingSlashReturnsInput) {
    EXPECT_EQ(Filename_from_path(L"C:\\shots\\"), L"C:\\shots\\");
}

TEST(string_utils, Build_saved_selection_balloon_message_SavedOnly) {
    EXPECT_EQ(Build_saved_selection_balloon_message(L"C:\\shots\\capture.png", false),
              L"Saved: capture.png.");
}

TEST(string_utils, Build_saved_selection_path_copy_failed_message_NamesTheFile) {
    EXPECT_EQ(
        Build_saved_selection_path_copy_failed_message(L"C:\\shots\\capture.png"),
        L"Saved: capture.png, but its path could not be copied to the clipboard.");
}

TEST(string_utils, Build_saved_selection_balloon_message_SavedAndCopied) {
    EXPECT_EQ(Build_saved_selection_balloon_message(L"C:\\shots\\capture.png", true),
              L"Saved: capture.png (file copied to clipboard).");
}
