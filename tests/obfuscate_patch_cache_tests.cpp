#include "greenflame_core/annotation_controller.h"
#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/obfuscate_patch_cache.h"
#include "greenflame_core/obfuscate_raster.h"

using namespace greenflame::core;

namespace {

constexpr int32_t kBlockSize = 4;
constexpr uint8_t kSaltStep = 17;

// A source that, like the capture renderer, depends only on the lower annotations
// whose visual bounds touch the bounds, and on salt (the capture's pixels).
struct FakeSource final : public IObfuscateSourceProvider {
    [[nodiscard]] std::optional<BgraBitmap>
    Build_composited_source(RectPx bounds,
                            std::span<const Annotation> lower_annotations) override {
        ++calls;
        if (fail) {
            return std::nullopt;
        }
        uint32_t hash = salt;
        for (Annotation const &annotation : lower_annotations) {
            RectPx const visual = Annotation_visual_bounds(annotation);
            if (!RectPx::Intersect(visual, bounds).has_value()) {
                continue;
            }
            hash = hash * 31u + static_cast<uint32_t>(annotation.id);
            hash = hash * 31u + static_cast<uint32_t>(visual.left);
            hash = hash * 31u + static_cast<uint32_t>(visual.top);
            if (auto const *const obfuscate =
                    std::get_if<ObfuscateAnnotation>(&annotation.data)) {
                hash = hash * 31u + static_cast<uint32_t>(obfuscate->raster_hash);
            }
        }
        int32_t const width = bounds.Width();
        int32_t const height = bounds.Height();
        BgraBitmap bitmap{
            .width_px = width,
            .height_px = height,
            .row_bytes = width * 4,
            .premultiplied_bgra = std::vector<uint8_t>(static_cast<size_t>(width) * 4u *
                                                       static_cast<size_t>(height)),
        };
        for (size_t at = 0; at < bitmap.premultiplied_bgra.size(); at += 4u) {
            bitmap.premultiplied_bgra[at] = static_cast<uint8_t>(hash);
            bitmap.premultiplied_bgra[at + 1u] = static_cast<uint8_t>(hash >> 8u);
            bitmap.premultiplied_bgra[at + 2u] = static_cast<uint8_t>(hash >> 16u);
            bitmap.premultiplied_bgra[at + 3u] = 255u;
        }
        return bitmap;
    }

    uint32_t salt = 1;
    bool fail = false;
    size_t calls = 0;
};

[[nodiscard]] Annotation Make_rect(uint64_t id, RectPx bounds) {
    return Annotation{
        .id = id,
        .data = RectangleAnnotation{.outer_bounds = bounds, .style = {.width_px = 2}}};
}

[[nodiscard]] Annotation Make_obfuscate(uint64_t id, RectPx bounds,
                                        IObfuscateSourceProvider &source,
                                        std::span<const Annotation> lower) {
    Annotation annotation{
        .id = id,
        .data = ObfuscateAnnotation{.bounds = bounds, .block_size = kBlockSize}};
    std::optional<Annotation> built =
        Rebuild_obfuscate_from_source(annotation, lower, source);
    EXPECT_TRUE(built.has_value());
    return built.value_or(annotation);
}

// The patches rebuilt from scratch, every lower annotation in, patches applied.
[[nodiscard]] std::vector<AnnotationPreviewPatch>
Uncached(std::span<const Annotation> annotations, std::span<const size_t> indices,
         IObfuscateSourceProvider &source) {
    std::vector<AnnotationPreviewPatch> patches;
    for (size_t const index : indices) {
        std::vector<Annotation> lower(annotations.begin(),
                                      annotations.begin() +
                                          static_cast<std::ptrdiff_t>(index));
        for (AnnotationPreviewPatch const &patch : patches) {
            lower[patch.index] = patch.annotation;
        }
        std::optional<Annotation> built =
            Rebuild_obfuscate_from_source(annotations[index], lower, source);
        EXPECT_TRUE(built.has_value());
        patches.push_back({index, built.value_or(Annotation{})});
    }
    return patches;
}

void Expect_patches_equal(std::span<const AnnotationPreviewPatch> actual,
                          std::span<const AnnotationPreviewPatch> expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        EXPECT_EQ(actual[i].index, expected[i].index);
        EXPECT_EQ(actual[i].annotation, expected[i].annotation);
    }
}

[[nodiscard]] uint8_t const *Raster_data(AnnotationPreviewPatch const &patch) {
    return std::get<ObfuscateAnnotation>(patch.annotation.data)
        .premultiplied_bgra.data();
}

// A rectangle under an obfuscate, a second rectangle off it, and the obfuscate.
struct Scene final {
    FakeSource source;
    std::vector<Annotation> doc;
    std::vector<size_t> indices = {2};

    Scene() {
        doc.push_back(Make_rect(1, RectPx::From_ltrb(10, 10, 30, 30)));
        doc.push_back(Make_rect(2, RectPx::From_ltrb(200, 200, 220, 220)));
        doc.push_back(Make_obfuscate(3, RectPx::From_ltrb(0, 0, 40, 40), source,
                                     std::span<const Annotation>(doc)));
    }
};

} // namespace

TEST(obfuscate_patch_cache, KeepsPatchWhileKeyHolds) {
    Scene scene;
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    ASSERT_EQ(cache.Patches().size(), 1u);
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));
    uint8_t const *const storage = Raster_data(cache.Patches()[0]);
    size_t const calls = scene.source.calls;

    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(scene.source.calls, calls);
    EXPECT_EQ(cache.Statistics().hits, 1u);
    EXPECT_EQ(cache.Statistics().builds, 1u);
    // Kept, not copied: the raster's storage (what uploads are keyed by) stays.
    EXPECT_EQ(Raster_data(cache.Patches()[0]), storage);
}

TEST(obfuscate_patch_cache, RebuildsWhenALowerTouchingAnnotationChanges) {
    Scene scene;
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    std::vector<AnnotationPreviewPatch> const before(cache.Patches().begin(),
                                                     cache.Patches().end());

    scene.doc[0] = Translate_annotation(scene.doc[0], {3, 0});
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(cache.Statistics().builds, 2u);
    EXPECT_NE(cache.Patches()[0].annotation, before[0].annotation);
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));

    // Style counts too, not only the bounds: the full value is the key.
    std::get<RectangleAnnotation>(scene.doc[0].data).style.color = RGB(1, 2, 3);
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(cache.Statistics().builds, 3u);
}

TEST(obfuscate_patch_cache, KeepsPatchWhenOnlyALowerAnnotationOffItChanges) {
    Scene scene;
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    scene.doc[1] = Translate_annotation(scene.doc[1], {5, 5});
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(cache.Statistics().builds, 1u);
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));
}

TEST(obfuscate_patch_cache, RebuildsWhenBoundsBlockSizeOrSourceChange) {
    Scene scene;
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));

    // The obfuscate itself moves a little: the same rectangle still touches it, so
    // only its bounds tell the old patch from the new one.
    auto &obfuscate = std::get<ObfuscateAnnotation>(scene.doc[2].data);
    obfuscate.bounds = RectPx::From_ltrb(2, 0, 42, 40);
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(cache.Statistics().builds, 2u);
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));

    obfuscate.block_size = kBlockSize + 1;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(cache.Statistics().builds, 3u);

    // The capture was rebuilt (Ctrl+K): new pixels, new generation.
    scene.source.salt += kSaltStep;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 2));
    EXPECT_EQ(cache.Statistics().builds, 4u);
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));
}

TEST(obfuscate_patch_cache, StackedObfuscateFollowsTheLowerPatch) {
    Scene scene;
    // A second obfuscate above the first, overlapping it but off the rectangle.
    scene.doc.push_back(Make_obfuscate(4, RectPx::From_ltrb(35, 35, 80, 80),
                                       scene.source,
                                       std::span<const Annotation>(scene.doc)));
    scene.indices = {2, 3};
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    std::vector<AnnotationPreviewPatch> const before(cache.Patches().begin(),
                                                     cache.Patches().end());

    // The rectangle moves under the lower one only; the upper one's source is the
    // lower one's new raster.
    scene.doc[0] = Translate_annotation(scene.doc[0], {2, 0});
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(cache.Statistics().builds, 4u);
    EXPECT_NE(cache.Patches()[1].annotation, before[1].annotation);
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));
}

TEST(obfuscate_patch_cache, LowerObfuscateWithoutRasterIsNeverKept) {
    Scene scene;
    // An obfuscate with no raster pixelates whatever is under it at draw time.
    scene.doc.insert(scene.doc.begin() + 2,
                     Annotation{.id = 5,
                                .data = ObfuscateAnnotation{
                                    .bounds = RectPx::From_ltrb(20, 20, 60, 60),
                                    .block_size = kBlockSize}});
    scene.indices = {3};
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_EQ(cache.Statistics().hits, 0u);
    EXPECT_EQ(cache.Statistics().builds, 2u);
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));
}

TEST(obfuscate_patch_cache, FailedBuildClearsAndReportsFailure) {
    Scene scene;
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    scene.doc[0] = Translate_annotation(scene.doc[0], {1, 0});
    scene.source.fail = true;
    EXPECT_FALSE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    EXPECT_TRUE(cache.Patches().empty());

    // Nothing stale survives: the next build starts over.
    scene.source.fail = false;
    ASSERT_TRUE(cache.Update(scene.doc, scene.indices, scene.source, 1));
    Expect_patches_equal(cache.Patches(),
                         Uncached(scene.doc, scene.indices, scene.source));
}

TEST(obfuscate_patch_cache, SkipsIndicesItCannotPatch) {
    Scene scene;
    std::vector<size_t> const indices = {0, 2, 9};
    ObfuscatePatchCache cache;
    ASSERT_TRUE(cache.Update(scene.doc, indices, scene.source, 1));
    ASSERT_EQ(cache.Patches().size(), 1u);
    EXPECT_EQ(cache.Patches()[0].index, 2u);
}
