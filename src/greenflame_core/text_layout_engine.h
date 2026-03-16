#pragma once

#include "greenflame_core/bubble_annotation_types.h"
#include "greenflame_core/text_annotation_types.h"

namespace greenflame::core {

struct DraftTextLayoutResult final {
    RectPx visual_bounds = {};
    std::vector<RectPx> selection_rects = {};
    RectPx caret_rect = {};
    RectPx overwrite_caret_rect = {};
    int32_t preferred_x_px = 0;
};

class ITextLayoutEngine {
  public:
    ITextLayoutEngine() = default;
    ITextLayoutEngine(ITextLayoutEngine const &) = default;
    ITextLayoutEngine &operator=(ITextLayoutEngine const &) = default;
    ITextLayoutEngine(ITextLayoutEngine &&) = default;
    ITextLayoutEngine &operator=(ITextLayoutEngine &&) = default;
    virtual ~ITextLayoutEngine() = default;

    [[nodiscard]] virtual DraftTextLayoutResult
    Build_draft_layout(TextDraftBuffer const &buf, PointPx origin) = 0;
    [[nodiscard]] virtual int32_t Hit_test_point(TextDraftBuffer const &buf,
                                                 PointPx origin, PointPx point) = 0;
    [[nodiscard]] virtual int32_t Move_vertical(TextDraftBuffer const &buf,
                                                PointPx origin, int32_t offset,
                                                int delta_lines,
                                                int32_t preferred_x_px) = 0;
    virtual void Rasterize(TextAnnotation &annotation) = 0;
    virtual void Rasterize_bubble(BubbleAnnotation &annotation) = 0;
};

} // namespace greenflame::core
