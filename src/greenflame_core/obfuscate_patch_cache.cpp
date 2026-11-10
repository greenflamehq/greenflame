#include "greenflame_core/obfuscate_patch_cache.h"

#include "greenflame_core/annotation_controller.h"
#include "greenflame_core/annotation_hit_test.h"
#include "greenflame_core/obfuscate_raster.h"

namespace greenflame::core {

namespace {

[[nodiscard]] bool Is_obfuscate_without_raster(Annotation const &annotation) noexcept {
    auto const *const obfuscate = std::get_if<ObfuscateAnnotation>(&annotation.data);
    return obfuscate != nullptr && obfuscate->premultiplied_bgra.empty();
}

} // namespace

bool ObfuscatePatchCache::Update(std::span<const Annotation> annotations,
                                 std::span<const size_t> preview_indices,
                                 IObfuscateSourceProvider &source_provider,
                                 uint64_t source_generation) {
    std::vector<Key> new_keys;
    std::vector<AnnotationPreviewPatch> new_patches;
    new_keys.reserve(preview_indices.size());
    // Reserved up front: the lower annotations below point into it.
    new_patches.reserve(preview_indices.size());
    auto const lower_at = [&](size_t index) -> Annotation const & {
        for (AnnotationPreviewPatch const &patch : new_patches) {
            if (patch.index == index) {
                return patch.annotation;
            }
        }
        return annotations[index];
    };

    for (size_t const index : preview_indices) {
        if (index >= annotations.size()) {
            continue;
        }
        Annotation const &annotation = annotations[index];
        auto const *const obfuscate =
            std::get_if<ObfuscateAnnotation>(&annotation.data);
        if (obfuscate == nullptr) {
            continue;
        }
        RectPx const bounds = obfuscate->bounds.Normalized();
        if (bounds.Is_empty()) {
            continue;
        }

        // The capture renderer composites only the lower annotations whose visual
        // bounds touch the obfuscate, so they are its whole source.
        std::vector<Annotation const *> touching;
        bool uncacheable = false;
        for (size_t lower = 0; lower < index; ++lower) {
            Annotation const &candidate = lower_at(lower);
            if (RectPx::Intersect(Annotation_visual_bounds(candidate), bounds)
                    .has_value()) {
                touching.push_back(&candidate);
                uncacheable = uncacheable || Is_obfuscate_without_raster(candidate);
            }
        }

        Key key{
            .index = index,
            .id = annotation.id,
            .bounds = bounds,
            .block_size = Clamp_obfuscate_block_size(obfuscate->block_size),
            .source_generation = source_generation,
            .lower = {},
            .uncacheable = uncacheable,
        };
        auto const old = std::ranges::find_if(
            keys_, [&](Key const &entry) { return entry.id == annotation.id; });
        bool const hit =
            !uncacheable && old != keys_.end() && !old->uncacheable &&
            old->index == key.index && old->bounds == key.bounds &&
            old->block_size == key.block_size &&
            old->source_generation == key.source_generation &&
            std::ranges::equal(old->lower, touching,
                               [](Annotation const &kept, Annotation const *current) {
                                   return kept == *current;
                               });
        if (hit) {
            size_t const at = static_cast<size_t>(old - keys_.begin());
            new_keys.push_back(std::move(*old));
            // Moved, not copied: the raster keeps its storage.
            new_patches.push_back(std::move(patches_[at]));
            ++stats_.hits;
            continue;
        }

        std::optional<Annotation> rebuilt = std::nullopt;
        if (uncacheable) {
            std::vector<Annotation> lower(annotations.begin(),
                                          annotations.begin() +
                                              static_cast<std::ptrdiff_t>(index));
            for (AnnotationPreviewPatch const &patch : new_patches) {
                lower[patch.index] = patch.annotation;
            }
            rebuilt = Rebuild_obfuscate_from_source(annotation, lower, source_provider);
        } else {
            key.lower.reserve(touching.size());
            for (Annotation const *const lower : touching) {
                key.lower.push_back(*lower);
            }
            rebuilt =
                Rebuild_obfuscate_from_source(annotation, key.lower, source_provider);
        }
        if (!rebuilt.has_value()) {
            Clear();
            return false;
        }
        ++stats_.builds;
        new_keys.push_back(std::move(key));
        new_patches.push_back(AnnotationPreviewPatch{index, std::move(*rebuilt)});
    }

    keys_ = std::move(new_keys);
    patches_ = std::move(new_patches);
    return true;
}

void ObfuscatePatchCache::Clear() noexcept {
    keys_.clear();
    patches_.clear();
}

} // namespace greenflame::core
