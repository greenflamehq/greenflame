#pragma once

namespace greenflame::core {

// Frame budgets for the overlay frame-rate targets.
inline constexpr double kFrameBudget30Ms = 1000.0 / 30.0;
inline constexpr double kFrameBudget60Ms = 1000.0 / 60.0;

struct FrameTimeSummary final {
    size_t count = 0;
    double p50_ms = 0.0;
    double p90_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    double max_ms = 0.0;
    // Share of frames at or under each budget, 0..100.
    double percent_within_30fps = 0.0;
    double percent_within_60fps = 0.0;
};

enum class FrameRateVerdict : uint8_t {
    Fail,
    Pass30,
    Pass60,
};

// Nearest-rank percentiles of the given frame times. Input order does not matter.
// Empty input gives a zeroed summary with count 0.
[[nodiscard]] FrameTimeSummary Summarize_frame_times(std::span<double const> frame_ms);

// Verdict on p95: Pass60 at or under 16.7 ms, Pass30 at or under 33.3 ms, else Fail.
// An empty summary is a Fail: no frames prove nothing.
[[nodiscard]] FrameRateVerdict
Judge_frame_times(FrameTimeSummary const &summary) noexcept;

[[nodiscard]] std::string_view
Frame_rate_verdict_label(FrameRateVerdict verdict) noexcept;

} // namespace greenflame::core
