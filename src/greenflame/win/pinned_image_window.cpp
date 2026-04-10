#include "win/pinned_image_window.h"

#include "greenflame_core/app_config.h"
#include "greenflame_core/save_image_policy.h"
#include "win/save_image.h"

namespace {

constexpr wchar_t kPinnedImageWindowClass[] = L"GreenflamePinnedImage";
constexpr int kHaloPaddingPx = 8;
constexpr int kHaloCornerRadiusPx = 6;
constexpr float kZoomStepFactor = 1.1f;
constexpr float kMinScale = 0.25f;
constexpr float kMaxScale = 8.f;
constexpr int kMinOpacityPercent = 20;
constexpr int kMaxOpacityPercent = 100;
constexpr int kOpacityStepPercent = 10;
constexpr BYTE kHaloRed = 135;
constexpr BYTE kHaloGreen = 223;
constexpr BYTE kHaloBlue = 0;
constexpr BYTE kIdleOuterHaloAlpha = 40;
constexpr BYTE kIdleMiddleHaloAlpha = 72;
constexpr BYTE kIdleInnerHaloAlpha = 128;
constexpr BYTE kIdleStrokeAlpha = 168;
constexpr BYTE kActiveOuterHaloAlpha = 72;
constexpr BYTE kActiveMiddleHaloAlpha = 120;
constexpr BYTE kActiveInnerHaloAlpha = 176;
constexpr BYTE kActiveStrokeAlpha = 232;
constexpr int kOuterHaloWidthPx = 6;
constexpr int kMiddleHaloWidthPx = 4;
constexpr int kInnerHaloWidthPx = 2;
constexpr int kInnerStrokeWidthPx = 1;
constexpr float kArcQuarterSweepDeg = 90.f;
constexpr int kDegreesPerQuarterTurn = 90;
constexpr float kMinScaleDelta = 0.001f;
constexpr wchar_t kCopyPinnedImageFailedMessage[] =
    L"Failed to copy the pinned image to the clipboard.";
constexpr wchar_t kSavePinnedImageFailedMessage[] =
    L"Failed to save the pinned image to a file.";

enum MenuCommandId : UINT {
    CopyToClipboard = 1,
    SaveToFile = 2,
    RotateRight = 3,
    RotateLeft = 4,
    IncreaseOpacity = 5,
    DecreaseOpacity = 6,
    Close = 7,
};

[[nodiscard]] int Drain_wheel_ticks(int &remainder, int new_delta) noexcept {
    remainder += new_delta;
    int ticks = 0;
    while (remainder >= WHEEL_DELTA) {
        ++ticks;
        remainder -= WHEEL_DELTA;
    }
    while (remainder <= -WHEEL_DELTA) {
        --ticks;
        remainder += WHEEL_DELTA;
    }
    return ticks;
}

[[nodiscard]] bool Ensure_gdiplus() {
    static ULONG_PTR token = 0;
    static bool ok = false;
    if (!ok) {
        Gdiplus::GdiplusStartupInput input;
        ok = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }
    return ok;
}

[[nodiscard]] Gdiplus::Color Green_halo_color(BYTE alpha) {
    return Gdiplus::Color(alpha, kHaloRed, kHaloGreen, kHaloBlue);
}

[[nodiscard]] std::wstring Zero_padded_number(uint32_t value, size_t width) {
    std::wstring text = std::to_wstring(value);
    if (text.size() < width) {
        text.insert(text.begin(), width - text.size(), L'0');
    }
    return text;
}

[[nodiscard]] std::wstring Build_default_pin_name() {
    SYSTEMTIME time = {};
    GetLocalTime(&time);

    std::wstring name = L"greenflame-pin-";
    name += std::to_wstring(time.wYear);
    name += Zero_padded_number(time.wMonth, 2);
    name += Zero_padded_number(time.wDay, 2);
    name += L"-";
    name += Zero_padded_number(time.wHour, 2);
    name += Zero_padded_number(time.wMinute, 2);
    name += Zero_padded_number(time.wSecond, 2);
    return name;
}

[[nodiscard]] DWORD
Filter_index_for_format(greenflame::core::ImageSaveFormat format) noexcept {
    switch (format) {
    case greenflame::core::ImageSaveFormat::Png:
        return 1;
    case greenflame::core::ImageSaveFormat::Jpeg:
        return 2;
    case greenflame::core::ImageSaveFormat::Bmp:
        return 3;
    }
    return 1;
}

void Append_rounded_rect_path(Gdiplus::GraphicsPath &path, Gdiplus::RectF rect,
                              float radius) {
    float const diameter = radius * 2.f;
    float const half_turn = kArcQuarterSweepDeg * 2.f;
    float const three_quarter_turn = kArcQuarterSweepDeg * 3.f;
    path.Reset();
    path.AddArc(rect.X, rect.Y, diameter, diameter, half_turn, kArcQuarterSweepDeg);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter,
                three_quarter_turn, kArcQuarterSweepDeg);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter,
                diameter, 0.f, kArcQuarterSweepDeg);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter,
                kArcQuarterSweepDeg, kArcQuarterSweepDeg);
    path.CloseFigure();
}

[[nodiscard]] bool Quarter_turns_swap_dimensions(int quarter_turns) noexcept {
    return (quarter_turns & 1) != 0;
}

void Calculate_display_dimensions(greenflame::GdiCaptureResult const &capture,
                                  float scale, int quarter_turns, int &oriented_width,
                                  int &oriented_height) {
    float const clamped_scale = std::clamp(scale, kMinScale, kMaxScale);
    float const scaled_width_value = static_cast<float>(capture.width) * clamped_scale;
    float const scaled_height_value =
        static_cast<float>(capture.height) * clamped_scale;
    int const scaled_width =
        std::max(1, static_cast<int>(std::lround(scaled_width_value)));
    int const scaled_height =
        std::max(1, static_cast<int>(std::lround(scaled_height_value)));
    if (Quarter_turns_swap_dimensions(quarter_turns)) {
        oriented_width = scaled_height;
        oriented_height = scaled_width;
        return;
    }
    oriented_width = scaled_width;
    oriented_height = scaled_height;
}

void Draw_halo(Gdiplus::Graphics &graphics, Gdiplus::RectF image_rect, bool active) {
    struct HaloLayer final {
        int width_px = 0;
        BYTE alpha = 0;
    };

    std::array<HaloLayer, 3> const layers =
        active ? std::array<HaloLayer, 3>{{
                     {kOuterHaloWidthPx, kActiveOuterHaloAlpha},
                     {kMiddleHaloWidthPx, kActiveMiddleHaloAlpha},
                     {kInnerHaloWidthPx, kActiveInnerHaloAlpha},
                 }}
               : std::array<HaloLayer, 3>{{
                     {kOuterHaloWidthPx, kIdleOuterHaloAlpha},
                     {kMiddleHaloWidthPx, kIdleMiddleHaloAlpha},
                     {kInnerHaloWidthPx, kIdleInnerHaloAlpha},
                 }};

    for (HaloLayer const &layer : layers) {
        float const outset = static_cast<float>(layer.width_px) * 0.5f;
        Gdiplus::RectF const halo_rect(image_rect.X - outset, image_rect.Y - outset,
                                       image_rect.Width + (outset * 2.f),
                                       image_rect.Height + (outset * 2.f));
        Gdiplus::GraphicsPath path;
        Append_rounded_rect_path(path, halo_rect,
                                 static_cast<float>(kHaloCornerRadiusPx) + outset);
        Gdiplus::Pen pen(Green_halo_color(layer.alpha),
                         static_cast<Gdiplus::REAL>(layer.width_px));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&pen, &path);
    }

    Gdiplus::GraphicsPath stroke_path;
    Append_rounded_rect_path(stroke_path, image_rect,
                             static_cast<float>(kHaloCornerRadiusPx));
    Gdiplus::Pen stroke(
        Green_halo_color(active ? kActiveStrokeAlpha : kIdleStrokeAlpha),
        static_cast<Gdiplus::REAL>(kInnerStrokeWidthPx));
    stroke.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&stroke, &stroke_path);
}

[[nodiscard]] bool Draw_transformed_capture(Gdiplus::Graphics &graphics,
                                            greenflame::GdiCaptureResult const &capture,
                                            Gdiplus::RectF image_rect,
                                            int quarter_turns, BYTE opacity_alpha) {
    Gdiplus::Bitmap source_bitmap(capture.bitmap, nullptr);
    if (source_bitmap.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    float draw_width = image_rect.Width;
    float draw_height = image_rect.Height;
    if (Quarter_turns_swap_dimensions(quarter_turns)) {
        draw_width = image_rect.Height;
        draw_height = image_rect.Width;
    }

    Gdiplus::ImageAttributes image_attributes;
    float const alpha = static_cast<float>(opacity_alpha) / static_cast<float>(255);
    Gdiplus::ColorMatrix const color_matrix = {{
        {1.f, 0.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, 0.f, alpha, 0.f},
        {0.f, 0.f, 0.f, 0.f, 1.f},
    }};
    image_attributes.SetColorMatrix(&color_matrix, Gdiplus::ColorMatrixFlagsDefault,
                                    Gdiplus::ColorAdjustTypeBitmap);

    float const center_x = image_rect.X + (image_rect.Width * 0.5f);
    float const center_y = image_rect.Y + (image_rect.Height * 0.5f);
    graphics.TranslateTransform(center_x, center_y);
    graphics.RotateTransform(
        static_cast<Gdiplus::REAL>(quarter_turns * kDegreesPerQuarterTurn));
    Gdiplus::RectF draw_rect(-(draw_width * 0.5f), -(draw_height * 0.5f), draw_width,
                             draw_height);
    graphics.DrawImage(&source_bitmap, draw_rect, 0.f, 0.f,
                       static_cast<Gdiplus::REAL>(capture.width),
                       static_cast<Gdiplus::REAL>(capture.height), Gdiplus::UnitPixel,
                       &image_attributes);
    graphics.ResetTransform();
    return true;
}

struct LayeredBitmapSurface final {
    HDC screen_dc = nullptr;
    HDC memory_dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old_bitmap = nullptr;
    void *bits = nullptr;
    int width = 0;
    int height = 0;

    LayeredBitmapSurface() = default;
    ~LayeredBitmapSurface() {
        if (old_bitmap != nullptr && memory_dc != nullptr) {
            SelectObject(memory_dc, old_bitmap);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory_dc != nullptr) {
            DeleteDC(memory_dc);
        }
        if (screen_dc != nullptr) {
            ReleaseDC(nullptr, screen_dc);
        }
    }

    LayeredBitmapSurface(LayeredBitmapSurface const &) = delete;
    LayeredBitmapSurface &operator=(LayeredBitmapSurface const &) = delete;

    [[nodiscard]] bool Create(int surface_width, int surface_height) {
        width = surface_width;
        height = surface_height;
        screen_dc = GetDC(nullptr);
        if (screen_dc == nullptr) {
            return false;
        }

        memory_dc = CreateCompatibleDC(screen_dc);
        if (memory_dc == nullptr) {
            return false;
        }

        BITMAPINFOHEADER bmi = {};
        greenflame::Fill_bmi32_top_down(bmi, width, height);
        bitmap = CreateDIBSection(screen_dc, reinterpret_cast<BITMAPINFO *>(&bmi),
                                  DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap == nullptr || bits == nullptr) {
            return false;
        }

        old_bitmap = SelectObject(memory_dc, bitmap);
        if (old_bitmap == nullptr || old_bitmap == HGDI_ERROR) {
            return false;
        }

        std::fill_n(static_cast<uint8_t *>(bits),
                    static_cast<size_t>(greenflame::Row_bytes32(width)) *
                        static_cast<size_t>(height),
                    static_cast<uint8_t>(0));
        return true;
    }
};

} // namespace

namespace greenflame {

PinnedImageWindow::PinnedImageWindow(IPinnedImageWindowEvents *events,
                                     core::AppConfig *config)
    : events_(events), config_(config) {}

PinnedImageWindow::~PinnedImageWindow() {
    Destroy();
    capture_.Free();
}

bool PinnedImageWindow::Register_window_class(HINSTANCE hinstance) {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &PinnedImageWindow::Static_wnd_proc;
    window_class.hInstance = hinstance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kPinnedImageWindowClass;
    return RegisterClassExW(&window_class) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool PinnedImageWindow::Create(HINSTANCE hinstance, GdiCaptureResult &capture,
                               core::RectPx screen_rect) {
    if (Is_open() || !capture.Is_valid() || !Ensure_gdiplus()) {
        return false;
    }

    hinstance_ = hinstance;
    initial_screen_rect_ = screen_rect;
    Take_capture_ownership(capture);

    int image_width = 0;
    int image_height = 0;
    Calculate_display_dimensions(capture_, scale_, quarter_turns_clockwise_,
                                 image_width, image_height);
    int const window_width = image_width + (kHaloPaddingPx * 2);
    int const window_height = image_height + (kHaloPaddingPx * 2);

    HWND const hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kPinnedImageWindowClass, L"",
        WS_POPUP, screen_rect.left - kHaloPaddingPx, screen_rect.top - kHaloPaddingPx,
        window_width, window_height, nullptr, nullptr, hinstance_, this);
    if (hwnd == nullptr) {
        capture_.Free();
        hinstance_ = nullptr;
        return false;
    }

    (void)SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    active_ = true;
    if (!Refresh_layered_window()) {
        DestroyWindow(hwnd);
        return false;
    }

    ShowWindow(hwnd, SW_SHOW);
    Bring_to_front();
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    return true;
}

void PinnedImageWindow::Destroy() {
    if (!Is_open()) {
        return;
    }
    DestroyWindow(hwnd_);
}

bool PinnedImageWindow::Is_open() const noexcept {
    return hwnd_ != nullptr && IsWindow(hwnd_) != 0;
}

void PinnedImageWindow::Take_capture_ownership(GdiCaptureResult &capture) noexcept {
    capture_.Free();
    capture_.bitmap = capture.bitmap;
    capture_.width = capture.width;
    capture_.height = capture.height;
    capture.bitmap = nullptr;
    capture.width = 0;
    capture.height = 0;
}

void PinnedImageWindow::Bring_to_front() noexcept {
    if (!Is_open()) {
        return;
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);
}

void PinnedImageWindow::Set_active(bool active) {
    bool const effective_active = active || context_menu_visible_;
    if (active_ == effective_active) {
        return;
    }

    active_ = effective_active;
    (void)Refresh_layered_window();
}

core::PointPx PinnedImageWindow::Current_window_center_screen() const noexcept {
    RECT window_rect = {};
    if (!Is_open() || GetWindowRect(hwnd_, &window_rect) == 0) {
        return {initial_screen_rect_.left + (initial_screen_rect_.Width() / 2),
                initial_screen_rect_.top + (initial_screen_rect_.Height() / 2)};
    }

    return {window_rect.left + ((window_rect.right - window_rect.left) / 2),
            window_rect.top + ((window_rect.bottom - window_rect.top) / 2)};
}

bool PinnedImageWindow::Refresh_layered_window(
    std::optional<core::PointPx> preserve_center_screen) {
    if (!Is_open() || !capture_.Is_valid() || !Ensure_gdiplus()) {
        return false;
    }

    int content_width = 0;
    int content_height = 0;
    Calculate_display_dimensions(capture_, scale_, quarter_turns_clockwise_,
                                 content_width, content_height);
    int const window_width = content_width + (kHaloPaddingPx * 2);
    int const window_height = content_height + (kHaloPaddingPx * 2);

    core::PointPx top_left = {};
    if (preserve_center_screen.has_value()) {
        top_left = {preserve_center_screen->x - (window_width / 2),
                    preserve_center_screen->y - (window_height / 2)};
    } else {
        RECT current_rect = {};
        if (GetWindowRect(hwnd_, &current_rect) != 0) {
            top_left = {current_rect.left, current_rect.top};
        } else {
            top_left = {initial_screen_rect_.left - kHaloPaddingPx,
                        initial_screen_rect_.top - kHaloPaddingPx};
        }
    }

    LayeredBitmapSurface surface;
    if (!surface.Create(window_width, window_height)) {
        return false;
    }

    Gdiplus::Bitmap canvas(window_width, window_height,
                           static_cast<INT>(Row_bytes32(window_width)),
                           PixelFormat32bppPARGB, static_cast<BYTE *>(surface.bits));
    if (canvas.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    Gdiplus::Graphics graphics(&canvas);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::RectF const image_rect(static_cast<Gdiplus::REAL>(kHaloPaddingPx),
                                    static_cast<Gdiplus::REAL>(kHaloPaddingPx),
                                    static_cast<Gdiplus::REAL>(content_width),
                                    static_cast<Gdiplus::REAL>(content_height));
    Draw_halo(graphics, image_rect, active_);
    BYTE const opacity_alpha = static_cast<BYTE>(
        std::clamp(opacity_percent_, kMinOpacityPercent, kMaxOpacityPercent) * 255 /
        100);
    if (!Draw_transformed_capture(graphics, capture_, image_rect,
                                  quarter_turns_clockwise_, opacity_alpha)) {
        return false;
    }

    POINT source_point = {0, 0};
    SIZE surface_size = {window_width, window_height};
    POINT target_point = {top_left.x, top_left.y};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    return UpdateLayeredWindow(hwnd_, surface.screen_dc, &target_point, &surface_size,
                               surface.memory_dc, &source_point, 0, &blend,
                               ULW_ALPHA) != 0;
}

bool PinnedImageWindow::Build_export_capture(GdiCaptureResult &out) const {
    out.Free();
    if (!capture_.Is_valid() || !Ensure_gdiplus()) {
        return false;
    }

    int export_width = 0;
    int export_height = 0;
    Calculate_display_dimensions(capture_, 1.f, quarter_turns_clockwise_, export_width,
                                 export_height);
    if (!Create_solid_capture(export_width, export_height, RGB(0, 0, 0), out)) {
        return false;
    }

    HDC const screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        out.Free();
        return false;
    }

    bool ok = false;
    HDC const memory_dc = CreateCompatibleDC(screen_dc);
    if (memory_dc != nullptr) {
        HGDIOBJ const old_bitmap = SelectObject(memory_dc, out.bitmap);
        if (old_bitmap != nullptr && old_bitmap != HGDI_ERROR) {
            Gdiplus::Graphics graphics(memory_dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.Clear(Gdiplus::Color(255, 0, 0, 0));
            Gdiplus::RectF const image_rect(0.f, 0.f,
                                            static_cast<Gdiplus::REAL>(export_width),
                                            static_cast<Gdiplus::REAL>(export_height));
            ok = Draw_transformed_capture(graphics, capture_, image_rect,
                                          quarter_turns_clockwise_, 255);
            SelectObject(memory_dc, old_bitmap);
        }
        DeleteDC(memory_dc);
    }
    ReleaseDC(nullptr, screen_dc);

    if (!ok) {
        out.Free();
    }
    return ok;
}

void PinnedImageWindow::Copy_to_clipboard() {
    GdiCaptureResult export_capture = {};
    if (!Build_export_capture(export_capture)) {
        MessageBoxW(hwnd_, kCopyPinnedImageFailedMessage, L"Greenflame",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    bool const copied = Copy_capture_to_clipboard(export_capture, hwnd_);
    export_capture.Free();
    if (!copied) {
        MessageBoxW(hwnd_, kCopyPinnedImageFailedMessage, L"Greenflame",
                    MB_OK | MB_ICONWARNING);
    }
}

void PinnedImageWindow::Save_to_file() {
    GdiCaptureResult export_capture = {};
    if (!Build_export_capture(export_capture)) {
        MessageBoxW(hwnd_, kSavePinnedImageFailedMessage, L"Greenflame",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring const initial_dir = Resolve_initial_save_directory(config_);
    core::ImageSaveFormat const default_format =
        config_ != nullptr ? core::Image_save_format_from_config(*config_)
                           : core::ImageSaveFormat::Png;

    OPENFILENAMEW ofn = {};
    std::array<wchar_t, MAX_PATH> path_buffer = {};
    std::wstring const default_name_with_extension =
        Build_default_pin_name() +
        std::wstring(core::Extension_for_image_save_format(default_format));
    std::span<wchar_t> path_span(path_buffer);
    size_t const copied_chars =
        std::min(default_name_with_extension.size(), path_span.size() - 1);
    std::copy_n(default_name_with_extension.data(), copied_chars, path_span.begin());
    path_span[copied_chars] = L'\0';

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"PNG (*.png)\0*.png\0JPEG "
                      L"(*.jpg;*.jpeg)\0*.jpg;*.jpeg\0BMP (*.bmp)\0*.bmp\0\0";
    ofn.lpstrFile = path_buffer.data();
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.nFilterIndex = Filter_index_for_format(default_format);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!initial_dir.empty()) {
        ofn.lpstrInitialDir = initial_dir.c_str();
    }

    if (!GetSaveFileNameW(&ofn)) {
        export_capture.Free();
        return;
    }

    std::wstring const resolved_path = core::Ensure_image_save_extension(
        std::wstring_view(path_buffer.data()), ofn.nFilterIndex);
    size_t const resolved_chars = std::min(resolved_path.size(), path_span.size() - 1);
    std::copy_n(resolved_path.data(), resolved_chars, path_span.begin());
    path_span[resolved_chars] = L'\0';

    core::ImageSaveFormat const format =
        core::Detect_image_save_format_from_path(std::wstring_view(path_buffer.data()));
    bool const saved = Save_capture_to_file(export_capture, path_buffer.data(), format);
    export_capture.Free();
    if (!saved) {
        MessageBoxW(hwnd_, kSavePinnedImageFailedMessage, L"Greenflame",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    if (config_ != nullptr) {
        std::wstring_view const path_view(path_buffer.data());
        size_t const last_slash = path_view.rfind(L'\\');
        if (last_slash != std::wstring_view::npos) {
            config_->last_save_as_dir.assign(path_view.substr(0, last_slash));
            config_->Normalize();
        }
    }
}

void PinnedImageWindow::Rotate(int32_t delta_quarter_turns) {
    if (!capture_.Is_valid()) {
        return;
    }

    quarter_turns_clockwise_ = (quarter_turns_clockwise_ + delta_quarter_turns) % 4;
    if (quarter_turns_clockwise_ < 0) {
        quarter_turns_clockwise_ += 4;
    }
    (void)Refresh_layered_window(Current_window_center_screen());
}

void PinnedImageWindow::Zoom(int32_t delta_steps) {
    if (!capture_.Is_valid() || delta_steps == 0) {
        return;
    }

    float new_scale = scale_;
    if (delta_steps > 0) {
        for (int index = 0; index < delta_steps; ++index) {
            new_scale *= kZoomStepFactor;
        }
    } else {
        for (int index = 0; index < -delta_steps; ++index) {
            new_scale /= kZoomStepFactor;
        }
    }
    new_scale = std::clamp(new_scale, kMinScale, kMaxScale);
    if (std::abs(new_scale - scale_) < kMinScaleDelta) {
        return;
    }

    scale_ = new_scale;
    (void)Refresh_layered_window(Current_window_center_screen());
}

void PinnedImageWindow::Adjust_opacity(int32_t delta_steps) {
    if (delta_steps == 0) {
        return;
    }

    int const new_opacity =
        std::clamp(opacity_percent_ + (delta_steps * kOpacityStepPercent),
                   kMinOpacityPercent, kMaxOpacityPercent);
    if (new_opacity == opacity_percent_) {
        return;
    }

    opacity_percent_ = new_opacity;
    (void)Refresh_layered_window();
}

void PinnedImageWindow::Start_drag(core::PointPx cursor_screen) {
    if (!Is_open()) {
        return;
    }

    RECT window_rect = {};
    GetWindowRect(hwnd_, &window_rect);
    drag_offset_px_ = {cursor_screen.x - window_rect.left,
                       cursor_screen.y - window_rect.top};
    dragging_ = true;
    SetCapture(hwnd_);
}

void PinnedImageWindow::Update_drag(core::PointPx cursor_screen) {
    if (!dragging_ || !Is_open()) {
        return;
    }

    int const new_left = cursor_screen.x - drag_offset_px_.x;
    int const new_top = cursor_screen.y - drag_offset_px_.y;
    SetWindowPos(hwnd_, HWND_TOPMOST, new_left, new_top, 0, 0,
                 SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

void PinnedImageWindow::End_drag() noexcept {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    if (GetCapture() == hwnd_) {
        ReleaseCapture();
    }
}

void PinnedImageWindow::Show_context_menu(POINT screen_point) {
    if (!Is_open()) {
        return;
    }

    if (screen_point.x == -1 && screen_point.y == -1) {
        RECT window_rect = {};
        GetWindowRect(hwnd_, &window_rect);
        screen_point.x =
            window_rect.left + ((window_rect.right - window_rect.left) / 2);
        screen_point.y = window_rect.top + ((window_rect.bottom - window_rect.top) / 2);
    }

    HMENU const menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu, MF_STRING, CopyToClipboard, L"Copy to clipboard\tCtrl+C");
    AppendMenuW(menu, MF_STRING, SaveToFile, L"Save to file\tCtrl+S");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, RotateRight, L"Rotate Right\tCtrl+Right");
    AppendMenuW(menu, MF_STRING, RotateLeft, L"Rotate Left\tCtrl+Left");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT increase_opacity_flags = MF_STRING;
    if (opacity_percent_ >= kMaxOpacityPercent) {
        increase_opacity_flags |= MF_GRAYED;
    }
    AppendMenuW(menu, increase_opacity_flags, IncreaseOpacity,
                L"Increase Opacity\tCtrl+Up");

    UINT decrease_opacity_flags = MF_STRING;
    if (opacity_percent_ <= kMinOpacityPercent) {
        decrease_opacity_flags |= MF_GRAYED;
    }
    AppendMenuW(menu, decrease_opacity_flags, DecreaseOpacity,
                L"Decrease Opacity\tCtrl+Down");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Close, L"Close\tEsc");

    context_menu_visible_ = true;
    Set_active(true);
    SetForegroundWindow(hwnd_);
    int const command =
        TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                         screen_point.x, screen_point.y, hwnd_, nullptr);
    context_menu_visible_ = false;
    Set_active(GetForegroundWindow() == hwnd_ || GetActiveWindow() == hwnd_);

    switch (command) {
    case CopyToClipboard:
        Copy_to_clipboard();
        break;
    case SaveToFile:
        Save_to_file();
        break;
    case RotateRight:
        Rotate(1);
        break;
    case RotateLeft:
        Rotate(-1);
        break;
    case IncreaseOpacity:
        Adjust_opacity(1);
        break;
    case DecreaseOpacity:
        Adjust_opacity(-1);
        break;
    case Close:
        Destroy();
        break;
    default:
        break;
    }

    DestroyMenu(menu);
}

LRESULT CALLBACK PinnedImageWindow::Static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                                    LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW const *create = reinterpret_cast<CREATESTRUCTW const *>(lparam);
        auto *self = reinterpret_cast<PinnedImageWindow *>(create->lpCreateParams);
        if (self == nullptr) {
            return FALSE;
        }
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    auto *self =
        reinterpret_cast<PinnedImageWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    LRESULT const result = self->Wnd_proc(msg, wparam, lparam);
    if (msg == WM_NCDESTROY) {
        self->hwnd_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self->events_ != nullptr) {
            self->events_->On_pinned_image_window_closed(self);
        }
    }
    return result;
}

LRESULT PinnedImageWindow::Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE) {
            Set_active(true);
        } else if (!context_menu_visible_) {
            Set_active(false);
        }
        return 0;
    case WM_SETFOCUS:
        Set_active(true);
        return 0;
    case WM_KILLFOCUS:
        if (!context_menu_visible_) {
            Set_active(false);
        }
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return TRUE;
    case WM_LBUTTONDOWN: {
        Bring_to_front();
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
        POINT screen_point = {};
        GetCursorPos(&screen_point);
        Start_drag({screen_point.x, screen_point.y});
        return 0;
    }
    case WM_MOUSEMOVE:
        if (dragging_) {
            POINT screen_point = {};
            GetCursorPos(&screen_point);
            Update_drag({screen_point.x, screen_point.y});
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        End_drag();
        return 0;
    case WM_CONTEXTMENU: {
        POINT screen_point = {static_cast<LONG>(static_cast<short>(LOWORD(lparam))),
                              static_cast<LONG>(static_cast<short>(HIWORD(lparam)))};
        Show_context_menu(screen_point);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int const steps = Drain_wheel_ticks(mouse_wheel_delta_remainder_,
                                            GET_WHEEL_DELTA_WPARAM(wparam));
        if (steps != 0) {
            Zoom(steps);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        bool const ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool const is_repeat = (lparam & (static_cast<LPARAM>(1) << 30)) != 0;
        if (wparam == VK_ESCAPE && !is_repeat) {
            Destroy();
            return 0;
        }
        if (!ctrl) {
            return 0;
        }
        switch (wparam) {
        case L'C':
            if (!is_repeat) {
                Copy_to_clipboard();
            }
            return 0;
        case L'S':
            if (!is_repeat) {
                Save_to_file();
            }
            return 0;
        case VK_RIGHT:
            if (!is_repeat) {
                Rotate(1);
            }
            return 0;
        case VK_LEFT:
            if (!is_repeat) {
                Rotate(-1);
            }
            return 0;
        case VK_UP:
            Adjust_opacity(1);
            return 0;
        case VK_DOWN:
            Adjust_opacity(-1);
            return 0;
        case VK_OEM_PLUS:
        case VK_ADD:
            if (!is_repeat) {
                Zoom(1);
            }
            return 0;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            if (!is_repeat) {
                Zoom(-1);
            }
            return 0;
        default:
            return 0;
        }
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        BeginPaint(hwnd_, &paint);
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        End_drag();
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

} // namespace greenflame
