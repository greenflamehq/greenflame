#include "greenflame_core/frame_stats.h"

namespace greenflame::core {

namespace {

constexpr double kPercentScale = 100.0;
constexpr double kP50 = 50.0;
constexpr double kP90 = 90.0;
constexpr double kP95 = 95.0;
constexpr double kP99 = 99.0;

// Nearest-rank: the smallest value with at least p% of samples at or below it.
double Nearest_rank(std::span<double const> sorted, double percentile) noexcept {
    double const rank =
        std::ceil(percentile / kPercentScale * static_cast<double>(sorted.size()));
    size_t index = rank < 1.0 ? 0 : static_cast<size_t>(rank) - 1;
    index = std::min(index, sorted.size() - 1);
    return sorted[index];
}

double Percent_within(std::span<double const> sorted, double budget_ms) noexcept {
    auto const within = std::upper_bound(sorted.begin(), sorted.end(), budget_ms);
    return kPercentScale * static_cast<double>(within - sorted.begin()) /
           static_cast<double>(sorted.size());
}

} // namespace

FrameTimeSummary Summarize_frame_times(std::span<double const> frame_ms) {
    FrameTimeSummary summary{};
    if (frame_ms.empty()) {
        return summary;
    }
    std::vector<double> sorted(frame_ms.begin(), frame_ms.end());
    std::sort(sorted.begin(), sorted.end());
    summary.count = sorted.size();
    summary.p50_ms = Nearest_rank(sorted, kP50);
    summary.p90_ms = Nearest_rank(sorted, kP90);
    summary.p95_ms = Nearest_rank(sorted, kP95);
    summary.p99_ms = Nearest_rank(sorted, kP99);
    summary.max_ms = sorted.back();
    summary.percent_within_30fps = Percent_within(sorted, kFrameBudget30Ms);
    summary.percent_within_60fps = Percent_within(sorted, kFrameBudget60Ms);
    return summary;
}

FrameRateVerdict Judge_frame_times(FrameTimeSummary const &summary) noexcept {
    if (summary.count == 0) {
        return FrameRateVerdict::Fail;
    }
    if (summary.p95_ms <= kFrameBudget60Ms) {
        return FrameRateVerdict::Pass60;
    }
    if (summary.p95_ms <= kFrameBudget30Ms) {
        return FrameRateVerdict::Pass30;
    }
    return FrameRateVerdict::Fail;
}

FrameSpikeKind Classify_frame_spike(FrameIntervalParts const &parts) noexcept {
    if (parts.input_to_end_ms <= kFrameSpikeMs) {
        return FrameSpikeKind::Idle;
    }
    double const late_ms = parts.input_to_end_ms - parts.wait_ms - parts.paint_ms;
    if (parts.wait_ms >= parts.paint_ms && parts.wait_ms >= late_ms) {
        return FrameSpikeKind::Wait;
    }
    if (parts.paint_ms >= late_ms) {
        return FrameSpikeKind::Paint;
    }
    return FrameSpikeKind::Late;
}

std::string_view Frame_spike_kind_label(FrameSpikeKind kind) noexcept {
    switch (kind) {
    case FrameSpikeKind::Idle:
        return "idle";
    case FrameSpikeKind::Wait:
        return "wait";
    case FrameSpikeKind::Paint:
        return "paint";
    case FrameSpikeKind::Late:
        break;
    }
    return "late";
}

FrameSpikeSummary
Summarize_frame_spikes(std::span<FrameIntervalParts const> frames) noexcept {
    FrameSpikeSummary summary{};
    for (FrameIntervalParts const &parts : frames) {
        if (parts.interval_ms <= kFrameSpikeMs) {
            continue;
        }
        FrameSpikeCount &slot =
            summary.by_kind[static_cast<size_t>(Classify_frame_spike(parts))];
        ++slot.count;
        slot.max_ms = std::max(slot.max_ms, parts.interval_ms);
    }
    return summary;
}

std::string_view Frame_rate_verdict_label(FrameRateVerdict verdict) noexcept {
    switch (verdict) {
    case FrameRateVerdict::Pass60:
        return "PASS-60";
    case FrameRateVerdict::Pass30:
        return "PASS-30";
    case FrameRateVerdict::Fail:
        break;
    }
    return "FAIL";
}

} // namespace greenflame::core
