#pragma once

// The obfuscate preview patches an annotation edit draws, kept from frame to frame
// while their source cannot have changed. Only the overlay's paint uses it: the commit
// path (AnnotationController) always rebuilds, uncached.

#include "greenflame_core/annotation_types.h"

namespace greenflame::core {

class IObfuscateSourceProvider;

// An annotation drawn in place of annotations[index] for one frame, so a preview never
// copies the whole document.
struct AnnotationPreviewPatch {
    size_t index = 0;
    Annotation annotation = {};
};

class ObfuscatePatchCache final {
  public:
    // How often Update kept a patch, and how often it built one.
    struct Stats final {
        size_t hits = 0;
        size_t builds = 0;
    };

    // Makes Patches() one patch per preview index, in order, each obfuscate rebuilt
    // from its source with the patches below it applied. A patch is kept while its key
    // holds: the same index and id, bounds, block size, source_generation (which must
    // change whenever the source capture does), and the same value of every lower
    // annotation, patches applied, whose visual bounds touch it. Indices past the end,
    // non-obfuscates and empty bounds get no patch. Returns false, with the cache
    // cleared, when a patch cannot be built.
    [[nodiscard]] bool Update(std::span<const Annotation> annotations,
                              std::span<const size_t> preview_indices,
                              IObfuscateSourceProvider &source_provider,
                              uint64_t source_generation);

    // A kept patch keeps its raster's storage, so draw code can tell it is unchanged.
    [[nodiscard]] std::span<const AnnotationPreviewPatch> Patches() const noexcept {
        return patches_;
    }
    void Clear() noexcept;
    [[nodiscard]] Stats Statistics() const noexcept { return stats_; }

  private:
    struct Key final {
        size_t index = 0;
        uint64_t id = 0;
        RectPx bounds = {};
        int32_t block_size = 0;
        uint64_t source_generation = 0;
        std::vector<Annotation> lower = {};
        // A lower obfuscate with no raster pixelates whatever is under it, touching
        // or not: such a patch is built from every lower annotation and never kept.
        bool uncacheable = false;
    };

    std::vector<Key> keys_ = {}; // parallel to patches_
    std::vector<AnnotationPreviewPatch> patches_ = {};
    Stats stats_ = {};
};

} // namespace greenflame::core
