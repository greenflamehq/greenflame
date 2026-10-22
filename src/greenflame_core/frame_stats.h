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

// A frame interval longer than this is a spike worth explaining.
inline constexpr double kFrameSpikeMs = 50.0;
// A part of a spike (frame-latency wait, paint) longer than this explains it.
inline constexpr double kFrameSpikePartMs = 40.0;

// One painted frame of a gesture, measured from the frame before it.
struct FrameIntervalParts final {
    double interval_ms = 0.0;     // previous frame's end to this frame's end
    double input_to_end_ms = 0.0; // first input since the previous frame, to the end
    double wait_ms = 0.0;         // frame-latency wait inside the paint
    double paint_ms = 0.0;        // the rest of the paint
};

// Why a long interval was long.
enum class FrameSpikeKind : uint8_t {
    Idle,  // no input for most of it: the frame followed its input promptly
    Wait,  // the frame-latency wait (DWM, present, the GPU) took most of it
    Paint, // drawing the frame took most of it
    Late,  // input arrived, but the paint started late (message loop)
};
inline constexpr size_t kFrameSpikeKindCount = 4;

[[nodiscard]] FrameSpikeKind
Classify_frame_spike(FrameIntervalParts const &parts) noexcept;

[[nodiscard]] std::string_view Frame_spike_kind_label(FrameSpikeKind kind) noexcept;

struct FrameSpikeCount final {
    size_t count = 0;
    double max_ms = 0.0;
};

// Count and longest interval per kind, indexed by FrameSpikeKind.
struct FrameSpikeSummary final {
    std::array<FrameSpikeCount, kFrameSpikeKindCount> by_kind = {};
};

// Sorts the intervals longer than kFrameSpikeMs by kind.
[[nodiscard]] FrameSpikeSummary
Summarize_frame_spikes(std::span<FrameIntervalParts const> frames) noexcept;

} // namespace greenflame::core
