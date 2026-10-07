// greenflame_render_bench: drives the real overlay paint code (Paint_d2d_frame)
// with synthetic input and reports per-frame cost. See docs/testing.md,
// "Overlay performance".
//
// B1 (default): offscreen render bench. CPU, GPU and frame time per frame on a
//     chosen monitor layout and adapter. No window, no present.
// B2 (--probe-present): present floor. Real topmost windows on the real
//     monitors, same swap chain as the app, frame interval per loop.
//
// Known gaps against OverlayWindow::On_paint (keep this list honest):
// - no toolbar buttons or glyphs, so frames with a selection are slightly cheaper
//   than in the app;
// - no Alt-free snapping of the crosshair cursor (CPU only, not paint);
// - no committed annotations, selection wheel, text drafts or obfuscate previews.

#include "greenflame/win/d2d_overlay_resources.h"
#include "greenflame/win/d2d_paint.h"
#include "greenflame/win/overlay_top_layer.h"
#include "greenflame_core/bmp.h"
#include "greenflame_core/frame_stats.h"
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
constexpr COLORREF kBrushColor = RGB(255, 0, 0);
constexpr COLORREF kHighlighterColor = RGB(255, 255, 0);
constexpr uint32_t kNoiseSeed = 0x12345678u;
constexpr uint32_t kLcgMultiplier = 1664525u;
constexpr uint32_t kLcgIncrement = 1013904223u;
constexpr uint32_t kNoiseShift = 8u;
constexpr uint32_t kOpaqueAlpha = 0xFF000000u;
constexpr double kMsPerSecond = 1000.0;
constexpr double kMegapixel = 1e6;
constexpr int kBytesPerPixel = 4;
constexpr float kExtraPassOpacity = 0.5f;
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

struct Args final {
    std::string layout = "jocelyn-desk";
    AdapterKind adapter_kind = AdapterKind::Default;
    UINT adapter_index = 0;
    std::string scenario = "all";
    bool smooth = true;
    int step_px = kDefaultStepPx;
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
    bool list_adapters = false;
    bool probe_present = false;
};

void Print_usage() {
    Print(
        "usage: greenflame_render_bench [options]\n"
        "  --layout jocelyn-desk|4k-desk|single-1080|current   (default jocelyn-desk)\n"
        "  --adapter default|N|warp     D3D adapter (default: what the app uses)\n"
        "  --list-adapters              print DXGI adapters and exit\n"
        "  --scenario hover|select|brush|highlighter|steady|all   (default all)\n"
        "  --smooth on|off              freehand smoothing (default on)\n"
        "  --step PX                    px between freehand points (default 8)\n"
        "  --frames N                   measured frames per run (default 300)\n"
        "  --repeat N                   runs per scenario (default 1)\n"
        "  --csv FILE                   per-frame rows\n"
        "  --dump-frame K FILE.bmp      write frame K (run 1) as BMP; with 'all', from "
        "select\n"
        "  --diff A.bmp B.bmp [--tolerance T]   compare two dumps\n"
        "  --probe-present [--passes N] [--frames N]   present floor on real "
        "monitors\n");
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
            if (*v == "default") {
                args.adapter_kind = AdapterKind::Default;
            } else if (*v == "warp") {
                args.adapter_kind = AdapterKind::Warp;
            } else {
                std::optional<int> const index = Parse_int(*v);
                if (!index) return std::nullopt;
                args.adapter_kind = AdapterKind::Index;
                args.adapter_index = static_cast<UINT>(*index);
            }
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

// Deterministic noise, so GPU colour compression cannot make blits cheap.
HBITMAP Make_noise_bitmap(int width, int height) {
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
    uint32_t state = kNoiseSeed;
    for (uint32_t &px : pixels) {
        state = state * kLcgMultiplier + kLcgIncrement;
        px = (state >> kNoiseShift) | kOpaqueAlpha;
    }
    return bitmap;
}

// Screenshot, shared resources and cache targets, as OverlayWindow sets them up.
bool Prepare_resources(D2DOverlayResources &res, int width, int height) {
    GdiCaptureResult capture{};
    capture.bitmap = Make_noise_bitmap(width, height);
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

bool Create_bench_device(D2DOverlayResources &res, Args const &args) {
    if (!res.Initialize_factory()) {
        return false;
    }
    switch (args.adapter_kind) {
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
        factory->EnumAdapters1(args.adapter_index, adapter.GetAddressOf()) != S_OK) {
        Print("no adapter {} (see --list-adapters)\n", args.adapter_index);
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
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

// Copies the current target to a CPU-readable bitmap and writes it as a bottom-up
// BMP (Build_bmp_bytes expects BMP row order, tightly packed).
bool Dump_target(D2DOverlayResources &res, int width, int height,
                 std::string const &path) {
    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    bp.dpiX = res.Target_dpi();
    bp.dpiY = res.Target_dpi();
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> readback;
    D2D1_SIZE_U const size =
        D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height));
    if (FAILED(
            res.hwnd_rt->CreateBitmap(size, nullptr, 0, bp, readback.GetAddressOf())) ||
        FAILED(
            readback->CopyFromBitmap(nullptr, res.back_buffer_bitmap.Get(), nullptr))) {
        return false;
    }
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return false;
    }
    size_t const row_bytes = static_cast<size_t>(width) * kBytesPerPixel;
    size_t const rows = static_cast<size_t>(height);
    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage-in-container")
    std::span<const uint8_t> const source(mapped.bits,
                                          static_cast<size_t>(mapped.pitch) * rows);
    CLANG_WARN_IGNORE_POP()
    std::vector<uint8_t> packed(row_bytes * rows);
    for (size_t y = 0; y < rows; ++y) {
        auto const from = source.begin() + static_cast<std::ptrdiff_t>(
                                               y * static_cast<size_t>(mapped.pitch));
        auto const to =
            packed.begin() + static_cast<std::ptrdiff_t>((rows - 1 - y) * row_bytes);
        std::copy_n(from, row_bytes, to);
    }
    (void)readback->Unmap();
    std::vector<uint8_t> const bmp =
        core::Build_bmp_bytes(packed, width, height, static_cast<int>(row_bytes));
    return !bmp.empty() && Write_file(path, bmp);
}

int Run_diff(Args const &args) {
    std::optional<std::vector<uint8_t>> const a = Read_file(args.diff_a);
    std::optional<std::vector<uint8_t>> const b = Read_file(args.diff_b);
    if (!a || !b) {
        Print("diff: cannot read {} or {}\n", args.diff_a, args.diff_b);
        return static_cast<int>(BenchExit::FileError);
    }
    std::optional<core::BmpDiff> const diff =
        core::Diff_bmp_bytes(*a, *b, args.tolerance);
    if (!diff) {
        Print("diff: not comparable (not 32bpp BMPs, or sizes differ)\n");
        return static_cast<int>(BenchExit::FileError);
    }
    Print("diff: {} pixels differ, {} over tolerance {}, max channel delta {}\n",
          diff->differing_pixels, diff->pixels_over_tolerance, args.tolerance,
          diff->max_channel_delta);
    return diff->pixels_over_tolerance == 0 ? static_cast<int>(BenchExit::Ok)
                                            : static_cast<int>(BenchExit::PixelsDiffer);
}

// ---------------------------------------------------------------------------
// B1: scenarios
// ---------------------------------------------------------------------------

constexpr std::array<std::string_view, 5> kAllScenarios = {
    {"hover", "select", "brush", "highlighter", "steady"}};

bool Is_freehand(std::string_view scenario) {
    return scenario == "brush" || scenario == "highlighter";
}

struct ScenarioState final {
    std::vector<core::PointPx> points = {};
};

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
        input.dragging = true;
        input.live_rect = core::RectPx::From_ltrb(kSelectStartPx, kSelectStartPx,
                                                  std::max(x, kSelectStartPx + 1),
                                                  std::max(y, kSelectStartPx + 1));
        input.cursor_client_px = {x, y};
        input.selection_drag_corner_guide_px =
            core::PointPx{input.live_rect.right, input.live_rect.bottom};
    } else if (Is_freehand(scenario) && frame >= 0) {
        // One new input point per frame, as the app gets today.
        core::RectPx const sel = input.final_selection;
        double const travelled = static_cast<double>(kFreehandEdgeInsetPx) +
                                 static_cast<double>(i) * args.step_px;
        double const span = static_cast<double>(sel.Width() - 2 * kFreehandEdgeInsetPx);
        double const x = static_cast<double>(sel.left + kFreehandEdgeInsetPx) +
                         std::fmod(travelled, span);
        double const row = std::floor(travelled / span);
        double const y =
            static_cast<double>(sel.top + sel.bottom) / 2.0 +
            std::sin(static_cast<double>(i) * kFreehandWavePerFrame) *
                static_cast<double>(sel.Height() / kFreehandAmplitudeDivisor) +
            row * kFreehandRowStepPx;
        state.points.push_back({static_cast<int32_t>(x), static_cast<int32_t>(y)});
        input.draft_freehand_points = state.points;
        input.cursor_client_px = state.points.back();
    }
}

void Set_scenario_base(std::string_view scenario, int width, int height,
                       Args const &args, D2DPaintInput &input) {
    if (scenario == "steady" || Is_freehand(scenario)) {
        input.final_selection = core::RectPx::From_ltrb(0, 0, width, height);
        input.cursor_client_px = {width / 2, height / 2};
    }
    if (Is_freehand(scenario)) {
        bool const highlighter = scenario == "highlighter";
        core::StrokeStyle style{};
        style.width_px = highlighter ? kHighlighterWidthPx : kBrushWidthPx;
        style.color = highlighter ? kHighlighterColor : kBrushColor;
        input.draft_freehand_style = style;
        input.draft_freehand_tip_shape = highlighter ? core::FreehandTipShape::Square
                                                     : core::FreehandTipShape::Round;
        input.draft_freehand_smoothing_mode = args.smooth
                                                  ? core::FreehandSmoothingMode::Smooth
                                                  : core::FreehandSmoothingMode::Off;
        input.draft_freehand_blit_opacity =
            highlighter ? static_cast<float>(core::kDefaultHighlighterOpacityPercent) /
                              static_cast<float>(core::StrokeStyle::kMaxOpacityPercent)
                        : 1.0f;
    }
}

struct FrameSample final {
    double cpu_ms = 0.0;
    double gpu_ms = -1.0; // negative: timestamps were disjoint
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

// Runs one scenario once. Returns the frame-time p95, or nullopt on paint failure.
std::optional<double> Run_scenario(RunContext &ctx, std::string_view scenario,
                                   Args const &args, int run, bool dump_here) {
    D2DPaintInput input{};
    input.monitor_rects_client = *ctx.monitors_client;
    Set_scenario_base(scenario, ctx.width, ctx.height, args, input);
    ctx.res->frozen_valid = false;
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
        LARGE_INTEGER const t0 = Now();
        // Mirror OverlayWindow::On_paint: rebuild invalid caches before the frame.
        if (!ctx.res->annotations_valid) {
            Rebuild_annotations_bitmap(*ctx.res, {});
        }
        if (!ctx.res->frozen_valid) {
            Rebuild_frozen_bitmap(*ctx.res, input.final_selection, ctx.width,
                                  ctx.height);
        }
        bool const ok =
            Paint_d2d_frame(*ctx.res, input, ctx.width, ctx.height, nullptr);
        LARGE_INTEGER const t1 = Now();
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
        sample.frame_ms = Qpc_ms(t0, t2);
        if (dj.Disjoint == FALSE && dj.Frequency != 0 && ts1 >= ts0) {
            sample.gpu_ms = static_cast<double>(ts1 - ts0) * kMsPerSecond /
                            static_cast<double>(dj.Frequency);
        }
        sample.mpx = static_cast<double>(ps.PSInvocations) / kMegapixel;
        sample.points = state.points.size();
        samples.push_back(sample);
        // Readback happens after the frame's numbers are taken, outside timing.
        if (dump_here && f == args.dump_frame) {
            if (!Dump_target(*ctx.res, ctx.width, ctx.height, args.dump_path)) {
                Print("dump: failed to write {}\n", args.dump_path);
                return std::nullopt;
            }
            Print("dump: frame {} of {} written to {}\n", f, scenario, args.dump_path);
        }
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

    Print("\nlayout={} vd={}x{} adapter=\"{}\" scenario={} smooth={} frames={} "
          "run={}/{}\n",
          ctx.layout->name, ctx.width, ctx.height, Narrow(*ctx.adapter_name), scenario,
          args.smooth ? "on" : "off", args.frames, run + 1, args.repeat);
    Print("           p50     p90     p95     p99     max  <=33.3  <=16.7\n");
    Print_row("cpu", core::Summarize_frame_times(cpu));
    if (gpu.empty()) {
        Print("gpu    n/a (timestamps disjoint)\n");
    } else {
        Print_row("gpu", core::Summarize_frame_times(gpu));
    }
    Print_row("frame", frame_summary);
    Print("shaded p50 {:.1f} Mpx ({:.2f} x desktop)\n", shaded,
          desktop_mpx > 0.0 ? shaded / desktop_mpx : 0.0);
    if (Is_freehand(scenario)) {
        size_t const per = samples.size() / kFreehandBuckets;
        for (size_t b = 0; per > 0 && b < static_cast<size_t>(kFreehandBuckets); ++b) {
            std::vector<double> bc, bg, bm;
            for (size_t k = b * per; k < (b + 1) * per; ++k) {
                bc.push_back(samples[k].cpu_ms);
                if (samples[k].gpu_ms >= 0.0) bg.push_back(samples[k].gpu_ms);
                bm.push_back(samples[k].mpx);
            }
            Print("points {:5}-{:5}  cpu p50 {:6.2f} | gpu p50 {:>6} | {:.1f} Mpx\n",
                  samples[b * per].points, samples[(b + 1) * per - 1].points,
                  Median(bc), bg.empty() ? std::string("n/a") : Csv_double(Median(bg)),
                  Median(bm));
        }
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
               kAllScenarios.end()) {
        scenarios.push_back(args.scenario);
    } else {
        Print("unknown scenario: {}\n", args.scenario);
        return static_cast<int>(BenchExit::BadArgs);
    }
    if (args.dump_frame >= args.frames) {
        Print("--dump-frame {} is past --frames {}\n", args.dump_frame, args.frames);
        return static_cast<int>(BenchExit::BadArgs);
    }

    core::RectPx const vd = Union_of(layout->monitors_screen);
    std::vector<core::RectPx> monitors_client;
    for (core::RectPx const &m : layout->monitors_screen) {
        monitors_client.push_back(core::RectPx::From_ltrb(
            m.left - vd.left, m.top - vd.top, m.right - vd.left, m.bottom - vd.top));
    }

    D2DOverlayResources res;
    GpuQueries queries;
    if (!Create_bench_device(res, args) ||
        !res.Create_offscreen_target(vd.Width(), vd.Height()) ||
        !Prepare_resources(res, vd.Width(), vd.Height()) ||
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
    RunContext ctx{
        &res,          &queries,   &*layout,    &monitors_client,
        &adapter_name, vd.Width(), vd.Height(), csv.is_open() ? &csv : nullptr};
    std::string_view const dump_scenario =
        args.scenario == "all" ? std::string_view("select") : scenarios.front();
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

// One loop = one steady frame (frozen blit + extra passes) on every surface, each
// through Paint_d2d_frame's own waitable wait and Present(1, 0).
std::optional<std::vector<double>> Probe_loop(std::vector<ProbeSurface> &surfaces,
                                              int frames) {
    std::vector<double> intervals;
    LARGE_INTEGER const start = Now();
    LARGE_INTEGER previous = start;
    for (int f = -kWarmupFrames; f < frames; ++f) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            (void)DispatchMessageW(&msg);
        }
        for (ProbeSurface &s : surfaces) {
            D2DPaintInput input{};
            input.monitor_rects_client = s.monitors_client;
            input.final_selection = core::RectPx::From_ltrb(0, 0, s.screen_rect.Width(),
                                                            s.screen_rect.Height());
            if (!Paint_d2d_frame(*s.res, input, s.screen_rect.Width(),
                                 s.screen_rect.Height(), s.extra.get())) {
                return std::nullopt;
            }
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

bool Report_probe(std::string_view label, std::vector<ProbeSurface> &surfaces,
                  int frames) {
    std::optional<std::vector<double>> const intervals = Probe_loop(surfaces, frames);
    if (!intervals) {
        Print("{}: paint failed\n", label);
        return false;
    }
    core::FrameTimeSummary const s = core::Summarize_frame_times(*intervals);
    double const fps = s.p50_ms > 0.0 ? kMsPerSecond / s.p50_ms : 0.0;
    Print("{}: {} surfaces, frame interval p50 {:.1f} p95 {:.1f} ms ({:.1f} fps), {} "
          "frames\n",
          label, surfaces.size(), s.p50_ms, s.p95_ms, fps, s.count);
    for (ProbeSurface const &surface : surfaces) {
        Print("    ({},{}) {}x{} on {}\n", surface.screen_rect.left,
              surface.screen_rect.top, surface.screen_rect.Width(),
              surface.screen_rect.Height(), Narrow(surface.adapter_name));
    }
    return true;
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
    std::vector<ProbeSurface> surfaces(1);
    surfaces[0].screen_rect = vd;
    for (core::RectPx const &m : monitors) {
        surfaces[0].monitors_client.push_back(core::RectPx::From_ltrb(
            m.left - vd.left, m.top - vd.top, m.right - vd.left, m.bottom - vd.top));
    }
    bool ok = Setup_probe_surface(surfaces[0], nullptr, args.passes) &&
              Report_probe("span", surfaces, args.frames);
    Destroy_surfaces(surfaces);
    if (!ok) {
        return static_cast<int>(BenchExit::DeviceFailed);
    }

    // Per-monitor: one window and swap chain per monitor, each on the adapter
    // that drives it (no app equivalent yet; this is the layout step 3 would ship).
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
