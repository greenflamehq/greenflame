#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"

using namespace greenflame::core;

// Fixture: predefined rects for Phase 0 (plan: at least one test file with
// tests/fixtures).
class RectPxFixture : public ::testing::Test {
  protected:
    RectPxFixture() = default;
    RectPxFixture(RectPxFixture const &) = delete;
    RectPxFixture &operator=(RectPxFixture const &) = delete;
    RectPxFixture(RectPxFixture &&) = delete;
    RectPxFixture &operator=(RectPxFixture &&) = delete;
    ~RectPxFixture() override = default;

    RectPx inverted{10, 10, 0, 0};
    RectPx normal{0, 0, 10, 10};
    RectPx overlapping{5, 5, 15, 15};
    RectPx disjoint{20, 20, 30, 30};
};

TEST_F(RectPxFixture, NormalizeInvertedRect) {
    auto an = inverted.Normalized();
    EXPECT_EQ(an.left, 0);
    EXPECT_EQ(an.top, 0);
    EXPECT_EQ(an.right, 10);
    EXPECT_EQ(an.bottom, 10);
}

TEST_F(RectPxFixture, IntersectNormalizedWithOverlapping) {
    auto an = inverted.Normalized();
    auto i = RectPx::Intersect(an, overlapping);
    EXPECT_TRUE(i.has_value());
    EXPECT_EQ(i->left, 5);
    EXPECT_EQ(i->top, 5);
    EXPECT_EQ(i->right, 10);
    EXPECT_EQ(i->bottom, 10);
}

TEST_F(RectPxFixture, IntersectWithDisjointYieldsEmpty) {
    auto i = RectPx::Intersect(normal, disjoint);
    EXPECT_FALSE(i.has_value());
}

TEST(monitor_policy, CrossMonitorSelectionRule) {
    MonitorInfo m1{MonitorDpiScale{150}, MonitorOrientation::Landscape};
    MonitorInfo m2{MonitorDpiScale{150}, MonitorOrientation::Landscape};
    MonitorInfo m3{MonitorDpiScale{125}, MonitorOrientation::Landscape};

    EXPECT_TRUE(Is_allowed(Decide_cross_monitor_selection(std::span{&m1, 1})));

    MonitorInfo pair1[] = {m1, m2};
    EXPECT_TRUE(Is_allowed(Decide_cross_monitor_selection(pair1)));

    MonitorInfo pair2[] = {m1, m3};
    EXPECT_EQ(Decide_cross_monitor_selection(pair2),
              CrossMonitorSelectionDecision::RefusedDifferentDpiScale);
}

TEST(rect_px, TryGetSize_ReturnsCorrectDimensions) {
    int32_t w = 0;
    int32_t h = 0;
    EXPECT_TRUE(RectPx::From_ltrb(10, 20, 110, 70).Try_get_size(w, h));
    EXPECT_EQ(w, 100);
    EXPECT_EQ(h, 50);
}

TEST(rect_px, TryGetSize_ReturnsFalseForEmptyRect) {
    int32_t w = 0;
    int32_t h = 0;
    EXPECT_FALSE(RectPx::From_ltrb(10, 10, 10, 10).Try_get_size(w, h));
    EXPECT_FALSE(RectPx::From_ltrb(10, 10, 5, 20).Try_get_size(w, h));
}

TEST(rect_px, TryGetSize_ReturnsFalseOnOverflow) {
    int32_t w = 0;
    int32_t h = 0;
    RectPx const r = RectPx::From_ltrb(INT32_MIN, 0, INT32_MAX, 1);
    EXPECT_FALSE(r.Try_get_size(w, h));
}

TEST(insets_px, TryExpandSize_ReturnsExpandedDimensions) {
    InsetsPx const insets{4, 8, 12, 16};
    int32_t ow = 0;
    int32_t oh = 0;
    EXPECT_TRUE(insets.Try_expand_size(100, 50, ow, oh));
    EXPECT_EQ(ow, 116); // 100 + 4 + 12
    EXPECT_EQ(oh, 74);  // 50 + 8 + 16
}

TEST(insets_px, TryExpandSize_ReturnsFalseForNonPositiveSource) {
    InsetsPx const insets{1, 1, 1, 1};
    int32_t ow = 0;
    int32_t oh = 0;
    EXPECT_FALSE(insets.Try_expand_size(0, 10, ow, oh));
    EXPECT_FALSE(insets.Try_expand_size(10, 0, ow, oh));
    EXPECT_FALSE(insets.Try_expand_size(-1, 10, ow, oh));
}

TEST(insets_px, TryExpandSize_ReturnsFalseForNegativeInset) {
    int32_t ow = 0;
    int32_t oh = 0;
    EXPECT_FALSE((InsetsPx{-1, 0, 0, 0}.Try_expand_size(10, 10, ow, oh)));
    EXPECT_FALSE((InsetsPx{0, -1, 0, 0}.Try_expand_size(10, 10, ow, oh)));
    EXPECT_FALSE((InsetsPx{0, 0, -1, 0}.Try_expand_size(10, 10, ow, oh)));
    EXPECT_FALSE((InsetsPx{0, 0, 0, -1}.Try_expand_size(10, 10, ow, oh)));
}

TEST(insets_px, TryExpandSize_ReturnsFalseOnOverflow) {
    int32_t ow = 0;
    int32_t oh = 0;
    EXPECT_FALSE((InsetsPx{1, 0, 0, 0}.Try_expand_size(INT32_MAX, 1, ow, oh)));
}
