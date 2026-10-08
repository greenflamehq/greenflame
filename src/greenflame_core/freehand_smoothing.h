#pragma once

#include "greenflame_core/rect_px.h"

namespace greenflame::core {

enum class FreehandSmoothingMode : uint8_t {
    Off,
    Smooth,
};

struct FreehandPreviewPlan final {
    size_t stable_raw_point_count = 0;
    size_t tail_start_index = 0;
    std::vector<PointPx> tail_points = {};
};

struct FreehandPreviewSegments final {
    std::vector<PointPx> stable_points = {};
    std::vector<PointPx> tail_points = {};
};

[[nodiscard]] std::optional<FreehandSmoothingMode>
Freehand_smoothing_mode_from_token(std::string_view token) noexcept;
[[nodiscard]] std::string_view
Freehand_smoothing_mode_token(FreehandSmoothingMode mode) noexcept;

[[nodiscard]] std::vector<PointPx>
Smooth_freehand_points(std::span<const PointPx> points, FreehandSmoothingMode mode,
                       int32_t stroke_width_px);

[[nodiscard]] FreehandPreviewPlan
Build_freehand_preview_plan(std::span<const PointPx> points, FreehandSmoothingMode mode,
                            int32_t stroke_width_px);

[[nodiscard]] FreehandPreviewSegments
Build_freehand_preview_segments(std::span<const PointPx> points,
                                FreehandSmoothingMode mode, int32_t stroke_width_px);

// Smooths a growing stroke at a cost per appended point that does not grow with the
// stroke. At every point count, Final_points() followed by Provisional_points()
// equals Smooth_freehand_points() of every point appended so far. Final points never
// change once returned; only the provisional end is recomputed.
class FreehandIncrementalSmoother final {
  public:
    void Reset(FreehandSmoothingMode mode, int32_t stroke_width_px);
    void Append(std::span<const PointPx> points);

    [[nodiscard]] std::span<const PointPx> Final_points() const noexcept {
        return final_;
    }
    [[nodiscard]] std::span<const PointPx> Provisional_points() const noexcept {
        return provisional_;
    }
    [[nodiscard]] size_t Raw_point_count() const noexcept { return raw_count_; }
    [[nodiscard]] FreehandSmoothingMode Mode() const noexcept { return mode_; }
    [[nodiscard]] int32_t Stroke_width_px() const noexcept { return stroke_width_px_; }

  private:
    void Append_point(PointPx point);
    void Update_provisional();

    FreehandSmoothingMode mode_ = FreehandSmoothingMode::Off;
    int32_t stroke_width_px_ = 0;
    size_t raw_count_ = 0;
    std::vector<PointPx> raw_head_ = {}; // first two raw points
    std::vector<PointPx> deduplicated_ = {};
    std::vector<PointPx> decimated_ = {}; // fixed decimated points
    size_t last_kept_index_ = 0;          // into deduplicated_
    size_t next_decimation_index_ = 1;    // into deduplicated_
    size_t next_segment_index_ = 0;       // into decimated_
    std::vector<PointPx> final_ = {};
    std::vector<PointPx> provisional_ = {};
};

} // namespace greenflame::core
