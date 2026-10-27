// greenflame_render_bench: drives the real overlay paint code (Paint_d2d_frame)
// with synthetic input and reports per-frame cost. See docs/testing.md,
// "Overlay performance".
//
// B1 (default): offscreen render bench. CPU, GPU and frame time per frame on a
//     chosen monitor layout and adapter. No window, no present. With
//     --split-monitors each frame goes into one offscreen target per monitor, as the
//     per-monitor overlay draws it.
// B2 (--probe-present): present floor. Real topmost windows on the real
//     monitors, same swap chain as the app, frame interval per loop: span,
//     per-monitor-shared (one device) and per-monitor (one device per GPU).
//
// Known gaps against OverlayWindow::On_paint (keep this list honest):
// - no toolbar buttons or glyphs, so frames with a selection are slightly cheaper
//   than in the app;
// - no Alt-free snapping of the crosshair cursor (CPU only, not paint);
// - no selection wheel or text drafts; committed annotations only with --annotated,
//   and obfuscate only as the obfuscate scenario's draft and in move and commit;
// - move fills no selected-annotation marquee or handles, and its text and bubble
//   annotations carry stand-in rasters (no text layout engine here).

#include "greenflame/win/d2d_overlay_resources.h"
#include "greenflame/win/d2d_paint.h"
#include "greenflame/win/obfuscate_preview.h"
#include "greenflame/win/overlay_top_layer.h"
#include "greenflame_core/bmp.h"
#include "greenflame_core/frame_stats.h"
#include "greenflame_core/overlay_controller.h"
#include "greenflame_core/selection_wheel.h"

namespace greenflame::bench {

namespace {

using Microsoft::WRL::ComPtr;

// Exit codes of this tool only (it is not the app; see ProcessExitCode there).
enum class BenchExit : int {
    Ok = 0,
    BadArgs = 2,
    DeviceFailed = 3,
    PaintFailed = 4,
    PixelsDiffer = 5,
    FileError = 6,
};

constexpr int kWarmupFrames = 5;
constexpr int kDefaultFrames = 300;
constexpr int kDefaultProbeFrames = 120;
constexpr int kDefaultStepPx = 8;
constexpr double kProbeTimeLimitMs = 8000.0;
constexpr int kFreehandBuckets = 5;
constexpr int kSelectStartPx = 64;
constexpr int kSweepMarginPx = 64;
constexpr int kFreehandEdgeInsetPx = 128;
constexpr double kFreehandWavePerFrame = 0.05;
constexpr int kFreehandAmplitudeDivisor = 5;
constexpr double kFreehandRowStepPx = 40.0;
constexpr int32_t kBrushWidthPx = 11;
constexpr int32_t kHighlighterWidthPx = 20;
constexpr int32_t kLineWidthPx = 4;
constexpr COLORREF kLineColor = RGB(0, 128, 255);
constexpr COLORREF kBrushColor = RGB(255, 0, 0);
constexpr COLORREF kHighlighterColor = RGB(255, 255, 0);
constexpr uint32_t kNoiseSeed = 0x12345678u;
constexpr uint32_t kLiftedNoiseSeed = 0x9E3779B9u; // --lifted: the window's own pixels
// --lifted: capture pixels around the shown part, so the source rect is offset.
constexpr int32_t kLiftedCaptureMarginPx = 32;
constexpr uint32_t kLcgMultiplier = 1664525u;
constexpr uint32_t kLcgIncrement = 1013904223u;
constexpr uint32_t kNoiseShift = 8u;
constexpr uint32_t kOpaqueAlpha = 0xFF000000u;
constexpr double kMsPerSecond = 1000.0;
constexpr double kMegapixel = 1e6;
constexpr int kBytesPerPixel = 4;
constexpr float kExtraPassOpacity = 0.5f;
// Frame-latency wait bound passed to Paint_d2d_frame. Offscreen targets have no
// waitable; the probe's swap chains wait about one vsync.
constexpr DWORD kFrameWaitTimeoutMs = 1000;
// obfuscate: the draft's size; it slides across the desktop's middle row.
constexpr int32_t kObfuscateDraftWidthPx = 512;
constexpr int32_t kObfuscateDraftHeightPx = 256;
constexpr int32_t kObfuscateBlockPx = 16;
constexpr uint8_t kObfuscateDarkGrey = 64;
constexpr uint8_t kObfuscateLightGrey = 192;
constexpr size_t kMaxIntDigits = 9;
constexpr int kDecimalBase = 10;

// Monitor sizes and placements of the built-in layouts.
constexpr int32_t kLaptopWidthPx = 1920;
constexpr int32_t kLaptopHeightPx = 1200;
constexpr int32_t kQhdWidthPx = 2560;
constexpr int32_t kQhdHeightPx = 1440;
constexpr int32_t kUhdWidthPx = 3840;
constexpr int32_t kUhdHeightPx = 2160;
constexpr int32_t kFhdWidthPx = 1920;
constexpr int32_t kFhdHeightPx = 1080;
// Laptop top edge on Jocelyn's desk (measured), and centred beside the 4K panels.
constexpr int32_t kJocelynDeskLaptopTopPx = 137;
constexpr int32_t kUhdDeskLaptopTopPx = (kUhdHeightPx - kLaptopHeightPx) / 2;

template <typename... Args>
void Print(std::format_string<Args...> format, Args &&...args) {
    std::cout << std::format(format, std::forward<Args>(args)...);
}

std::string Narrow(std::wstring_view text) { return winrt::to_string(text); }

// ---------------------------------------------------------------------------
// Layouts
// ---------------------------------------------------------------------------

struct Layout final {
    std::string name = {};
    std::vector<core::RectPx> monitors_screen = {};
};

core::RectPx Monitor(int32_t left, int32_t top, int32_t width, int32_t height) {
    return core::RectPx::From_ltrb(left, top, left + width, top + height);
}

BOOL CALLBACK Collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM user) noexcept {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) != FALSE) {
        auto *const out = reinterpret_cast<std::vector<core::RectPx> *>(user);
        out->push_back(core::RectPx::From_ltrb(info.rcMonitor.left, info.rcMonitor.top,
                                               info.rcMonitor.right,
                                               info.rcMonitor.bottom));
    }
    return TRUE;
}

std::vector<core::RectPx> Current_monitors() {
    std::vector<core::RectPx> monitors;
    (void)EnumDisplayMonitors(nullptr, nullptr, Collect_monitor,
                              reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

std::optional<Layout> Find_layout(std::string_view name) {
    // Jocelyn's desk: laptop 1920x1200 left of two 4K panels run at 2560x1440.
    if (name == "jocelyn-desk") {
        return Layout{std::string(name),
                      {Monitor(-kLaptopWidthPx, kJocelynDeskLaptopTopPx, kLaptopWidthPx,
                               kLaptopHeightPx),
                       Monitor(0, 0, kQhdWidthPx, kQhdHeightPx),
                       Monitor(kQhdWidthPx, 0, kQhdWidthPx, kQhdHeightPx)}};
    }
    // Same desk with the 4K panels at native resolution.
    if (name == "4k-desk") {
        return Layout{std::string(name),
                      {Monitor(-kLaptopWidthPx, kUhdDeskLaptopTopPx, kLaptopWidthPx,
                               kLaptopHeightPx),
                       Monitor(0, 0, kUhdWidthPx, kUhdHeightPx),
                       Monitor(kUhdWidthPx, 0, kUhdWidthPx, kUhdHeightPx)}};
    }
    if (name == "single-1080") {
        return Layout{std::string(name), {Monitor(0, 0, kFhdWidthPx, kFhdHeightPx)}};
    }
    if (name == "current") {
        Layout layout{std::string(name), Current_monitors()};
        if (layout.monitors_screen.empty()) {
            return std::nullopt;
        }
        return layout;
    }
    return std::nullopt;
}

core::RectPx Union_of(std::span<const core::RectPx> rects) {
    core::RectPx out = rects.front();
    for (core::RectPx const &r : rects) {
        out = core::RectPx::From_ltrb(
            std::min(out.left, r.left), std::min(out.top, r.top),
            std::max(out.right, r.right), std::max(out.bottom, r.bottom));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

enum class AdapterKind : uint8_t { Default, Index, Warp };

struct AdapterChoice final {
    AdapterKind kind = AdapterKind::Default;
    UINT index = 0;
};

struct Args final {
    std::string layout = "jocelyn-desk";
    AdapterChoice adapter = {};
    AdapterChoice shared_adapter = {}; // --shared-adapter: the probe's shared device
    std::string scenario = "all";
    bool smooth = true;
    int step_px = kDefaultStepPx;
    int points_per_frame = 1;
    int opacity_percent = core::StrokeStyle::kMaxOpacityPercent;
    bool opacity_given = false;
    int straighten_at = -1;
    bool diagonal = false;     // --diagonal
    int dab_points = 0;        // --dab N
    bool dab_no_draft = false; // --dab-no-draft
    int frames = kDefaultFrames;
    bool frames_given = false;
    int repeat = 1;
    int passes = 1;
    std::string csv_path = {};
    int dump_frame = -1;
    std::string dump_path = {};
    std::string diff_a = {};
    std::string diff_b = {};
    int tolerance = 0;
    int max_pixels = 0; // --max-pixels: pixels allowed over the tolerance
    bool list_adapters = false;
    bool probe_present = false;
    bool annotated = false;
    bool lifted = false;               // --lifted
    bool lifted_as_screenshot = false; // --lifted-as-screenshot
    bool arrow = false;                // --arrow
    bool flat = false;                 // --flat
    bool line_dynamic = false;         // --line-dynamic
    bool select_dynamic = false;       // --select-dynamic
    bool stale_frozen = false;         // --stale-frozen
    bool resize = false;               // --resize
    int stroke_points = 64;         // --stroke-points N: points per --annotated stroke
    int obfuscations = 0;           // --obfuscations K
    bool obfuscate_below = false;   // --obfuscate-below
    bool multi = false;             // --multi
    std::string edit_kind = "rect"; // --edit-kind
    bool edit_dynamic = false;      // --edit-dynamic
    bool verify_edit = false;       // --verify-edit
    bool click_first = false;       // --click-first
    bool click_moved = false;       // --click-moved
    bool raster_load = false;       // --raster-load
    bool reverse = false;           // --reverse: freehand runs right to left
    bool split_monitors = false;    // --split-monitors: one target per monitor
    bool mask_monitors = false;     // --mask-monitors: diff only monitor pixels
};

void Print_usage() {
    Print(
        "usage: greenflame_render_bench [options]\n"
        "  --layout jocelyn-desk|4k-desk|single-1080|current   (default jocelyn-desk)\n"
        "  --adapter default|N|warp     D3D adapter (default: what the app uses)\n"
        "  --list-adapters              print DXGI adapters and exit\n"
        "  --scenario hover|select|brush|highlighter|line|obfuscate|steady|all   "
        "(default all), or move|commit (not in all)\n"
        "  --smooth on|off              freehand smoothing (default on)\n"
        "  --step PX                    px between freehand points (default 8)\n"
        "  --points-per-frame N         freehand points added per frame (default 1)\n"
        "  --opacity N                  stroke opacity percent, 0-100 (default 100 "
        "brush, 33 highlighter)\n"
        "  --straighten-at K            highlighter: from frame K the stroke is its "
        "first and newest point\n"
        "  --diagonal                   freehand: points run from the selection's "
        "top-left to its bottom-right instead of a wave\n"
        "  --reverse                    freehand: the stroke runs right to left\n"
        "  --split-monitors             draw each frame into one offscreen target per "
        "monitor (the per-monitor overlay); dumps are stitched to the desktop\n"
        "  --dab N [--dab-no-draft]     freehand: first draw and commit an N-point "
        "stroke at the same start, N at most --points-per-frame (--dab-no-draft: "
        "commit it without drawing)\n"
        "  --annotated                  inset selection plus committed brush and "
        "highlighter strokes\n"
        "  --edit-kind K                move: the moved annotation, rect (default), "
        "filled-rect, ellipse, filled-ellipse, line, arrow, brush, highlighter, "
        "obfuscate, text or bubble\n"
        "  --multi                      move: also move an ellipse below it (a "
        "multi-selection body move)\n"
        "  --obfuscations K             move, commit: K obfuscates across the "
        "selection's "
        "middle row, above the moved annotation\n"
        "  --obfuscate-below            move: the obfuscates sit below the moved "
        "annotation\n"
        "  --stroke-points N            points per --annotated stroke (default 64)\n"
        "  --edit-dynamic               move: edit frames take the full annotation "
        "rebuild and the dynamic path\n"
        "  --verify-edit                move: draw every frame again with "
        "--edit-dynamic and fail unless the two match at tolerance 0, or unless the "
        "edit fast paths ran (none of the changed-area path with --lifted or "
        "--stale-frozen)\n"
        "  --click-first                move: first a click on the annotation that "
        "does not move it, an idle frame, and a selection drag and commit, before "
        "the drag\n"
        "  --raster-load                move: 3 obfuscates of 1600x400 and 3 texts of "
        "1600x200 below the moved annotation, off its path (large rasters)\n"
        "  --click-moved                with --click-first: the click's painted frame "
        "has the annotation moved (it is back at the release, which does nothing); "
        "fail unless the steady frame after the selection commit matches a fresh "
        "rebuild\n"
        "  --resize                     move: drag the right edge instead of the body; "
        "select: resize a committed selection, inset "
        "by an eighth of the desktop, by a handle\n"
        "  --arrow                      line: draw an arrow head\n"
        "  --flat                       line: run left to right along the selection's "
        "middle row\n"
        "  --line-dynamic               line: skip the frozen-frame path (the frozen "
        "frame reads stale every frame); frames must match without it\n"
        "  --select-dynamic             select: skip the selection-gesture path (the "
        "frozen frame reads stale every frame); frames must match without it\n"
        "  --stale-frozen               build the frozen frame for no selection before "
        "the run, as a drag leaves it (move: from the annotations moved halfway, as "
        "mid-edit); frames must match without it\n"
        "  --lifted                     with --annotated: a lifted window capture with "
        "its own pixels over the selection\n"
        "  --lifted-as-screenshot       with --annotated: those pixels in the "
        "screenshot instead; frames must match --lifted\n"
        "  --frames N                   measured frames per run (default 300)\n"
        "  --repeat N                   runs per scenario (default 1)\n"
        "  --csv FILE                   per-frame rows\n"
        "  --dump-frame K FILE.bmp      write frame K (run 1) as BMP; with 'all', from "
        "select; K = --frames: the freehand release frame\n"
        "  --diff A.bmp B.bmp [--tolerance T] [--max-pixels N] [--mask-monitors]   "
        "compare two dumps; fails when more than N pixels (default 0) differ by more "
        "than T; --mask-monitors compares only pixels on --layout's monitors\n"
        "  --probe-present [--passes N] [--frames N]   present floor on real "
        "monitors\n"
        "  --shared-adapter default|N   the one device of the probe's "
        "per-monitor-shared arrangement (default: what the app uses)\n");
}

// Non-negative decimal integer, or nullopt.
std::optional<int> Parse_int(std::string_view text) {
    if (text.empty() || text.size() > kMaxIntDigits) {
        return std::nullopt;
    }
    int value = 0;
    for (char const c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * kDecimalBase + (c - '0');
    }
    return value;
}

// "default", "warp" (when allowed) or a DXGI adapter index; nullopt otherwise.
std::optional<AdapterChoice> Parse_adapter_choice(std::string_view text,
                                                  bool allow_warp) {
    if (text == "default") {
        return AdapterChoice{};
    }
    if (allow_warp && text == "warp") {
        return AdapterChoice{.kind = AdapterKind::Warp};
    }
    std::optional<int> const index = Parse_int(text);
    if (!index) {
        return std::nullopt;
    }
    return AdapterChoice{.kind = AdapterKind::Index,
                         .index = static_cast<UINT>(*index)};
}

std::optional<Args> Parse_args(std::span<char *const> argv) {
    Args args;
    std::span<char *const> const options = argv.empty() ? argv : argv.subspan(1);
    std::vector<std::string_view> const tokens(options.begin(), options.end());
    size_t i = 0;
    auto next = [&]() -> std::optional<std::string_view> {
        if (i + 1 >= tokens.size()) {
            return std::nullopt;
        }
        ++i;
        return tokens[i];
    };
    auto next_int = [&](int min_value) -> std::optional<int> {
        std::optional<std::string_view> const text = next();
        if (!text) {
            return std::nullopt;
        }
        std::optional<int> const value = Parse_int(*text);
        if (!value || *value < min_value) {
            return std::nullopt;
        }
        return value;
    };
    for (; i < tokens.size(); ++i) {
        std::string_view const key = tokens[i];
        if (key == "--layout") {
            std::optional<std::string_view> const v = next();
            if (!v) return std::nullopt;
            args.layout = std::string(*v);
        } else if (key == "--adapter") {
            std::optional<std::string_view> const v = next();
            if (!v) return std::nullopt;
            std::optional<AdapterChoice> const choice =
                Parse_adapter_choice(*v, /*allow_warp=*/true);
            if (!choice) return std::nullopt;
            args.adapter = *choice;
        } else if (key == "--shared-adapter") {
            std::optional<std::string_view> const v = next();
            if (!v) return std::nullopt;
            // No WARP: it cannot present to a real monitor at a useful rate.
            std::optional<AdapterChoice> const choice =
                Parse_adapter_choice(*v, /*allow_warp=*/false);
            if (!choice) return std::nullopt;
            args.shared_adapter = *choice;
        } else if (key == "--scenario") {
            std::optional<std::string_view> const v = next();
            if (!v) return std::nullopt;
            args.scenario = std::string(*v);
        } else if (key == "--smooth") {
            std::optional<std::string_view> const v = next();
            if (!v || (*v != "on" && *v != "off")) return std::nullopt;
            args.smooth = *v == "on";
        } else if (key == "--step") {
            std::optional<int> const v = next_int(1);
            if (!v) return std::nullopt;
            args.step_px = *v;
        } else if (key == "--points-per-frame") {
            std::optional<int> const v = next_int(1);
            if (!v) return std::nullopt;
            args.points_per_frame = *v;
        } else if (key == "--opacity") {
            std::optional<int> const v =
                next_int(core::StrokeStyle::kMinOpacityPercent);
            if (!v || *v > core::StrokeStyle::kMaxOpacityPercent) return std::nullopt;
            args.opacity_percent = *v;
            args.opacity_given = true;
        } else if (key == "--dab") {
            std::optional<int> const v = next_int(1);
            if (!v) return std::nullopt;
            args.dab_points = *v;
        } else if (key == "--dab-no-draft") {
            args.dab_no_draft = true;
        } else if (key == "--straighten-at") {
            std::optional<int> const v = next_int(0);
            if (!v) return std::nullopt;
            args.straighten_at = *v;
        } else if (key == "--diagonal") {
            args.diagonal = true;
        } else if (key == "--frames") {
            std::optional<int> const v = next_int(1);
            if (!v) return std::nullopt;
            args.frames = *v;
            args.frames_given = true;
        } else if (key == "--repeat") {
            std::optional<int> const v = next_int(1);
            if (!v) return std::nullopt;
            args.repeat = *v;
        } else if (key == "--passes") {
            std::optional<int> const v = next_int(1);
            if (!v) return std::nullopt;
            args.passes = *v;
        } else if (key == "--csv") {
            std::optional<std::string_view> const v = next();
            if (!v) return std::nullopt;
            args.csv_path = std::string(*v);
        } else if (key == "--dump-frame") {
            std::optional<int> const k = next_int(0);
            std::optional<std::string_view> const path = next();
            if (!k || !path) return std::nullopt;
            args.dump_frame = *k;
            args.dump_path = std::string(*path);
        } else if (key == "--diff") {
            std::optional<std::string_view> const a = next();
            std::optional<std::string_view> const b = next();
            if (!a || !b) return std::nullopt;
            args.diff_a = std::string(*a);
            args.diff_b = std::string(*b);
        } else if (key == "--tolerance") {
            std::optional<int> const v = next_int(0);
            if (!v) return std::nullopt;
            args.tolerance = *v;
        } else if (key == "--max-pixels") {
            std::optional<int> const v = next_int(0);
            if (!v) return std::nullopt;
            args.max_pixels = *v;
        } else if (key == "--annotated") {
            args.annotated = true;
        } else if (key == "--arrow") {
            args.arrow = true;
        } else if (key == "--flat") {
            args.flat = true;
        } else if (key == "--reverse") {
            args.reverse = true;
        } else if (key == "--split-monitors") {
            args.split_monitors = true;
        } else if (key == "--mask-monitors") {
            args.mask_monitors = true;
        } else if (key == "--line-dynamic") {
            args.line_dynamic = true;
        } else if (key == "--select-dynamic") {
            args.select_dynamic = true;
        } else if (key == "--stale-frozen") {
            args.stale_frozen = true;
        } else if (key == "--resize") {
            args.resize = true;
        } else if (key == "--edit-dynamic") {
            args.edit_dynamic = true;
        } else if (key == "--verify-edit") {
            args.verify_edit = true;
        } else if (key == "--click-first") {
            args.click_first = true;
        } else if (key == "--click-moved") {
            args.click_moved = true;
        } else if (key == "--raster-load") {
            args.raster_load = true;
        } else if (key == "--multi") {
            args.multi = true;
        } else if (key == "--obfuscate-below") {
            args.obfuscate_below = true;
        } else if (key == "--obfuscations") {
            std::optional<int> const v = next_int(0);
            if (!v) return std::nullopt;
            args.obfuscations = *v;
        } else if (key == "--stroke-points") {
            std::optional<int> const v = next_int(2);
            if (!v) return std::nullopt;
            args.stroke_points = *v;
        } else if (key == "--edit-kind") {
            std::optional<std::string_view> const v = next();
            if (!v) return std::nullopt;
            args.edit_kind = std::string(*v);
        } else if (key == "--lifted") {
            args.lifted = true;
        } else if (key == "--lifted-as-screenshot") {
            args.lifted_as_screenshot = true;
        } else if (key == "--list-adapters") {
            args.list_adapters = true;
        } else if (key == "--probe-present") {
            args.probe_present = true;
        } else {
            Print("unknown option: {}\n", key);
            return std::nullopt;
        }
    }
    if (args.probe_present && !args.frames_given) {
        args.frames = kDefaultProbeFrames;
    }
    return args;
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

double Qpc_ms(LARGE_INTEGER from, LARGE_INTEGER to) {
    static LARGE_INTEGER const kQpcFrequency = [] {
        LARGE_INTEGER f{};
        (void)QueryPerformanceFrequency(&f);
        return f;
    }();
    return static_cast<double>(to.QuadPart - from.QuadPart) * kMsPerSecond /
           static_cast<double>(kQpcFrequency.QuadPart);
}

LARGE_INTEGER Now() {
    LARGE_INTEGER t{};
    (void)QueryPerformanceCounter(&t);
    return t;
}

double Median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// ---------------------------------------------------------------------------
// Device and resources
// ---------------------------------------------------------------------------

// Deterministic noise, so GPU colour compression cannot make blits cheap. Inside
// `paste`, the noise of a paste-sized bitmap made from `paste_seed` instead.
HBITMAP Make_noise_bitmap(int width, int height, uint32_t seed, core::RectPx paste = {},
                          uint32_t paste_seed = 0) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP const bitmap =
        CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        return nullptr;
    }
    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage-in-container")
    std::span<uint32_t> const pixels(static_cast<uint32_t *>(bits),
                                     static_cast<size_t>(width) *
                                         static_cast<size_t>(height));
    CLANG_WARN_IGNORE_POP()
    uint32_t state = seed;
    uint32_t paste_state = paste_seed;
    for (size_t i = 0; i < pixels.size(); ++i) {
        state = state * kLcgMultiplier + kLcgIncrement;
        core::PointPx const at = {static_cast<int32_t>(i % static_cast<size_t>(width)),
                                  static_cast<int32_t>(i / static_cast<size_t>(width))};
        uint32_t value = state;
        if (paste.Contains(at)) {
            paste_state = paste_state * kLcgMultiplier + kLcgIncrement;
            value = paste_state;
        }
        pixels[i] = (value >> kNoiseShift) | kOpaqueAlpha;
    }
    return bitmap;
}

// The selection --annotated uses: inset by an eighth of the desktop on each side.
core::RectPx Inset_selection(int width, int height) {
    return core::RectPx::From_ltrb(width / 8, height / 8, width - width / 8,
                                   height - height / 8);
}

// --lifted: a window capture with its own pixels, shown over the selection, as a
// Ctrl-click window selection lifts it. --lifted-as-screenshot: no lifted window, the
// screenshot itself carries those pixels; its frames must match --lifted.
bool Prepare_lifted(D2DOverlayResources &res, int width, int height, Args const &args) {
    core::RectPx const sel = Inset_selection(width, height);
    GdiCaptureResult capture{};
    // The shown part sits kLiftedCaptureMarginPx inside the capture, which has other
    // pixels around it.
    core::RectPx const source = core::RectPx::From_ltrb(
        kLiftedCaptureMarginPx, kLiftedCaptureMarginPx,
        kLiftedCaptureMarginPx + sel.Width(), kLiftedCaptureMarginPx + sel.Height());
    capture.width = args.lifted ? sel.Width() + 2 * kLiftedCaptureMarginPx : width;
    capture.height = args.lifted ? sel.Height() + 2 * kLiftedCaptureMarginPx : height;
    capture.bitmap =
        args.lifted
            ? Make_noise_bitmap(capture.width, capture.height, ~kNoiseSeed, source,
                                kLiftedNoiseSeed)
            : Make_noise_bitmap(width, height, kNoiseSeed, sel, kLiftedNoiseSeed);
    if (capture.bitmap == nullptr) {
        return false;
    }
    bool const ok = args.lifted ? res.Upload_lifted_window_capture(capture)
                                : res.Upload_screenshot(capture);
    (void)DeleteObject(capture.bitmap);
    if (ok && args.lifted) {
        res.Set_lifted_view(LiftedWindowView{.bitmap = res.lifted_window_capture.Get(),
                                             .dest_rect = sel,
                                             .source_rect = source});
    }
    return ok;
}

// Screenshot, shared resources and cache targets, as OverlayWindow sets them up.
bool Prepare_resources(D2DOverlayResources &res, int width, int height) {
    GdiCaptureResult capture{};
    capture.bitmap = Make_noise_bitmap(width, height, kNoiseSeed);
    capture.width = width;
    capture.height = height;
    if (capture.bitmap == nullptr) {
        return false;
    }
    bool const ok = res.Upload_screenshot(capture) && res.Create_shared_resources() &&
                    res.Create_cache_targets(width, height);
    (void)DeleteObject(capture.bitmap);
    if (ok) {
        Rebuild_annotations_bitmap(res, {});
    }
    return ok;
}

std::wstring Adapter_name(ID3D11Device *device) {
    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf()))) ||
        FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf())) ||
        FAILED(adapter->GetDesc(&desc))) {
        return L"?";
    }
    return std::wstring(desc.Description);
}

int List_adapters() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return static_cast<int>(BenchExit::DeviceFailed);
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) !=
                     DXGI_ERROR_NOT_FOUND;
         ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        (void)adapter->GetDesc1(&desc);
        Print("{}: {}\n", i, Narrow(desc.Description));
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) !=
                         DXGI_ERROR_NOT_FOUND;
             ++o) {
            DXGI_OUTPUT_DESC od{};
            (void)output->GetDesc(&od);
            Print("     output {} ({},{})-({},{})\n", Narrow(od.DeviceName),
                  od.DesktopCoordinates.left, od.DesktopCoordinates.top,
                  od.DesktopCoordinates.right, od.DesktopCoordinates.bottom);
        }
    }
    return static_cast<int>(BenchExit::Ok);
}

bool Create_bench_device(D2DOverlayResources &res, AdapterChoice const &choice) {
    if (!res.Initialize_factory()) {
        return false;
    }
    switch (choice.kind) {
    case AdapterKind::Default:
        return res.Create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE);
    case AdapterKind::Warp:
        return res.Create_device(nullptr, D3D_DRIVER_TYPE_WARP);
    case AdapterKind::Index:
        break;
    }
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))) ||
        factory->EnumAdapters1(choice.index, adapter.GetAddressOf()) != S_OK) {
        Print("no adapter {} (see --list-adapters)\n", choice.index);
        return false;
    }
    return res.Create_device(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN);
}

// ---------------------------------------------------------------------------
// Pixel dump and diff
// ---------------------------------------------------------------------------

bool Write_file(std::string const &path, std::span<const uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<char const *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::optional<std::vector<uint8_t>> Read_file(std::string const &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }
    // One read: a desktop-sized dump is tens of MB, too slow byte by byte in debug.
    std::streamoff const size = in.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
    if (!in) {
        return std::nullopt;
    }
    return bytes;
}

// Copies one target into packed, a desktop-sized bottom-up BGRA buffer (BMP row
// order, tightly packed), at the target's rect.
bool Read_target_into(D2DOverlayResources &res, D2DSurfaceTarget const &target,
                      int width, int height, std::span<uint8_t> packed) {
    core::RectPx const rect = target.rect_client;
    if (rect.left < 0 || rect.top < 0 || rect.right > width || rect.bottom > height) {
        return false;
    }
    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    bp.dpiX = res.Target_dpi();
    bp.dpiY = res.Target_dpi();
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> readback;
    D2D1_SIZE_U const size = D2D1::SizeU(static_cast<UINT32>(rect.Width()),
                                         static_cast<UINT32>(rect.Height()));
    if (FAILED(
            res.hwnd_rt->CreateBitmap(size, nullptr, 0, bp, readback.GetAddressOf())) ||
        FAILED(readback->CopyFromBitmap(nullptr, target.bitmap.Get(), nullptr))) {
        return false;
    }
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return false;
    }
    size_t const desktop_row_bytes = static_cast<size_t>(width) * kBytesPerPixel;
    size_t const target_row_bytes = static_cast<size_t>(rect.Width()) * kBytesPerPixel;
    size_t const target_rows = static_cast<size_t>(rect.Height());
    size_t const rows = static_cast<size_t>(height);
    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage-in-container")
    std::span<const uint8_t> const source(
        mapped.bits, static_cast<size_t>(mapped.pitch) * target_rows);
    CLANG_WARN_IGNORE_POP()
    for (size_t y = 0; y < target_rows; ++y) {
        size_t const desktop_y = static_cast<size_t>(rect.top) + y;
        auto const from = source.begin() + static_cast<std::ptrdiff_t>(
                                               y * static_cast<size_t>(mapped.pitch));
        auto const to =
            packed.begin() + static_cast<std::ptrdiff_t>(
                                 (rows - 1 - desktop_y) * desktop_row_bytes +
                                 static_cast<size_t>(rect.left) * kBytesPerPixel);
        std::copy_n(from, target_row_bytes, to);
    }
    (void)readback->Unmap();
    return true;
}

// Writes every target, stitched at its rect into one desktop-sized image, as a
// bottom-up BMP. Pixels no target covers stay black.
bool Dump_target(D2DOverlayResources &res, int width, int height,
                 std::string const &path) {
    size_t const row_bytes = static_cast<size_t>(width) * kBytesPerPixel;
    std::vector<uint8_t> packed(row_bytes * static_cast<size_t>(height));
    for (D2DSurfaceTarget const &target : res.targets) {
        if (!Read_target_into(res, target, width, height, packed)) {
            return false;
        }
    }
    std::vector<uint8_t> const bmp =
        core::Build_bmp_bytes(packed, width, height, static_cast<int>(row_bytes));
    return !bmp.empty() && Write_file(path, bmp);
}

// Monitors in client pixels: each rect shifted so the desktop's top-left is (0, 0).
std::vector<core::RectPx>
Monitors_client(std::span<const core::RectPx> monitors_screen) {
    core::RectPx const vd = Union_of(monitors_screen);
    std::vector<core::RectPx> monitors_client;
    for (core::RectPx const &m : monitors_screen) {
        monitors_client.push_back(core::RectPx::From_ltrb(
            m.left - vd.left, m.top - vd.top, m.right - vd.left, m.bottom - vd.top));
    }
    return monitors_client;
}

int Run_diff(Args const &args) {
    std::optional<std::vector<uint8_t>> const a = Read_file(args.diff_a);
    std::optional<std::vector<uint8_t>> const b = Read_file(args.diff_b);
    if (!a || !b) {
        Print("diff: cannot read {} or {}\n", args.diff_a, args.diff_b);
        return static_cast<int>(BenchExit::FileError);
    }
    std::vector<core::RectPx> mask;
    if (args.mask_monitors) {
        std::optional<Layout> const layout = Find_layout(args.layout);
        if (!layout) {
            Print("unknown layout: {}\n", args.layout);
            return static_cast<int>(BenchExit::BadArgs);
        }
        mask = Monitors_client(layout->monitors_screen);
    }
    std::optional<core::BmpDiff> const diff =
        core::Diff_bmp_bytes(*a, *b, args.tolerance, mask);
    if (!diff) {
        Print("diff: not comparable (not 32bpp BMPs, or sizes differ)\n");
        return static_cast<int>(BenchExit::FileError);
    }
    Print("diff: {} pixels differ, {} over tolerance {}, max channel delta {}\n",
          diff->differing_pixels, diff->pixels_over_tolerance, args.tolerance,
          diff->max_channel_delta);
    return diff->pixels_over_tolerance <= static_cast<size_t>(args.max_pixels)
               ? static_cast<int>(BenchExit::Ok)
               : static_cast<int>(BenchExit::PixelsDiffer);
}

// ---------------------------------------------------------------------------
// B1: scenarios
// ---------------------------------------------------------------------------

constexpr std::array<std::string_view, 7> kAllScenarios = {
    {"hover", "select", "brush", "highlighter", "line", "obfuscate", "steady"}};

constexpr double kCyclesPerMegacycle = 1'000'000.0;

bool Is_freehand(std::string_view scenario) {
    return scenario == "brush" || scenario == "highlighter";
}

// --opacity, or the tool's default: 100 for the brush, the app default for the
// highlighter.
int Stroke_opacity_percent(std::string_view scenario, Args const &args) {
    return scenario == "highlighter" && !args.opacity_given
               ? core::kDefaultHighlighterOpacityPercent
               : args.opacity_percent;
}

struct ScenarioState final {
    std::vector<core::PointPx> points = {};
    std::vector<core::PointPx> straightened = {}; // --straighten-at: first and newest
    core::Annotation line = {};                   // line: the draft annotation
    core::Annotation obfuscate = {};              // obfuscate: the draft annotation
};

// obfuscate: a block pattern of kObfuscateDraftWidthPx x kObfuscateDraftHeightPx, as
// the tool's raster, at bounds.
core::ObfuscateAnnotation Make_obfuscate_draft(core::RectPx bounds) {
    core::ObfuscateAnnotation draft{};
    draft.bounds = bounds;
    draft.block_size = kObfuscateBlockPx;
    draft.bitmap_width_px = bounds.Width();
    draft.bitmap_height_px = bounds.Height();
    draft.bitmap_row_bytes = bounds.Width() * kBytesPerPixel;
    draft.premultiplied_bgra.resize(static_cast<size_t>(draft.bitmap_row_bytes) *
                                    static_cast<size_t>(draft.bitmap_height_px));
    std::span<uint8_t> const pixels(draft.premultiplied_bgra);
    for (int32_t y = 0; y < draft.bitmap_height_px; ++y) {
        for (int32_t x = 0; x < draft.bitmap_width_px; ++x) {
            bool const dark =
                ((x / kObfuscateBlockPx) + (y / kObfuscateBlockPx)) % 2 == 0;
            uint8_t const grey = dark ? kObfuscateDarkGrey : kObfuscateLightGrey;
            size_t const at =
                static_cast<size_t>(y) * static_cast<size_t>(draft.bitmap_row_bytes) +
                static_cast<size_t>(x) * kBytesPerPixel;
            pixels[at] = grey;
            pixels[at + 1] = grey;
            pixels[at + 2] = grey;
            pixels[at + 3] = 255;
        }
    }
    return draft;
}

// Point t of count on the diagonal from the inset top-left to the inset bottom-right.
core::PointPx Diagonal_point(core::RectPx sel, int64_t t, int64_t count) {
    int64_t const last = std::max<int64_t>(1, count - 1);
    int64_t const w = sel.Width() - 2 * kFreehandEdgeInsetPx;
    int64_t const h = sel.Height() - 2 * kFreehandEdgeInsetPx;
    return {static_cast<int32_t>(sel.left + kFreehandEdgeInsetPx + w * t / last),
            static_cast<int32_t>(sel.top + kFreehandEdgeInsetPx + h * t / last)};
}

// Fills the per-frame fields the way OverlayWindow::On_paint does for this mode.
void Fill_frame_input(std::string_view scenario, int frame, int frames, int width,
                      int height, Args const &args, ScenarioState &state,
                      D2DPaintInput &input) {
    int const i = std::max(frame, 0);
    int const last = std::max(1, frames - 1);
    if (scenario == "hover") {
        // Crosshair mode before any selection: magnifier, crosshair, size label.
        int const x = kSweepMarginPx +
                      static_cast<int>(
                          static_cast<int64_t>(width - 2 * kSweepMarginPx) * i / last);
        input.cursor_client_px = {x, height / 2};
    } else if (scenario == "select") {
        // Live drag from near the top-left corner to the far bottom-right.
        int const x = kSelectStartPx +
                      static_cast<int>(
                          static_cast<int64_t>(width - 2 * kSelectStartPx) * i / last);
        int const y = kSelectStartPx +
                      static_cast<int>(
                          static_cast<int64_t>(height - 2 * kSelectStartPx) * i / last);
        input.live_rect = core::RectPx::From_ltrb(kSelectStartPx, kSelectStartPx,
                                                  std::max(x, kSelectStartPx + 1),
                                                  std::max(y, kSelectStartPx + 1));
        input.cursor_client_px = {x, y};
        if (args.resize) {
            // As On_paint during a handle drag: the dragged handle's edges highlight.
            input.handle_dragging = true;
            input.highlight_handle = core::SelectionHandle::BottomRight;
        } else {
            input.dragging = true;
            input.selection_drag_corner_guide_px =
                core::PointPx{input.live_rect.right, input.live_rect.bottom};
        }
    } else if (scenario == "line" && frame >= 0) {
        // Line tool drag: from near the selection's top-left (--flat: left middle) to
        // the cursor.
        core::RectPx const sel = input.final_selection;
        auto const at = [&](int64_t t) {
            core::PointPx p = Diagonal_point(sel, t, frames);
            if (args.flat) {
                p.y = (sel.top + sel.bottom) / 2;
            }
            return p;
        };
        core::LineAnnotation const line{
            .start = at(0),
            .end = at(i),
            .style = {.width_px = kLineWidthPx, .color = kLineColor},
            .arrow_head = args.arrow,
        };
        state.line = core::Annotation{.id = 1, .data = line};
        input.draft_annotation = &state.line;
        input.cursor_client_px = line.end;
    } else if (scenario == "obfuscate" && frame >= 0) {
        // An obfuscate draft sliding left to right across the desktop's middle row,
        // over every monitor boundary. Its raster stays the same; only bounds move.
        int32_t const travel = width - 2 * kSweepMarginPx - kObfuscateDraftWidthPx;
        int32_t const left =
            kSweepMarginPx +
            static_cast<int32_t>(static_cast<int64_t>(std::max(travel, 0)) * i / last);
        int32_t const top = (height - kObfuscateDraftHeightPx) / 2;
        core::RectPx const bounds = core::RectPx::From_ltrb(
            left, top, left + kObfuscateDraftWidthPx, top + kObfuscateDraftHeightPx);
        auto *const draft =
            std::get_if<core::ObfuscateAnnotation>(&state.obfuscate.data);
        if (draft == nullptr) {
            state.obfuscate =
                core::Annotation{.id = 1, .data = Make_obfuscate_draft(bounds)};
        } else {
            draft->bounds = bounds;
        }
        input.draft_annotation = &state.obfuscate;
        input.cursor_client_px = bounds.Bottom_right();
    } else if (Is_freehand(scenario) && frame >= 0) {
        // --points-per-frame new input points per frame (default 1). The path wave
        // advances per point, so more points per frame draw a longer stroke that
        // wraps into rows and crosses itself.
        core::RectPx const sel = input.final_selection;
        double const span = static_cast<double>(sel.Width() - 2 * kFreehandEdgeInsetPx);
        for (int k = 0; k < args.points_per_frame; ++k) {
            int64_t const p = static_cast<int64_t>(i) * args.points_per_frame + k;
            double const travelled = static_cast<double>(kFreehandEdgeInsetPx) +
                                     static_cast<double>(p) * args.step_px;
            double const x = static_cast<double>(sel.left + kFreehandEdgeInsetPx) +
                             std::fmod(travelled, span);
            double const row = std::floor(travelled / span);
            double const y =
                static_cast<double>(sel.top + sel.bottom) / 2.0 +
                std::sin(static_cast<double>(p) * kFreehandWavePerFrame) *
                    static_cast<double>(sel.Height() / kFreehandAmplitudeDivisor) +
                row * kFreehandRowStepPx;
            core::PointPx point =
                args.diagonal
                    ? Diagonal_point(
                          sel, p, static_cast<int64_t>(frames) * args.points_per_frame)
                    : core::PointPx{static_cast<int32_t>(x), static_cast<int32_t>(y)};
            if (args.reverse) {
                point.x = sel.left + sel.right - point.x;
            }
            state.points.push_back(point);
        }
        input.draft_freehand_points = state.points;
        // --straighten-at: the highlighter tool keeps the first point and moves the
        // second to the cursor, as after a pause.
        if (scenario == "highlighter" && args.straighten_at >= 0 &&
            frame >= args.straighten_at) {
            state.straightened = {state.points.front(), state.points.back()};
            input.draft_freehand_points = state.straightened;
        }
        input.cursor_client_px = state.points.back();
    }
}

// --annotated: committed strokes crossing the selection edge on every monitor, so
// frames carry annotations both inside the selection and under the dim.
std::vector<core::Annotation> Make_bench_annotations(int width, int height,
                                                     int points_per_stroke) {
    constexpr int stroke_count = 4;
    constexpr double waves_per_stroke = 6.0;
    constexpr int32_t brush_width_px = 7;
    std::vector<core::Annotation> annotations;
    for (int k = 0; k < stroke_count; ++k) {
        core::FreehandStrokeAnnotation stroke{};
        bool const highlighter = k == stroke_count - 1;
        stroke.style.width_px = highlighter ? kHighlighterWidthPx : brush_width_px;
        stroke.style.color = highlighter ? kHighlighterColor : RGB(0, 0, 255);
        stroke.freehand_tip_shape = highlighter ? core::FreehandTipShape::Square
                                                : core::FreehandTipShape::Round;
        double const base_y =
            static_cast<double>(height) * (k + 1) / (stroke_count + 1);
        for (int i = 0; i < points_per_stroke; ++i) {
            double const t = static_cast<double>(i) / (points_per_stroke - 1);
            double const y =
                base_y + std::sin(t * waves_per_stroke * 2.0 * std::numbers::pi) *
                             static_cast<double>(height) / kFreehandAmplitudeDivisor;
            stroke.points.push_back(
                {static_cast<int32_t>(t * (width - 1)), static_cast<int32_t>(y)});
        }
        annotations.push_back(
            core::Annotation{.id = static_cast<uint64_t>(k + 1), .data = stroke});
    }
    return annotations;
}

// The draft stroke style the tool uses for a freehand scenario. The app sets the style
// opacity (and, for the highlighter, the blit opacity) from the tool opacity.
core::StrokeStyle Freehand_style(std::string_view scenario, Args const &args) {
    bool const highlighter = scenario == "highlighter";
    core::StrokeStyle style{};
    style.width_px = highlighter ? kHighlighterWidthPx : kBrushWidthPx;
    style.color = highlighter ? kHighlighterColor : kBrushColor;
    style.opacity_percent = Stroke_opacity_percent(scenario, args);
    return style;
}

void Set_scenario_base(std::string_view scenario, int width, int height,
                       Args const &args, D2DPaintInput &input) {
    if (scenario == "steady" || scenario == "line" || scenario == "obfuscate" ||
        Is_freehand(scenario) || (scenario == "select" && args.resize)) {
        input.final_selection = args.annotated || args.resize
                                    ? Inset_selection(width, height)
                                    : core::RectPx::From_ltrb(0, 0, width, height);
        input.cursor_client_px = {width / 2, height / 2};
    }
    if (Is_freehand(scenario)) {
        bool const highlighter = scenario == "highlighter";
        // The blit opacity (k1 for the highlighter multiply) also comes from the tool
        // opacity.
        int const opacity_percent = Stroke_opacity_percent(scenario, args);
        input.draft_freehand_style = Freehand_style(scenario, args);
        input.draft_freehand_tip_shape = highlighter ? core::FreehandTipShape::Square
                                                     : core::FreehandTipShape::Round;
        input.draft_freehand_smoothing_mode = args.smooth
                                                  ? core::FreehandSmoothingMode::Smooth
                                                  : core::FreehandSmoothingMode::Off;
        input.draft_freehand_blit_opacity_percent =
            highlighter ? opacity_percent : core::StrokeStyle::kMaxOpacityPercent;
    }
}

// ---------------------------------------------------------------------------
// move / commit: annotation edits and selection commits
// ---------------------------------------------------------------------------

// move: the moved annotation's box, left of the selection's middle row at the start.
constexpr int32_t kEditBoxWidthPx = 320;
constexpr int32_t kEditBoxHeightPx = 160;
constexpr int32_t kEditMarginPx = 32;
// --multi: the second moved annotation sits this far below the first.
constexpr int32_t kEditMultiGapPx = 24;
// --obfuscations K: each obfuscate's size and block size, on the move's path.
constexpr int32_t kEditObfuscateWidthPx = 384;
constexpr int32_t kEditObfuscateHeightPx = 224;
constexpr int32_t kEditObfuscateBlockPx = 12;
constexpr int32_t kEditStrokeWidthPx = 6;
constexpr int32_t kEditZigzagPoints = 64;
constexpr uint8_t kEditRed = 0;
constexpr uint8_t kEditGreen = 160;
constexpr uint8_t kEditBlue = 64;
constexpr COLORREF kEditColor = RGB(kEditRed, kEditGreen, kEditBlue);
// Text and bubble stand-ins: a raster of this colour and alpha, in kEditRasterCellPx
// cells, since the bench has no text layout engine.
constexpr uint8_t kEditRasterAlpha = 192;
constexpr int32_t kEditRasterCellPx = 8;
constexpr uint64_t kEditFirstId = 1000;
// commit: odd frames commit the --annotated inset shrunk by this much on each side.
constexpr int32_t kCommitShrinkPx = 32;
// --raster-load: large rasters below the moved annotation, off its path.
constexpr int32_t kLoadWidthPx = 1600;
constexpr int32_t kLoadObfuscateHeightPx = 400;
constexpr int32_t kLoadTextHeightPx = 200;
constexpr int32_t kLoadGapPx = 16;
constexpr int kLoadCount = 3;

// What OverlayWindow's obfuscate source provider sees: the screenshot, or with --lifted
// the window capture, as a GDI bitmap.
class BenchObfuscateSource final : public core::IObfuscateSourceProvider {
  public:
    BenchObfuscateSource(GdiCaptureResult capture, core::RectPx capture_bounds) noexcept
        : capture_(capture), capture_bounds_(capture_bounds) {}
    BenchObfuscateSource(BenchObfuscateSource const &) = delete;
    BenchObfuscateSource &operator=(BenchObfuscateSource const &) = delete;
    BenchObfuscateSource(BenchObfuscateSource &&) = delete;
    BenchObfuscateSource &operator=(BenchObfuscateSource &&) = delete;
    ~BenchObfuscateSource() override { capture_.Free(); }

    [[nodiscard]] std::optional<core::BgraBitmap>
    Build_composited_source(core::RectPx bounds,
                            std::span<const core::Annotation> lower) override {
        return Build_obfuscate_source(capture_, capture_bounds_, bounds, lower);
    }

  private:
    GdiCaptureResult capture_ = {};
    core::RectPx capture_bounds_ = {};
};

// The same pixels Prepare_resources and Prepare_lifted upload, as the source capture.
std::unique_ptr<BenchObfuscateSource> Make_bench_obfuscate_source(int width, int height,
                                                                  Args const &args) {
    core::RectPx const sel = Inset_selection(width, height);
    GdiCaptureResult capture{};
    core::RectPx capture_bounds = {};
    if (args.lifted) {
        core::RectPx const source =
            core::RectPx::From_ltrb(kLiftedCaptureMarginPx, kLiftedCaptureMarginPx,
                                    kLiftedCaptureMarginPx + sel.Width(),
                                    kLiftedCaptureMarginPx + sel.Height());
        capture.width = sel.Width() + 2 * kLiftedCaptureMarginPx;
        capture.height = sel.Height() + 2 * kLiftedCaptureMarginPx;
        capture.bitmap = Make_noise_bitmap(capture.width, capture.height, ~kNoiseSeed,
                                           source, kLiftedNoiseSeed);
        capture_bounds = core::RectPx::From_ltrb(
            sel.left - kLiftedCaptureMarginPx, sel.top - kLiftedCaptureMarginPx,
            sel.right + kLiftedCaptureMarginPx, sel.bottom + kLiftedCaptureMarginPx);
    } else {
        capture.width = width;
        capture.height = height;
        capture.bitmap =
            args.lifted_as_screenshot
                ? Make_noise_bitmap(width, height, kNoiseSeed, sel, kLiftedNoiseSeed)
                : Make_noise_bitmap(width, height, kNoiseSeed);
    }
    if (capture.bitmap == nullptr) {
        return nullptr;
    }
    return std::make_unique<BenchObfuscateSource>(capture, capture_bounds);
}

// A raster stand-in for text and bubble annotations: kEditColor cells over transparent.
void Fill_stand_in_raster(int32_t width, int32_t height, int32_t &out_width,
                          int32_t &out_height, int32_t &out_row_bytes,
                          std::vector<uint8_t> &out_pixels) {
    out_width = width;
    out_height = height;
    out_row_bytes = width * kBytesPerPixel;
    out_pixels.assign(static_cast<size_t>(out_row_bytes) * static_cast<size_t>(height),
                      0);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            if (((x / kEditRasterCellPx) + (y / kEditRasterCellPx)) % 2 != 0) {
                continue;
            }
            float const a = static_cast<float>(kEditRasterAlpha) / 255.f;
            size_t const at =
                static_cast<size_t>(y) * static_cast<size_t>(out_row_bytes) +
                static_cast<size_t>(x) * kBytesPerPixel;
            out_pixels[at] = static_cast<uint8_t>(kEditBlue * a);
            out_pixels[at + 1] = static_cast<uint8_t>(kEditGreen * a);
            out_pixels[at + 2] = static_cast<uint8_t>(kEditRed * a);
            out_pixels[at + 3] = kEditRasterAlpha;
        }
    }
}

// The annotation --edit-kind moves, filling box. Obfuscates get their raster later.
std::optional<core::Annotation> Make_edit_annotation(std::string_view kind,
                                                     core::RectPx box, uint64_t id) {
    core::StrokeStyle const style{.width_px = kEditStrokeWidthPx, .color = kEditColor};
    core::Annotation annotation{.id = id};
    if (kind == "rect" || kind == "filled-rect") {
        annotation.data = core::RectangleAnnotation{
            .outer_bounds = box, .style = style, .filled = kind == "filled-rect"};
    } else if (kind == "ellipse" || kind == "filled-ellipse") {
        annotation.data = core::EllipseAnnotation{
            .outer_bounds = box, .style = style, .filled = kind == "filled-ellipse"};
    } else if (kind == "line" || kind == "arrow") {
        annotation.data = core::LineAnnotation{.start = {box.left, box.top},
                                               .end = {box.right, box.bottom},
                                               .style = style,
                                               .arrow_head = kind == "arrow"};
    } else if (kind == "brush" || kind == "highlighter") {
        bool const highlighter = kind == "highlighter";
        core::FreehandStrokeAnnotation stroke{};
        stroke.style =
            highlighter ? core::StrokeStyle{.width_px = kHighlighterWidthPx,
                                            .color = kHighlighterColor,
                                            .opacity_percent =
                                                core::kDefaultHighlighterOpacityPercent}
                        : style;
        stroke.freehand_tip_shape = highlighter ? core::FreehandTipShape::Square
                                                : core::FreehandTipShape::Round;
        for (int32_t i = 0; i < kEditZigzagPoints; ++i) {
            int32_t const x =
                box.left + static_cast<int32_t>(static_cast<int64_t>(box.Width()) * i /
                                                (kEditZigzagPoints - 1));
            stroke.points.push_back({x, i % 2 == 0 ? box.top : box.bottom});
        }
        annotation.data = stroke;
    } else if (kind == "obfuscate") {
        annotation.data = core::ObfuscateAnnotation{
            .bounds = box, .block_size = kEditObfuscateBlockPx};
    } else if (kind == "text") {
        core::TextAnnotation text{};
        text.origin = {box.left, box.top};
        text.visual_bounds = box;
        Fill_stand_in_raster(box.Width(), box.Height(), text.bitmap_width_px,
                             text.bitmap_height_px, text.bitmap_row_bytes,
                             text.premultiplied_bgra);
        annotation.data = text;
    } else if (kind == "bubble") {
        core::BubbleAnnotation bubble{};
        bubble.diameter_px = box.Height();
        bubble.center = {box.left + box.Height() / 2, box.top + box.Height() / 2};
        bubble.color = kEditColor;
        Fill_stand_in_raster(bubble.diameter_px, bubble.diameter_px,
                             bubble.bitmap_width_px, bubble.bitmap_height_px,
                             bubble.bitmap_row_bytes, bubble.premultiplied_bgra);
        annotation.data = bubble;
    } else {
        return std::nullopt;
    }
    return annotation;
}

// The obfuscate at index rebuilt over the annotations below it, as a commit does.
bool Commit_obfuscate(std::vector<core::Annotation> &doc, size_t index,
                      core::IObfuscateSourceProvider &source) {
    if (!std::holds_alternative<core::ObfuscateAnnotation>(doc[index].data)) {
        return true;
    }
    std::optional<core::Annotation> rebuilt = Build_preview_obfuscate_annotation(
        doc[index], std::span<const core::Annotation>(doc).first(index), source);
    if (!rebuilt.has_value()) {
        return false;
    }
    doc[index] = std::move(*rebuilt);
    return true;
}

// move and commit: the document, what moves, and the obfuscate source.
struct EditScene final {
    EditScene() = default;
    EditScene(EditScene const &) = delete;
    EditScene &operator=(EditScene const &) = delete;
    EditScene(EditScene &&) = default;
    EditScene &operator=(EditScene &&) = default;
    ~EditScene() = default;

    std::vector<core::Annotation> doc = {};
    std::vector<size_t> moved = {};           // indices of the moved annotations
    std::vector<core::Annotation> start = {}; // the moved annotations at the start
    std::vector<core::AnnotationEditPreview> previews = {};
    std::vector<AnnotationPreviewPatch> patches = {};
    std::unique_ptr<BenchObfuscateSource> source = nullptr;
    int32_t travel_px = 0;
    std::vector<core::MonitorWithBounds> monitors = {}; // commit: the layout's monitors
    core::PointPx origin = {};                          // commit: desktop origin
};

// Builds the scene: --annotated strokes (--stroke-points each), --obfuscations K on
// the move's path (above the moved annotation, or below it with --obfuscate-below),
// and the moved annotation (two with --multi). Every obfuscate is committed.
std::optional<EditScene> Make_edit_scene(std::string_view scenario, int width,
                                         int height, Layout const &layout,
                                         Args const &args) {
    EditScene scene{};
    scene.source = Make_bench_obfuscate_source(width, height, args);
    if (scene.source == nullptr) {
        return std::nullopt;
    }
    if (args.annotated) {
        scene.doc = Make_bench_annotations(width, height, args.stroke_points);
    }
    core::RectPx const sel = Inset_selection(width, height);
    int32_t const mid_y = (sel.top + sel.bottom) / 2;
    uint64_t next_id = kEditFirstId;

    std::vector<core::Annotation> obfuscates;
    for (int k = 0; k < args.obfuscations; ++k) {
        int32_t const center_x =
            sel.left + static_cast<int32_t>(static_cast<int64_t>(sel.Width()) *
                                            (k + 1) / (args.obfuscations + 1));
        core::RectPx const box = core::RectPx::From_ltrb(
            center_x - kEditObfuscateWidthPx / 2, mid_y - kEditObfuscateHeightPx / 2,
            center_x + kEditObfuscateWidthPx / 2, mid_y + kEditObfuscateHeightPx / 2);
        obfuscates.push_back(*Make_edit_annotation("obfuscate", box, next_id++));
    }

    std::vector<core::Annotation> moved;
    if (scenario == "move") {
        core::RectPx const box = core::RectPx::From_ltrb(
            sel.left + kEditMarginPx, mid_y - kEditBoxHeightPx / 2,
            sel.left + kEditMarginPx + kEditBoxWidthPx, mid_y + kEditBoxHeightPx / 2);
        std::optional<core::Annotation> first =
            Make_edit_annotation(args.edit_kind, box, next_id++);
        if (!first.has_value()) {
            Print("unknown --edit-kind {}\n", args.edit_kind);
            return std::nullopt;
        }
        moved.push_back(std::move(*first));
        if (args.multi) {
            core::RectPx const below = core::RectPx::From_ltrb(
                box.left, box.bottom + kEditMultiGapPx, box.right,
                box.bottom + kEditMultiGapPx + kEditBoxHeightPx);
            moved.push_back(*Make_edit_annotation("ellipse", below, next_id++));
        }
        scene.travel_px = sel.Width() - 2 * kEditMarginPx - kEditBoxWidthPx;
    }

    auto const append = [&](std::vector<core::Annotation> &from, bool is_moved) {
        for (core::Annotation &annotation : from) {
            if (is_moved) {
                scene.moved.push_back(scene.doc.size());
                scene.start.push_back(annotation);
            }
            scene.doc.push_back(std::move(annotation));
        }
    };
    if (args.raster_load) {
        std::vector<core::Annotation> load;
        for (int k = 0; k < kLoadCount; ++k) {
            int32_t const left = sel.left + k * (kLoadWidthPx + kLoadGapPx);
            load.push_back(*Make_edit_annotation(
                "obfuscate",
                core::RectPx::From_ltrb(left, sel.top + kLoadGapPx, left + kLoadWidthPx,
                                        sel.top + kLoadGapPx + kLoadObfuscateHeightPx),
                next_id++));
            load.push_back(*Make_edit_annotation(
                "text",
                core::RectPx::From_ltrb(left,
                                        sel.bottom - kLoadGapPx - kLoadTextHeightPx,
                                        left + kLoadWidthPx, sel.bottom - kLoadGapPx),
                next_id++));
        }
        append(load, false);
    }
    if (args.obfuscate_below) {
        append(obfuscates, false);
        append(moved, true);
    } else {
        append(moved, true);
        append(obfuscates, false);
    }
    for (size_t i = 0; i < scene.doc.size(); ++i) {
        if (!Commit_obfuscate(scene.doc, i, *scene.source)) {
            Print("move: could not build an obfuscate source\n");
            return std::nullopt;
        }
    }
    for (size_t i = 0; i < scene.moved.size(); ++i) {
        scene.start[i] = scene.doc[scene.moved[i]];
    }

    for (core::RectPx const &m : layout.monitors_screen) {
        core::MonitorWithBounds monitor{};
        monitor.bounds = m;
        monitor.info.dpi_scale.percent = 100;
        scene.monitors.push_back(monitor);
    }
    core::RectPx const vd = Union_of(layout.monitors_screen);
    scene.origin = {vd.left, vd.top};
    return scene;
}

// --resize: the moved annotation's right edge follows the drag instead of the whole
// annotation. Kinds without a box move instead.
core::Annotation Edited_annotation(core::Annotation const &start, int32_t dx,
                                   bool resize) {
    if (!resize) {
        return core::Translate_annotation(start, {dx, 0});
    }
    core::Annotation edited = start;
    std::visit(core::Overloaded{
                   [&](core::RectangleAnnotation &a) { a.outer_bounds.right += dx; },
                   [&](core::EllipseAnnotation &a) { a.outer_bounds.right += dx; },
                   [&](core::ObfuscateAnnotation &a) { a.bounds.right += dx; },
                   [&](auto &) { edited = core::Translate_annotation(start, {dx, 0}); },
               },
               edited.data);
    return edited;
}

// move: frame i of the drag, as OverlayWindow::On_paint fills an edit frame: the
// document with the moved annotations at their new place, the edit previews, and the
// obfuscate patches built through the shared preview builder.
void Fill_move_frame(EditScene &scene, int frame, int frames, Args const &args,
                     D2DPaintInput &input) {
    int const i = std::max(frame, 0);
    int const last = std::max(1, frames - 1);
    int32_t const dx =
        static_cast<int32_t>(static_cast<int64_t>(scene.travel_px) * i / last);
    scene.previews.clear();
    for (size_t m = 0; m < scene.moved.size(); ++m) {
        core::Annotation const edited =
            Edited_annotation(scene.start[m], dx, args.resize);
        scene.previews.push_back(
            core::AnnotationEditPreview{.index = scene.moved[m],
                                        .annotation_before = scene.start[m],
                                        .annotation_after = edited});
        scene.doc[scene.moved[m]] = edited;
    }
    std::vector<size_t> const preview_indices =
        core::Obfuscate_preview_indices(scene.doc, scene.previews);
    scene.patches =
        Build_obfuscate_preview_patches(scene.doc, preview_indices, *scene.source);
    input.annotation_editing = true;
    input.annotations = scene.doc;
    input.annotation_patches = scene.patches;
    input.annotation_edit_previews = scene.previews;
}

// move: the release, as the controller commits it: every obfuscate rebuilt over its
// final lower annotations, no previews, the caches invalidated.
bool Commit_move(EditScene &scene, D2DOverlayResources &res, D2DPaintInput &input) {
    for (size_t i = 0; i < scene.doc.size(); ++i) {
        if (!Commit_obfuscate(scene.doc, i, *scene.source)) {
            return false;
        }
    }
    scene.previews.clear();
    scene.patches.clear();
    input.annotation_editing = false;
    input.annotations = scene.doc;
    input.annotation_patches = {};
    input.annotation_edit_previews = {};
    res.Apply_overlay_action(core::OverlayAction::InvalidateFrozenCache);
    return true;
}

// commit: frame i drags a new selection on a fresh overlay controller (the --annotated
// inset, shrunk on odd frames) and applies the action its release returns.
void Fill_commit_frame(EditScene &scene, int frame, int width, int height,
                       D2DOverlayResources &res, D2DPaintInput &input) {
    core::RectPx sel = Inset_selection(width, height);
    if (frame % 2 != 0) {
        sel = core::RectPx::From_ltrb(
            sel.left + kCommitShrinkPx, sel.top + kCommitShrinkPx,
            sel.right - kCommitShrinkPx, sel.bottom - kCommitShrinkPx);
    }
    core::OverlayController controller;
    controller.Reset_for_session(scene.monitors);
    core::PointPx const from = {sel.left, sel.top};
    core::PointPx const to = {sel.right, sel.bottom};
    core::PointPx const from_screen = {from.x + scene.origin.x,
                                       from.y + scene.origin.y};
    core::PointPx const to_screen = {to.x + scene.origin.x, to.y + scene.origin.y};
    (void)controller.On_primary_press({}, from, from_screen, std::nullopt, std::nullopt,
                                      std::nullopt, {}, core::SnapEdges{},
                                      scene.origin.x, scene.origin.y);
    (void)controller.On_pointer_move({}, to, to_screen, std::nullopt, {}, std::nullopt,
                                     scene.origin.x, scene.origin.y);
    core::OverlayAction const action = controller.On_primary_release({}, to);
    res.Apply_overlay_action(action);
    input.final_selection = controller.State().final_selection;
    input.annotations = scene.doc;
}

struct FrameSample final {
    double cpu_ms = 0.0;
    double cpu_mcycles = 0.0; // thread CPU cycles over the same span, in millions
    double gpu_ms = -1.0;     // negative: timestamps were disjoint
    double frame_ms = 0.0;
    double mpx = 0.0;
    size_t points = 0;
};

struct GpuQueries final {
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> start;
    ComPtr<ID3D11Query> end;
    ComPtr<ID3D11Query> done;
    ComPtr<ID3D11Query> stats;
};

bool Create_queries(ID3D11Device *device, GpuQueries &q) {
    device->GetImmediateContext(q.context.GetAddressOf());
    D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    if (FAILED(device->CreateQuery(&desc, q.disjoint.GetAddressOf()))) return false;
    desc.Query = D3D11_QUERY_TIMESTAMP;
    if (FAILED(device->CreateQuery(&desc, q.start.GetAddressOf()))) return false;
    if (FAILED(device->CreateQuery(&desc, q.end.GetAddressOf()))) return false;
    desc.Query = D3D11_QUERY_EVENT;
    if (FAILED(device->CreateQuery(&desc, q.done.GetAddressOf()))) return false;
    desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
    return SUCCEEDED(device->CreateQuery(&desc, q.stats.GetAddressOf()));
}

template <typename T> T Wait_query(ID3D11DeviceContext *context, ID3D11Query *query) {
    T value{};
    while (context->GetData(query, &value, sizeof(value), 0) != S_OK) {
        YieldProcessor();
    }
    return value;
}

// Fixed 3-decimal text, for CSV fields and table cells that may read n/a.
std::string Csv_double(double value) { return std::format("{:.3f}", value); }

void Print_row(std::string_view label, core::FrameTimeSummary const &s) {
    Print("{:<6} {:7.2f} {:7.2f} {:7.2f} {:7.2f} {:7.2f} {:6.0f}% {:6.0f}%\n", label,
          s.p50_ms, s.p90_ms, s.p95_ms, s.p99_ms, s.max_ms, s.percent_within_30fps,
          s.percent_within_60fps);
}

struct RunContext final {
    D2DOverlayResources *res = nullptr;
    GpuQueries *queries = nullptr;
    Layout const *layout = nullptr;
    std::vector<core::RectPx> const *monitors_client = nullptr;
    std::wstring const *adapter_name = nullptr;
    int width = 0;
    int height = 0;
    std::ofstream *csv = nullptr;
};

// The annotation the tool commits for these draft points: smoothed when smoothing is
// on, raw when off or straightened.
core::Annotation Committed_stroke(std::span<const core::PointPx> raw,
                                  core::StrokeStyle const &style,
                                  D2DPaintInput const &input, Args const &args,
                                  bool straightened, uint64_t id) {
    core::FreehandStrokeAnnotation stroke{
        .points = args.smooth && !straightened
                      ? core::Smooth_freehand_points(
                            raw, core::FreehandSmoothingMode::Smooth, style.width_px)
                      : std::vector<core::PointPx>(raw.begin(), raw.end()),
        .style = style,
        .freehand_tip_shape = input.draft_freehand_tip_shape,
    };
    return core::Annotation{.id = id, .data = stroke};
}

// --verify-edit: reads the frame just drawn, draws it again with the edit fast paths
// off, and returns the number of pixels that differ (nullopt if a step failed). The
// edit caches stay as they were for the next frame.
std::optional<size_t> Verify_edit_frame(D2DOverlayResources &res,
                                        D2DPaintInput const &input, int width,
                                        int height) {
    size_t const row_bytes = static_cast<size_t>(width) * kBytesPerPixel;
    std::vector<uint8_t> fast(row_bytes * static_cast<size_t>(height));
    std::vector<uint8_t> full(fast.size());
    for (D2DSurfaceTarget const &target : res.targets) {
        if (!Read_target_into(res, target, width, height, fast)) {
            return std::nullopt;
        }
    }
    bool const was_fast = res.edit_fast_paths;
    res.edit_fast_paths = false;
    bool const ok = Paint_d2d_frame(res, input, width, height, nullptr,
                                    kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
    res.edit_fast_paths = was_fast;
    if (!ok) {
        return std::nullopt;
    }
    for (D2DSurfaceTarget const &target : res.targets) {
        if (!Read_target_into(res, target, width, height, full)) {
            return std::nullopt;
        }
    }
    // Whole-buffer compare first: a per-pixel loop over a desktop-sized buffer takes
    // seconds per frame in debug builds.
    if (std::ranges::equal(fast, full)) {
        return 0;
    }
    size_t differing = 0;
    for (size_t at = 0; at < fast.size(); at += kBytesPerPixel) {
        if (!std::equal(fast.begin() + static_cast<std::ptrdiff_t>(at),
                        fast.begin() + static_cast<std::ptrdiff_t>(at + kBytesPerPixel),
                        full.begin() + static_cast<std::ptrdiff_t>(at))) {
            ++differing;
        }
    }
    return differing;
}

// Reads every target into packed (a desktop-sized bottom-up BGRA buffer).
bool Read_targets(D2DOverlayResources &res, int width, int height,
                  std::vector<uint8_t> &packed) {
    packed.assign(
        static_cast<size_t>(width) * kBytesPerPixel * static_cast<size_t>(height), 0);
    for (D2DSurfaceTarget const &target : res.targets) {
        if (!Read_target_into(res, target, width, height, packed)) {
            return false;
        }
    }
    return true;
}

// --click-moved: paints a steady frame from the caches as they are, then again with
// the annotations and the frozen frame rebuilt from input's annotations, and returns
// how many pixels differ (nullopt if a step failed).
std::optional<size_t> Steady_differs_from_rebuild(D2DOverlayResources &res,
                                                  D2DPaintInput const &input, int width,
                                                  int height) {
    std::vector<uint8_t> cached;
    std::vector<uint8_t> fresh;
    if (Paint_d2d_frame(res, input, width, height, nullptr, kFrameWaitTimeoutMs) !=
            D2DFrameStatus::Ok ||
        !Read_targets(res, width, height, cached)) {
        return std::nullopt;
    }
    Rebuild_annotations_bitmap(res, input.annotations);
    Rebuild_frozen_bitmap(res, input.final_selection, width, height);
    if (Paint_d2d_frame(res, input, width, height, nullptr, kFrameWaitTimeoutMs) !=
            D2DFrameStatus::Ok ||
        !Read_targets(res, width, height, fresh)) {
        return std::nullopt;
    }
    if (std::ranges::equal(cached, fresh)) {
        return 0;
    }
    size_t differing = 0;
    for (size_t at = 0; at < cached.size(); at += kBytesPerPixel) {
        if (!std::equal(cached.begin() + static_cast<std::ptrdiff_t>(at),
                        cached.begin() +
                            static_cast<std::ptrdiff_t>(at + kBytesPerPixel),
                        fresh.begin() + static_cast<std::ptrdiff_t>(at))) {
            ++differing;
        }
    }
    return differing;
}

// Runs one scenario once. Returns the frame-time p95, or nullopt on paint failure.
std::optional<double> Run_scenario(RunContext &ctx, std::string_view scenario,
                                   Args const &args, int run, bool dump_here) {
    D2DPaintInput input{};
    input.monitor_rects_client = *ctx.monitors_client;
    Set_scenario_base(scenario, ctx.width, ctx.height, args, input);
    input.lifted_window_bitmap = ctx.res->lifted_view.bitmap;
    input.lifted_window_dest_rect = ctx.res->lifted_view.dest_rect;
    input.lifted_window_source_rect = ctx.res->lifted_view.source_rect;
    std::vector<core::Annotation> annotations =
        args.annotated
            ? Make_bench_annotations(ctx.width, ctx.height, args.stroke_points)
            : std::vector<core::Annotation>{};
    input.annotations = annotations;
    Rebuild_annotations_bitmap(*ctx.res, annotations);
    ctx.res->frozen_valid = false;
    // --dab N: a short stroke of the same tool, from the same start point, drawn and
    // committed before the measured stroke (as with a quick click then a fast stroke).
    // --dab-no-draft commits it without drawing it, so the measured stroke starts from
    // a fresh draft: the two runs' frames must match.
    if (Is_freehand(scenario) && args.dab_points > 0) {
        ScenarioState dab_state;
        D2DPaintInput dab_input = input;
        Fill_frame_input(scenario, 0, args.frames, ctx.width, ctx.height, args,
                         dab_state, dab_input);
        dab_state.points.resize(
            std::min(dab_state.points.size(), static_cast<size_t>(args.dab_points)));
        bool ok = true;
        if (!args.dab_no_draft) {
            dab_input.draft_freehand_points = dab_state.points;
            Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width,
                                  ctx.height);
            ok = Paint_d2d_frame(*ctx.res, dab_input, ctx.width, ctx.height, nullptr,
                                 kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
        }
        annotations.push_back(Committed_stroke(
            dab_state.points, Freehand_style(scenario, args), input, args,
            /*straightened=*/false, static_cast<uint64_t>(annotations.size() + 1)));
        input.annotations = annotations;
        Rebuild_annotations_bitmap(*ctx.res, annotations);
        Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width, ctx.height);
        if (ok && !args.dab_no_draft) {
            ok = Paint_d2d_frame(*ctx.res, input, ctx.width, ctx.height, nullptr,
                                 kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
        }
        if (!ok) {
            Print("paint failed (device lost?)\n");
            return std::nullopt;
        }
    }
    // move and commit: the scene, its committed annotations and caches.
    std::optional<EditScene> edit = std::nullopt;
    if (scenario == "move" || scenario == "commit") {
        edit = Make_edit_scene(scenario, ctx.width, ctx.height, *ctx.layout, args);
        if (!edit.has_value()) {
            return std::nullopt;
        }
        input.final_selection = Inset_selection(ctx.width, ctx.height);
        input.annotations = edit->doc;
        Rebuild_annotations_bitmap(*ctx.res, edit->doc);
    }
    ctx.res->edit_fast_paths = !args.edit_dynamic;
    if (args.stale_frozen && edit.has_value() && scenario == "move") {
        // Valid and built for the selection, but from the annotations moved halfway,
        // as a frozen rebuild in the middle of an edit leaves it. Edit frames must not
        // take it for the edit's start.
        D2DPaintInput mid = input;
        Fill_move_frame(*edit, args.frames / 2, args.frames, args, mid);
        Rebuild_annotations_bitmap(*ctx.res, edit->doc);
        Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width, ctx.height);
        for (size_t m = 0; m < edit->moved.size(); ++m) {
            edit->doc[edit->moved[m]] = edit->start[m];
        }
        Rebuild_annotations_bitmap(*ctx.res, edit->doc);
    } else if (args.stale_frozen) {
        // As a selection drag leaves it: valid, but built for no selection. The paint
        // must rebuild it for the frame's selection.
        Rebuild_frozen_bitmap(*ctx.res, core::RectPx{}, ctx.width, ctx.height);
    }
    if (args.click_first && edit.has_value() && scenario == "move") {
        // A click on the annotation that does not move it: one edit frame, then the
        // release returns no action, so nothing rebuilds. Then a selection drag (the
        // frozen frame built for no selection) and its commit (built for the selection
        // again). The drag that follows must still take the edit fast paths.
        // --click-moved: the painted edit frame shows the annotation moved, and the
        // release (back at the start) returns no action.
        D2DPaintInput click = input;
        Fill_move_frame(*edit, args.click_moved ? args.frames / 2 : 0, args.frames,
                        args, click);
        for (size_t m = 0; m < edit->moved.size(); ++m) {
            edit->doc[edit->moved[m]] = edit->start[m];
        }
        D2DPaintInput idle = input;
        idle.annotations = edit->doc;
        bool const ok = Paint_d2d_frame(*ctx.res, click, ctx.width, ctx.height, nullptr,
                                        kFrameWaitTimeoutMs) == D2DFrameStatus::Ok &&
                        Paint_d2d_frame(*ctx.res, idle, ctx.width, ctx.height, nullptr,
                                        kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
        if (!ok) {
            Print("paint failed (device lost?)\n");
            return std::nullopt;
        }
        Rebuild_frozen_bitmap(*ctx.res, core::RectPx{}, ctx.width, ctx.height);
        Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width, ctx.height);
        if (args.click_moved) {
            std::optional<size_t> const differing =
                Steady_differs_from_rebuild(*ctx.res, idle, ctx.width, ctx.height);
            if (!differing.has_value()) {
                Print("click-moved: readback failed\n");
                return std::nullopt;
            }
            Print("click-moved: the steady frame differs from a fresh rebuild in {} "
                  "pixels\n",
                  *differing);
            if (*differing > 0) {
                return std::nullopt;
            }
        }
    }
    ctx.res->edit_stats = {};
    size_t verify_differing_frames = 0;
    ScenarioState state;
    std::vector<FrameSample> samples;
    samples.reserve(static_cast<size_t>(args.frames));

    for (int f = -kWarmupFrames; f < args.frames; ++f) {
        Fill_frame_input(scenario, f, args.frames, ctx.width, ctx.height, args, state,
                         input);
        ID3D11DeviceContext *const context = ctx.queries->context.Get();
        context->Begin(ctx.queries->disjoint.Get());
        context->End(ctx.queries->start.Get());
        context->Begin(ctx.queries->stats.Get());
        ULONG64 cycles0 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &cycles0);
        LARGE_INTEGER const t0 = Now();
        // move and commit: OverlayWindow's input and action work for the frame (the
        // obfuscate patches, a selection commit), timed as part of the frame.
        if (edit.has_value() && scenario == "move") {
            Fill_move_frame(*edit, f, args.frames, args, input);
        } else if (edit.has_value()) {
            Fill_commit_frame(*edit, f, ctx.width, ctx.height, *ctx.res, input);
        }
        // Mirror OverlayWindow::On_paint: rebuild invalid caches before the frame.
        if (!ctx.res->annotations_valid) {
            Rebuild_annotations_bitmap(
                *ctx.res, edit.has_value()
                              ? std::span<const core::Annotation>(edit->doc)
                              : std::span<const core::Annotation>(annotations));
        }
        if (!ctx.res->frozen_valid) {
            Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width,
                                  ctx.height);
        }
        if (args.line_dynamic || (args.select_dynamic && scenario == "select")) {
            // A stale frozen frame sends a line draft or a selection gesture down the
            // dynamic path.
            ctx.res->frozen_valid = false;
        }
        bool const ok = Paint_d2d_frame(*ctx.res, input, ctx.width, ctx.height, nullptr,
                                        kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
        LARGE_INTEGER const t1 = Now();
        ULONG64 cycles1 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &cycles1);
        context->End(ctx.queries->stats.Get());
        context->End(ctx.queries->end.Get());
        context->End(ctx.queries->disjoint.Get());
        context->End(ctx.queries->done.Get());
        context->Flush();
        (void)Wait_query<BOOL>(context, ctx.queries->done.Get());
        LARGE_INTEGER const t2 = Now();
        auto const dj = Wait_query<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(
            context, ctx.queries->disjoint.Get());
        auto const ts0 = Wait_query<UINT64>(context, ctx.queries->start.Get());
        auto const ts1 = Wait_query<UINT64>(context, ctx.queries->end.Get());
        auto const ps = Wait_query<D3D11_QUERY_DATA_PIPELINE_STATISTICS>(
            context, ctx.queries->stats.Get());
        if (!ok) {
            Print("paint failed (device lost?)\n");
            return std::nullopt;
        }
        if (f < 0) {
            continue;
        }
        FrameSample sample;
        sample.cpu_ms = Qpc_ms(t0, t1);
        sample.cpu_mcycles =
            static_cast<double>(cycles1 - cycles0) / kCyclesPerMegacycle;
        sample.frame_ms = Qpc_ms(t0, t2);
        if (dj.Disjoint == FALSE && dj.Frequency != 0 && ts1 >= ts0) {
            sample.gpu_ms = static_cast<double>(ts1 - ts0) * kMsPerSecond /
                            static_cast<double>(dj.Frequency);
        }
        sample.mpx = static_cast<double>(ps.PSInvocations) / kMegapixel;
        sample.points = state.points.size();
        samples.push_back(sample);
        // --verify-edit: the same frame again through the full rebuild and the dynamic
        // path, compared pixel for pixel, outside timing.
        if (args.verify_edit && edit.has_value()) {
            std::optional<size_t> const differing =
                Verify_edit_frame(*ctx.res, input, ctx.width, ctx.height);
            if (!differing.has_value()) {
                Print("verify-edit: readback failed\n");
                return std::nullopt;
            }
            if (*differing > 0) {
                ++verify_differing_frames;
                Print("verify-edit: frame {} differs in {} pixels\n", f, *differing);
            }
        }
        // Readback happens after the frame's numbers are taken, outside timing.
        if (dump_here && f == args.dump_frame) {
            if (!Dump_target(*ctx.res, ctx.width, ctx.height, args.dump_path)) {
                Print("dump: failed to write {}\n", args.dump_path);
                return std::nullopt;
            }
            Print("dump: frame {} of {} written to {}\n", f, scenario, args.dump_path);
        }
    }

    if (args.verify_edit && edit.has_value()) {
        Print("verify-edit: {} of {} frames differ\n", verify_differing_frames,
              args.frames);
        if (verify_differing_frames > 0) {
            return std::nullopt;
        }
        // The fast paths must have run, or the check above proves nothing. A lifted
        // window and a frozen frame built mid-edit must keep the changed-area path off.
        D2DOverlayResources::EditStats const stats = ctx.res->edit_stats;
        bool const region_expected = !args.lifted && !args.stale_frozen;
        Print("verify-edit: {} changed-area frames, {} below-cache hits\n",
              stats.region_frames, stats.below_cache_hits);
        if (scenario == "move" && ((region_expected && stats.region_frames == 0) ||
                                   (!region_expected && stats.region_frames > 0) ||
                                   (args.annotated && stats.below_cache_hits == 0))) {
            Print("verify-edit: the edit fast paths did not run as expected\n");
            return std::nullopt;
        }
    }

    // move: the release frame. The controller commits the edit (every obfuscate
    // rebuilt), the caches are invalidated and rebuilt, and the frame has no previews.
    std::optional<std::pair<double, double>> move_release_ms = std::nullopt;
    ULONG64 move_release_cycles = 0;
    if (edit.has_value() && scenario == "move") {
        ID3D11DeviceContext *const context = ctx.queries->context.Get();
        ULONG64 cycles0 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &cycles0);
        LARGE_INTEGER const t0 = Now();
        bool ok = Commit_move(*edit, *ctx.res, input);
        if (ok && !ctx.res->annotations_valid) {
            Rebuild_annotations_bitmap(*ctx.res, edit->doc);
        }
        if (ok && !ctx.res->frozen_valid) {
            Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width,
                                  ctx.height);
        }
        ok = ok && Paint_d2d_frame(*ctx.res, input, ctx.width, ctx.height, nullptr,
                                   kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
        LARGE_INTEGER const t1 = Now();
        ULONG64 cycles1 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &cycles1);
        move_release_cycles = cycles1 - cycles0;
        context->End(ctx.queries->done.Get());
        context->Flush();
        (void)Wait_query<BOOL>(context, ctx.queries->done.Get());
        LARGE_INTEGER const t2 = Now();
        if (!ok) {
            Print("move: release failed\n");
            return std::nullopt;
        }
        move_release_ms = std::pair{Qpc_ms(t0, t1), Qpc_ms(t0, t2)};
        if (dump_here && args.dump_frame == args.frames) {
            if (!Dump_target(*ctx.res, ctx.width, ctx.height, args.dump_path)) {
                Print("dump: failed to write {}\n", args.dump_path);
                return std::nullopt;
            }
            Print("dump: release frame of {} written to {}\n", scenario,
                  args.dump_path);
        }
    }

    // Freehand: the release frame. Commit the stroke the way the tool does (smoothed
    // when smoothing is on, raw when off or straightened), rebuild the caches and paint
    // the frame with no draft.
    std::optional<std::pair<double, double>> commit_ms = std::nullopt;
    // Thread CPU cycles: unlike QPC milliseconds, they leave out time the thread spends
    // waiting or preempted, so they hold up on a loaded machine.
    ULONG64 commit_cycles = 0;
    size_t commit_points = 0;
    std::pair<double, double> rebuild_ms = {};
    ULONG64 rebuild_cycles = 0;
    if (Is_freehand(scenario) && !input.draft_freehand_points.empty()) {
        std::span<const core::PointPx> const raw = input.draft_freehand_points;
        bool const straightened = raw.size() == 2 && args.straighten_at >= 0;
        std::vector<core::Annotation> committed = annotations;
        committed.push_back(Committed_stroke(
            raw, Freehand_style(scenario, args), input, args, straightened,
            static_cast<uint64_t>(annotations.size() + 1)));
        commit_points = std::get<core::FreehandStrokeAnnotation>(committed.back().data)
                            .points.size();
        input.annotations = committed;
        input.draft_freehand_points = {};
        ID3D11DeviceContext *const context = ctx.queries->context.Get();
        ULONG64 cycles0 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &cycles0);
        LARGE_INTEGER const t0 = Now();
        Rebuild_annotations_bitmap(*ctx.res, committed);
        Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width, ctx.height);
        bool const ok = Paint_d2d_frame(*ctx.res, input, ctx.width, ctx.height, nullptr,
                                        kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
        LARGE_INTEGER const t1 = Now();
        ULONG64 cycles1 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &cycles1);
        commit_cycles = cycles1 - cycles0;
        context->End(ctx.queries->done.Get());
        context->Flush();
        (void)Wait_query<BOOL>(context, ctx.queries->done.Get());
        LARGE_INTEGER const t2 = Now();
        input.annotations = annotations;
        if (!ok) {
            Print("paint failed (device lost?)\n");
            return std::nullopt;
        }
        commit_ms = std::pair{Qpc_ms(t0, t1), Qpc_ms(t0, t2)};
        if (dump_here && args.dump_frame == args.frames) {
            if (!Dump_target(*ctx.res, ctx.width, ctx.height, args.dump_path)) {
                Print("dump: failed to write {}\n", args.dump_path);
                return std::nullopt;
            }
            Print("dump: release frame of {} written to {}\n", scenario,
                  args.dump_path);
            // The same frame with the annotations rebuilt from scratch, as after an
            // undo and redo: the draft is gone, so nothing of it can be reused.
            input.annotations = committed;
            Rebuild_annotations_bitmap(*ctx.res, committed);
            Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width,
                                  ctx.height);
            bool const rebuilt =
                Paint_d2d_frame(*ctx.res, input, ctx.width, ctx.height, nullptr,
                                kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
            input.annotations = annotations;
            std::string const rebuild_path = args.dump_path + ".rebuild.bmp";
            if (!rebuilt ||
                !Dump_target(*ctx.res, ctx.width, ctx.height, rebuild_path)) {
                Print("dump: failed to write {}\n", rebuild_path);
                return std::nullopt;
            }
            Print("dump: rebuilt release frame written to {}\n", rebuild_path);
        }
        // Undo, redo or an edit: the annotations rebuilt with no draft left to reuse
        // (the release frame cleared it).
        ULONG64 rebuild_cycles0 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &rebuild_cycles0);
        LARGE_INTEGER const rebuild_t0 = Now();
        Rebuild_annotations_bitmap(*ctx.res, committed);
        LARGE_INTEGER const rebuild_t1 = Now();
        ULONG64 rebuild_cycles1 = 0;
        (void)QueryThreadCycleTime(GetCurrentThread(), &rebuild_cycles1);
        rebuild_cycles = rebuild_cycles1 - rebuild_cycles0;
        context->End(ctx.queries->done.Get());
        context->Flush();
        (void)Wait_query<BOOL>(context, ctx.queries->done.Get());
        rebuild_ms =
            std::pair{Qpc_ms(rebuild_t0, rebuild_t1), Qpc_ms(rebuild_t0, Now())};
    }

    std::vector<double> cpu, gpu, frame, mpx;
    for (FrameSample const &s : samples) {
        cpu.push_back(s.cpu_ms);
        frame.push_back(s.frame_ms);
        mpx.push_back(s.mpx);
        if (s.gpu_ms >= 0.0) gpu.push_back(s.gpu_ms);
        if (ctx.csv != nullptr) {
            *ctx.csv << ctx.layout->name << ',' << '"' << Narrow(*ctx.adapter_name)
                     << '"' << ',' << scenario << ',' << (args.smooth ? "on" : "off")
                     << ',' << run << ',' << (&s - samples.data()) << ','
                     << Csv_double(s.cpu_ms) << ','
                     << (s.gpu_ms >= 0.0 ? Csv_double(s.gpu_ms) : std::string()) << ','
                     << Csv_double(s.frame_ms) << ',' << Csv_double(s.mpx) << ','
                     << s.points << '\n';
        }
    }
    core::FrameTimeSummary const frame_summary = core::Summarize_frame_times(frame);
    core::FrameRateVerdict const verdict = core::Judge_frame_times(frame_summary);
    double const desktop_mpx =
        static_cast<double>(ctx.width) * static_cast<double>(ctx.height) / kMegapixel;
    double const shaded = Median(mpx);
    std::vector<double> mcycles;
    mcycles.reserve(samples.size());
    for (FrameSample const &s : samples) {
        mcycles.push_back(s.cpu_mcycles);
    }

    Print("\nlayout={} vd={}x{} adapter=\"{}\" scenario={} smooth={} "
          "points/frame={} opacity={} frames={} run={}/{}\n",
          ctx.layout->name, ctx.width, ctx.height, Narrow(*ctx.adapter_name), scenario,
          args.smooth ? "on" : "off", args.points_per_frame,
          Stroke_opacity_percent(scenario, args), args.frames, run + 1, args.repeat);
    Print("           p50     p90     p95     p99     max  <=33.3  <=16.7\n");
    Print_row("cpu", core::Summarize_frame_times(cpu));
    if (gpu.empty()) {
        Print("gpu    n/a (timestamps disjoint)\n");
    } else {
        Print_row("gpu", core::Summarize_frame_times(gpu));
    }
    Print_row("frame", frame_summary);
    Print("shaded p50 {:.1f} Mpx ({:.2f} x desktop); cpu p50 {:.1f} Mcycles\n", shaded,
          desktop_mpx > 0.0 ? shaded / desktop_mpx : 0.0, Median(mcycles));
    if (Is_freehand(scenario)) {
        size_t const per = samples.size() / kFreehandBuckets;
        for (size_t b = 0; per > 0 && b < static_cast<size_t>(kFreehandBuckets); ++b) {
            std::vector<double> bc, by, bg, bm;
            for (size_t k = b * per; k < (b + 1) * per; ++k) {
                bc.push_back(samples[k].cpu_ms);
                by.push_back(samples[k].cpu_mcycles);
                if (samples[k].gpu_ms >= 0.0) bg.push_back(samples[k].gpu_ms);
                bm.push_back(samples[k].mpx);
            }
            Print(
                "points {:5}-{:5}  cpu p50 {:6.2f} ({:.1f} Mcycles) | gpu p50 {:>6} | "
                "{:.1f} Mpx\n",
                samples[b * per].points, samples[(b + 1) * per - 1].points, Median(bc),
                Median(by), bg.empty() ? std::string("n/a") : Csv_double(Median(bg)),
                Median(bm));
        }
    }
    if (move_release_ms.has_value()) {
        Print("release: cpu {:.2f} ms ({:.1f} Mcycles), frame {:.2f} ms\n",
              move_release_ms->first,
              static_cast<double>(move_release_cycles) / kCyclesPerMegacycle,
              move_release_ms->second);
    }
    if (commit_ms.has_value()) {
        Print("release: {} committed points, cpu {:.2f} ms ({:.1f} Mcycles), frame "
              "{:.2f} ms\n",
              commit_points, commit_ms->first,
              static_cast<double>(commit_cycles) / kCyclesPerMegacycle,
              commit_ms->second);
        Print("rebuild (undo/redo): cpu {:.2f} ms ({:.1f} Mcycles), gpu done {:.2f} "
              "ms\n",
              rebuild_ms.first,
              static_cast<double>(rebuild_cycles) / kCyclesPerMegacycle,
              rebuild_ms.second);
    }
    Print("verdict {} (frame p95 {:.2f} ms; budgets 33.3 / 16.7)\n",
          core::Frame_rate_verdict_label(verdict), frame_summary.p95_ms);
    return frame_summary.p95_ms;
}

int Run_render_bench(Args const &args) {
    std::optional<Layout> const layout = Find_layout(args.layout);
    if (!layout) {
        Print("unknown layout: {}\n", args.layout);
        return static_cast<int>(BenchExit::BadArgs);
    }
    std::vector<std::string_view> scenarios;
    if (args.scenario == "all") {
        scenarios.assign(kAllScenarios.begin(), kAllScenarios.end());
    } else if (std::find(kAllScenarios.begin(), kAllScenarios.end(), args.scenario) !=
                   kAllScenarios.end() ||
               args.scenario == "move" || args.scenario == "commit") {
        scenarios.push_back(args.scenario);
    } else {
        Print("unknown scenario: {}\n", args.scenario);
        return static_cast<int>(BenchExit::BadArgs);
    }
    // --dump-frame equal to --frames dumps a freehand scenario's release frame.
    std::string_view const dump_scenario =
        args.scenario == "all" ? std::string_view("select") : scenarios.front();
    if (args.dump_frame > args.frames ||
        (args.dump_frame == args.frames && !Is_freehand(dump_scenario) &&
         dump_scenario != "move")) {
        Print("--dump-frame {} is past --frames {} (equal is the release frame, brush, "
              "highlighter or move only)\n",
              args.dump_frame, args.frames);
        return static_cast<int>(BenchExit::BadArgs);
    }

    // --dab draws its short stroke from the first frame's points.
    if (args.dab_points > args.points_per_frame) {
        Print("--dab {} is past --points-per-frame {}\n", args.dab_points,
              args.points_per_frame);
        return static_cast<int>(BenchExit::BadArgs);
    }
    if (args.dab_no_draft && args.dab_points == 0) {
        Print("--dab-no-draft needs --dab N\n");
        return static_cast<int>(BenchExit::BadArgs);
    }
    if ((args.lifted || args.lifted_as_screenshot) &&
        (!args.annotated || (args.lifted && args.lifted_as_screenshot))) {
        Print("--lifted and --lifted-as-screenshot need --annotated, one at a time\n");
        return static_cast<int>(BenchExit::BadArgs);
    }

    core::RectPx const vd = Union_of(layout->monitors_screen);
    std::vector<core::RectPx> const monitors_client =
        Monitors_client(layout->monitors_screen);

    D2DOverlayResources res;
    GpuQueries queries;
    bool targets_ok = Create_bench_device(res, args.adapter);
    if (targets_ok && args.split_monitors) {
        for (core::RectPx const &m : monitors_client) {
            targets_ok = targets_ok && res.Add_offscreen_target(m);
        }
    } else if (targets_ok) {
        targets_ok = res.Add_offscreen_target(
            core::RectPx::From_ltrb(0, 0, vd.Width(), vd.Height()));
    }
    if (!targets_ok || !Prepare_resources(res, vd.Width(), vd.Height()) ||
        ((args.lifted || args.lifted_as_screenshot) &&
         !Prepare_lifted(res, vd.Width(), vd.Height(), args)) ||
        !Create_queries(res.d3d_device.Get(), queries)) {
        Print("device setup failed\n");
        return static_cast<int>(BenchExit::DeviceFailed);
    }
    std::wstring const adapter_name = Adapter_name(res.d3d_device.Get());

    std::ofstream csv;
    if (!args.csv_path.empty()) {
        csv.open(args.csv_path);
        if (!csv) {
            Print("cannot write {}\n", args.csv_path);
            return static_cast<int>(BenchExit::FileError);
        }
        csv << "layout,adapter,scenario,smooth,run,frame,cpu_ms,gpu_ms,frame_ms,mpx,"
               "points\n";
    }

#if defined(_DEBUG)
    Print("build: DEBUG (D3D debug layer on when installed; numbers are not "
          "meaningful)\n");
#else
    Print("build: release\n");
#endif
    Print("targets: {}\n", args.split_monitors ? "one per monitor" : "one (desktop)");
    RunContext ctx{
        &res,          &queries,   &*layout,    &monitors_client,
        &adapter_name, vd.Width(), vd.Height(), csv.is_open() ? &csv : nullptr};
    for (std::string_view const scenario : scenarios) {
        std::vector<double> p95s;
        for (int run = 0; run < args.repeat; ++run) {
            bool const dump_here =
                args.dump_frame >= 0 && run == 0 && scenario == dump_scenario;
            std::optional<double> const p95 =
                Run_scenario(ctx, scenario, args, run, dump_here);
            if (!p95) {
                return static_cast<int>(BenchExit::PaintFailed);
            }
            p95s.push_back(*p95);
        }
        if (args.repeat > 1) {
            Print("\n{}: median of {} frame p95s = {:.2f} ms\n", scenario, args.repeat,
                  Median(p95s));
        }
    }
    return static_cast<int>(BenchExit::Ok);
}

// ---------------------------------------------------------------------------
// B2: present-floor probe
// ---------------------------------------------------------------------------

// Draws extra full-surface blits inside Paint_d2d_frame's BeginDraw/EndDraw, to
// mimic frames that cost several passes over the surface.
class ExtraPassesLayer final : public IOverlayTopLayer {
  public:
    ExtraPassesLayer(ID2D1Bitmap *bitmap, int extra_passes)
        : bitmap_(bitmap), extra_passes_(extra_passes) {}

    [[nodiscard]] bool Is_visible() const noexcept override {
        return extra_passes_ > 0;
    }
    [[nodiscard]] bool Paint_d2d(ID2D1RenderTarget *rt, IDWriteFactory *,
                                 ID2D1SolidColorBrush *) noexcept override {
        for (int p = 0; p < extra_passes_; ++p) {
            rt->DrawBitmap(bitmap_, nullptr, kExtraPassOpacity);
        }
        return true;
    }

  private:
    ID2D1Bitmap *bitmap_ = nullptr;
    int extra_passes_ = 0;
};

LRESULT CALLBACK Probe_window_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                   LPARAM lparam) noexcept {
    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

constexpr wchar_t kProbeWindowClass[] = L"greenflame_render_bench_probe";

HWND Create_probe_window(core::RectPx const &screen_rect) {
    HWND const hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kProbeWindowClass, L"",
        WS_POPUP, screen_rect.left, screen_rect.top, screen_rect.Width(),
        screen_rect.Height(), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd != nullptr) {
        (void)ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    return hwnd;
}

struct ProbeSurface final {
    ProbeSurface() = default;
    ~ProbeSurface() = default;
    ProbeSurface(ProbeSurface const &) = delete;
    ProbeSurface &operator=(ProbeSurface const &) = delete;
    ProbeSurface(ProbeSurface &&) noexcept = default;

    core::RectPx screen_rect = {};
    HWND hwnd = nullptr;
    std::unique_ptr<D2DOverlayResources> res = std::make_unique<D2DOverlayResources>();
    std::vector<core::RectPx> monitors_client = {};
    std::unique_ptr<ExtraPassesLayer> extra = {};
    std::wstring adapter_name = {};
};

// The DXGI adapter that drives the monitor at this rect, or null.
ComPtr<IDXGIAdapter1> Adapter_for_monitor(core::RectPx const &rect) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return nullptr;
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) !=
                     DXGI_ERROR_NOT_FOUND;
         ++i) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) !=
                         DXGI_ERROR_NOT_FOUND;
             ++o) {
            DXGI_OUTPUT_DESC od{};
            (void)output->GetDesc(&od);
            RECT const r = od.DesktopCoordinates;
            if (r.left == rect.left && r.top == rect.top && r.right == rect.right &&
                r.bottom == rect.bottom) {
                return adapter;
            }
        }
    }
    return nullptr;
}

bool Setup_probe_surface(ProbeSurface &surface, IDXGIAdapter *adapter, int passes) {
    surface.hwnd = Create_probe_window(surface.screen_rect);
    int const w = surface.screen_rect.Width();
    int const h = surface.screen_rect.Height();
    D2DOverlayResources &res = *surface.res;
    if (surface.hwnd == nullptr || !res.Initialize_factory() ||
        !res.Create_hwnd_rt(surface.hwnd, w, h, adapter) ||
        !Prepare_resources(res, w, h)) {
        return false;
    }
    Rebuild_frozen_bitmap(res, core::RectPx::From_ltrb(0, 0, w, h), w, h);
    surface.extra =
        std::make_unique<ExtraPassesLayer>(res.screenshot.Get(), passes - 1);
    surface.adapter_name = Adapter_name(res.d3d_device.Get());
    return true;
}

// One steady frame (frozen blit + extra passes) on every surface, each through
// Paint_d2d_frame's own waitable wait and Present(1, 0).
[[nodiscard]] bool Paint_probe_surfaces(std::vector<ProbeSurface> &surfaces) {
    for (ProbeSurface &s : surfaces) {
        D2DPaintInput input{};
        input.monitor_rects_client = s.monitors_client;
        input.final_selection = core::RectPx::From_ltrb(0, 0, s.screen_rect.Width(),
                                                        s.screen_rect.Height());
        if (Paint_d2d_frame(*s.res, input, s.screen_rect.Width(),
                            s.screen_rect.Height(), s.extra.get(),
                            kFrameWaitTimeoutMs) != D2DFrameStatus::Ok) {
            return false;
        }
    }
    return true;
}

// One loop = one call of paint_all, which paints one frame on every window.
std::optional<std::vector<double>> Probe_loop(std::function<bool()> const &paint_all,
                                              int frames) {
    std::vector<double> intervals;
    LARGE_INTEGER const start = Now();
    LARGE_INTEGER previous = start;
    for (int f = -kWarmupFrames; f < frames; ++f) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            (void)DispatchMessageW(&msg);
        }
        if (!paint_all()) {
            return std::nullopt;
        }
        LARGE_INTEGER const now = Now();
        if (f >= 0) {
            intervals.push_back(Qpc_ms(previous, now));
        }
        previous = now;
        if (Qpc_ms(start, now) > kProbeTimeLimitMs) {
            break;
        }
    }
    return intervals;
}

void Destroy_surfaces(std::vector<ProbeSurface> &surfaces) {
    for (ProbeSurface &s : surfaces) {
        // Swap chain and waitable go before their window.
        s.res->Release_all();
        if (s.hwnd != nullptr) {
            (void)DestroyWindow(s.hwnd);
        }
    }
    surfaces.clear();
}

// Prints one arrangement's frame intervals; false if a paint failed.
bool Report_intervals(std::string_view label, size_t surface_count,
                      std::optional<std::vector<double>> const &intervals) {
    if (!intervals) {
        Print("{}: paint failed\n", label);
        return false;
    }
    core::FrameTimeSummary const s = core::Summarize_frame_times(*intervals);
    double const fps = s.p50_ms > 0.0 ? kMsPerSecond / s.p50_ms : 0.0;
    Print("{}: {} surfaces, frame interval p50 {:.1f} p95 {:.1f} ms ({:.1f} fps), {} "
          "frames\n",
          label, surface_count, s.p50_ms, s.p95_ms, fps, s.count);
    return true;
}

bool Report_probe(std::string_view label, std::vector<ProbeSurface> &surfaces,
                  int frames) {
    if (!Report_intervals(
            label, surfaces.size(),
            Probe_loop([&surfaces] { return Paint_probe_surfaces(surfaces); },
                       frames))) {
        return false;
    }
    for (ProbeSurface const &surface : surfaces) {
        Print("    ({},{}) {}x{} on {}\n", surface.screen_rect.left,
              surface.screen_rect.top, surface.screen_rect.Width(),
              surface.screen_rect.Height(), Narrow(surface.adapter_name));
    }
    return true;
}

// One window and swap chain per monitor, all on one device (--shared-adapter)
// with one set of desktop-sized caches: each loop is one Paint_d2d_frame over every
// window's target, the per-monitor overlay's own path (gf-ojs.6, option 3a).
struct SharedProbe final {
    SharedProbe() = default;
    ~SharedProbe() = default;
    SharedProbe(SharedProbe const &) = delete;
    SharedProbe &operator=(SharedProbe const &) = delete;
    SharedProbe(SharedProbe &&) = delete;
    SharedProbe &operator=(SharedProbe &&) = delete;

    core::RectPx vd = {};
    std::vector<core::RectPx> monitors_client = {};
    std::vector<HWND> windows = {};
    std::unique_ptr<D2DOverlayResources> res = std::make_unique<D2DOverlayResources>();
    std::unique_ptr<ExtraPassesLayer> extra = {};
};

bool Setup_shared_probe(SharedProbe &probe, std::span<const core::RectPx> monitors,
                        int passes, AdapterChoice const &adapter) {
    D2DOverlayResources &res = *probe.res;
    if (!Create_bench_device(res, adapter)) {
        return false;
    }
    for (size_t i = 0; i < monitors.size() && i < probe.monitors_client.size(); ++i) {
        HWND const hwnd = Create_probe_window(monitors[i]);
        if (hwnd == nullptr) {
            return false;
        }
        // Listed first, so teardown destroys it on any failure below.
        probe.windows.push_back(hwnd);
        if (!res.Add_swap_chain_target(hwnd, probe.monitors_client[i])) {
            return false;
        }
    }
    // The caches come from the device context, which now targets a back buffer.
    if (!Prepare_resources(res, probe.vd.Width(), probe.vd.Height())) {
        return false;
    }
    Rebuild_frozen_bitmap(
        res, core::RectPx::From_ltrb(0, 0, probe.vd.Width(), probe.vd.Height()),
        probe.vd.Width(), probe.vd.Height());
    probe.extra = std::make_unique<ExtraPassesLayer>(res.screenshot.Get(), passes - 1);
    return true;
}

[[nodiscard]] bool Paint_shared_probe(SharedProbe &probe) {
    D2DPaintInput input{};
    input.monitor_rects_client = probe.monitors_client;
    input.final_selection =
        core::RectPx::From_ltrb(0, 0, probe.vd.Width(), probe.vd.Height());
    return Paint_d2d_frame(*probe.res, input, probe.vd.Width(), probe.vd.Height(),
                           probe.extra.get(),
                           kFrameWaitTimeoutMs) == D2DFrameStatus::Ok;
}

void Destroy_shared_probe(SharedProbe &probe) {
    // Swap chains and waitables go before their windows.
    probe.res->Release_all();
    for (HWND const hwnd : probe.windows) {
        (void)DestroyWindow(hwnd);
    }
    probe.windows.clear();
}

int Run_probe(Args const &args) {
    std::vector<core::RectPx> const monitors = Current_monitors();
    if (monitors.empty()) {
        Print("no monitors\n");
        return static_cast<int>(BenchExit::DeviceFailed);
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = Probe_window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kProbeWindowClass;
    if (RegisterClassW(&wc) == 0) {
        return static_cast<int>(BenchExit::DeviceFailed);
    }
    Print("present probe: passes={} frames={} (topmost windows for a few seconds)\n",
          args.passes, args.frames);

    // Span: one window over the virtual desktop on the default adapter (the app).
    core::RectPx const vd = Union_of(monitors);
    std::vector<core::RectPx> const monitors_client = Monitors_client(monitors);
    std::vector<ProbeSurface> surfaces(1);
    surfaces[0].screen_rect = vd;
    surfaces[0].monitors_client = monitors_client;
    bool ok = Setup_probe_surface(surfaces[0], nullptr, args.passes) &&
              Report_probe("span", surfaces, args.frames);
    Destroy_surfaces(surfaces);
    if (!ok) {
        return static_cast<int>(BenchExit::DeviceFailed);
    }

    // Per-monitor-shared: one window and swap chain per monitor on one device
    // (--shared-adapter, default what the app uses), with shared desktop-sized caches
    // (the planned app layout).
    {
        SharedProbe shared{};
        shared.vd = vd;
        shared.monitors_client = monitors_client;
        ok = Setup_shared_probe(shared, monitors, args.passes, args.shared_adapter) &&
             Report_intervals(
                 "per-monitor-shared", shared.windows.size(),
                 Probe_loop([&shared] { return Paint_shared_probe(shared); },
                            args.frames));
        if (ok) {
            for (D2DSurfaceTarget const &target : shared.res->targets) {
                Print("    ({},{}) {}x{}\n", target.rect_client.left + vd.left,
                      target.rect_client.top + vd.top, target.rect_client.Width(),
                      target.rect_client.Height());
            }
            Print("    all on {}\n",
                  Narrow(Adapter_name(shared.res->d3d_device.Get())));
        }
        Destroy_shared_probe(shared);
    }
    if (!ok) {
        return static_cast<int>(BenchExit::DeviceFailed);
    }

    // Per-monitor: one window and swap chain per monitor, each on the adapter
    // that drives it (option 3b: every cache would be duplicated per GPU).
    surfaces.resize(monitors.size());
    for (size_t i = 0; i < monitors.size() && ok; ++i) {
        surfaces[i].screen_rect = monitors[i];
        surfaces[i].monitors_client.push_back(
            core::RectPx::From_ltrb(0, 0, monitors[i].Width(), monitors[i].Height()));
        ComPtr<IDXGIAdapter1> const adapter = Adapter_for_monitor(monitors[i]);
        ok = Setup_probe_surface(surfaces[i], adapter.Get(), args.passes);
    }
    ok = ok && Report_probe("per-monitor", surfaces, args.frames);
    Destroy_surfaces(surfaces);
    return ok ? static_cast<int>(BenchExit::Ok)
              : static_cast<int>(BenchExit::DeviceFailed);
}

int Main(std::span<char *const> argv) {
    std::optional<Args> const args = Parse_args(argv);
    if (!args) {
        Print_usage();
        return static_cast<int>(BenchExit::BadArgs);
    }
    if (!args->diff_a.empty()) {
        return Run_diff(*args);
    }
    if (args->list_adapters) {
        return List_adapters();
    }
    if (args->probe_present) {
        return Run_probe(*args);
    }
    return Run_render_bench(*args);
}

} // namespace

} // namespace greenflame::bench

int main(int argc, char **argv) {
    // Console exe without the app manifest: opt in to the app's DPI mode first, or
    // monitor rects are virtualized and probe windows get DWM-stretched.
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // The driver sleeps while the GPU-done query is pending. At the default 15.6 ms
    // timer tick every frame time rounds up to a tick multiple (measured: steady
    // frames read 15.4 ms with 0.2 ms CPU and 6.4 ms GPU). 1 ms keeps them honest.
    (void)timeBeginPeriod(1);
    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage-in-container")
    std::span<char *const> const args(argv, static_cast<size_t>(argc));
    CLANG_WARN_IGNORE_POP()
    int const code = greenflame::bench::Main(args);
    (void)timeEndPeriod(1);
    return code;
}
