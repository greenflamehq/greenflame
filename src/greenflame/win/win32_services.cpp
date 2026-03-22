#include "win/win32_services.h"

#include "greenflame/win/d2d_text_layout_engine.h"
#include "greenflame_core/string_utils.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/save_image.h"
#include "win/wgc_window_capture.h"

namespace {

constexpr std::array<unsigned char, 3> kUtf8BomBytes = {{0xEFu, 0xBBu, 0xBFu}};

struct WindowSearchState {
    std::wstring_view needle = {};
    std::vector<greenflame::WindowMatch> matches = {};
    greenflame::IWindowQuery const *window_query = nullptr;
    bool had_exception = false;
};

struct MinimizedWindowSearchState {
    std::wstring_view needle = {};
    size_t match_count = 0;
    bool had_exception = false;
};

[[nodiscard]] bool Is_window_cloaked(HWND hwnd) noexcept {
    DWORD cloaked = 0;
    HRESULT const hr =
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

[[nodiscard]] bool Is_searchable_top_level_window(HWND hwnd,
                                                  bool allow_minimized) noexcept {
    if (IsWindow(hwnd) == 0 || IsWindowVisible(hwnd) == 0 ||
        GetParent(hwnd) != nullptr || Is_window_cloaked(hwnd)) {
        return false;
    }

    return allow_minimized || IsIconic(hwnd) == 0;
}

[[nodiscard]] std::optional<greenflame::core::WindowCandidateInfo>
Try_get_window_candidate_info(HWND hwnd, greenflame::IWindowQuery const *window_query,
                              bool allow_minimized) {
    if (!Is_searchable_top_level_window(hwnd, allow_minimized)) {
        return std::nullopt;
    }

    greenflame::core::RectPx rect = {};
    if (!allow_minimized) {
        if (window_query == nullptr) {
            return std::nullopt;
        }

        std::optional<greenflame::core::RectPx> const queried_rect =
            window_query->Get_window_rect(hwnd);
        if (!queried_rect.has_value()) {
            return std::nullopt;
        }
        rect = *queried_rect;
    }

    int const title_len = GetWindowTextLengthW(hwnd);
    std::wstring title = {};
    if (title_len > 0) {
        title.resize(static_cast<size_t>(title_len) + 1u);
        int const copied =
            GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
        if (copied <= 0) {
            return std::nullopt;
        }
        title.resize(static_cast<size_t>(copied));
    }

    wchar_t class_name_buffer[256] = {};
    int const class_name_len = GetClassNameW(hwnd, class_name_buffer, 256);
    std::wstring class_name = {};
    if (class_name_len > 0) {
        class_name.assign(class_name_buffer, static_cast<size_t>(class_name_len));
    }

    greenflame::core::WindowCandidateInfo info{};
    info.title = std::move(title);
    info.class_name = std::move(class_name);
    info.rect = rect;
    info.hwnd_value = reinterpret_cast<std::uintptr_t>(hwnd);
    return info;
}

BOOL CALLBACK Enum_windows_by_title_proc(HWND hwnd, LPARAM lparam) noexcept {
    auto *state = reinterpret_cast<WindowSearchState *>(lparam);
    if (state == nullptr) {
        return FALSE;
    }

    try {
        std::optional<greenflame::core::WindowCandidateInfo> const info =
            Try_get_window_candidate_info(hwnd, state->window_query, false);
        if (!info.has_value() || info->title.empty()) {
            return TRUE;
        }

        std::wstring const &title = info->title;
        if (!greenflame::core::Contains_no_case(title, state->needle)) {
            return TRUE;
        }

        state->matches.push_back(greenflame::WindowMatch{*info, hwnd});
        return TRUE;
    } catch (...) {
        state->had_exception = true;
        return FALSE;
    }
}

BOOL CALLBACK Enum_minimized_windows_by_title_proc(HWND hwnd, LPARAM lparam) noexcept {
    auto *state = reinterpret_cast<MinimizedWindowSearchState *>(lparam);
    if (state == nullptr) {
        return FALSE;
    }

    try {
        if (IsIconic(hwnd) == 0) {
            return TRUE;
        }

        std::optional<greenflame::core::WindowCandidateInfo> const info =
            Try_get_window_candidate_info(hwnd, nullptr, true);
        if (!info.has_value() || info->title.empty()) {
            return TRUE;
        }

        if (!greenflame::core::Contains_no_case(info->title, state->needle) ||
            greenflame::core::Is_cli_invocation_window(*info, state->needle)) {
            return TRUE;
        }

        ++state->match_count;
        return TRUE;
    } catch (...) {
        state->had_exception = true;
        return FALSE;
    }
}

[[nodiscard]] greenflame::core::RectPx
Capture_rect_from_screen_rect(greenflame::core::RectPx screen_rect,
                              greenflame::core::RectPx virtual_bounds) {
    return greenflame::core::RectPx::From_ltrb(screen_rect.left - virtual_bounds.left,
                                               screen_rect.top - virtual_bounds.top,
                                               screen_rect.right - virtual_bounds.left,
                                               screen_rect.bottom - virtual_bounds.top);
}

[[nodiscard]] bool
Try_compute_render_sizes(greenflame::core::CaptureSaveRequest const &request,
                         int32_t &source_width, int32_t &source_height,
                         int32_t &output_width, int32_t &output_height) noexcept {
    if (!request.source_rect_screen.Try_get_size(source_width, source_height)) {
        return false;
    }
    return request.padding_px.Try_expand_size(source_width, source_height, output_width,
                                              output_height);
}

[[nodiscard]] bool Try_compute_offset(int32_t clipped_start, int32_t source_start,
                                      int32_t &offset) noexcept {
    int64_t const offset64 =
        static_cast<int64_t>(clipped_start) - static_cast<int64_t>(source_start);
    if (offset64 < 0 || offset64 > static_cast<int64_t>(INT32_MAX)) {
        return false;
    }
    offset = static_cast<int32_t>(offset64);
    return true;
}

[[nodiscard]] std::wstring Trim_trailing_wspace(std::wstring text) {
    while (!text.empty() && std::iswspace(text.back()) != 0) {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] std::wstring Format_windows_error_message(DWORD error) {
    if (error == 0) {
        return {};
    }

    LPWSTR buffer = nullptr;
    DWORD const flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD const length =
        FormatMessageW(flags, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return L"Windows error " + std::to_wstring(error);
    }

    std::wstring message(buffer, static_cast<size_t>(length));
    LocalFree(buffer);
    return Trim_trailing_wspace(std::move(message));
}

void Strip_utf8_bom(std::string &utf8_text) {
    if (utf8_text.size() >= kUtf8BomBytes.size() &&
        static_cast<unsigned char>(utf8_text[0]) == kUtf8BomBytes[0] &&
        static_cast<unsigned char>(utf8_text[1]) == kUtf8BomBytes[1] &&
        static_cast<unsigned char>(utf8_text[2]) == kUtf8BomBytes[2]) {
        utf8_text.erase(0, kUtf8BomBytes.size());
    }
}

[[nodiscard]] bool Try_compute_annotation_target_bounds(
    greenflame::core::CaptureSaveRequest const &request,
    greenflame::core::RectPx &target_bounds) noexcept {
    int64_t const left64 =
        static_cast<int64_t>(request.source_rect_screen.left) - request.padding_px.left;
    int64_t const top64 =
        static_cast<int64_t>(request.source_rect_screen.top) - request.padding_px.top;
    int64_t const right64 = static_cast<int64_t>(request.source_rect_screen.right) +
                            request.padding_px.right;
    int64_t const bottom64 = static_cast<int64_t>(request.source_rect_screen.bottom) +
                             request.padding_px.bottom;
    if (left64 < static_cast<int64_t>(INT32_MIN) ||
        left64 > static_cast<int64_t>(INT32_MAX) ||
        top64 < static_cast<int64_t>(INT32_MIN) ||
        top64 > static_cast<int64_t>(INT32_MAX) ||
        right64 < static_cast<int64_t>(INT32_MIN) ||
        right64 > static_cast<int64_t>(INT32_MAX) ||
        bottom64 < static_cast<int64_t>(INT32_MIN) ||
        bottom64 > static_cast<int64_t>(INT32_MAX)) {
        return false;
    }

    target_bounds = greenflame::core::RectPx::From_ltrb(
        static_cast<int32_t>(left64), static_cast<int32_t>(top64),
        static_cast<int32_t>(right64), static_cast<int32_t>(bottom64));
    return true;
}

[[nodiscard]] greenflame::core::CaptureSaveResult
Make_capture_save_result(greenflame::core::CaptureSaveStatus status,
                         std::wstring_view error_message = {}) {
    return greenflame::core::CaptureSaveResult{status, std::wstring(error_message)};
}

[[nodiscard]] greenflame::core::CaptureSaveResult
Save_bitmap_to_file(greenflame::GdiCaptureResult const &capture, std::wstring_view path,
                    greenflame::core::ImageSaveFormat format) {
    if (path.empty()) {
        return Make_capture_save_result(
            greenflame::core::CaptureSaveStatus::SaveFailed,
            L"Error: Failed to encode or write image file.");
    }

    std::wstring const output_path(path);
    if (!greenflame::Save_capture_to_file(capture, output_path.c_str(), format)) {
        return Make_capture_save_result(
            greenflame::core::CaptureSaveStatus::SaveFailed,
            L"Error: Failed to encode or write image file.");
    }
    return Make_capture_save_result(greenflame::core::CaptureSaveStatus::Success);
}

[[nodiscard]] bool Composite_annotations_into_capture(
    greenflame::GdiCaptureResult &capture,
    std::span<const greenflame::core::Annotation> annotations,
    greenflame::core::RectPx target_bounds) {
    if (!capture.Is_valid() || annotations.empty()) {
        return true;
    }

    HDC const dc = GetDC(nullptr);
    if (dc == nullptr) {
        return false;
    }

    bool ok = false;
    int const row_bytes = greenflame::Row_bytes32(capture.width);
    size_t const buffer_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(capture.height);
    std::vector<uint8_t> pixels(buffer_size);
    BITMAPINFOHEADER bmi{};
    greenflame::Fill_bmi32_top_down(bmi, capture.width, capture.height);
    if (GetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                  pixels.data(), reinterpret_cast<BITMAPINFO *>(&bmi),
                  DIB_RGB_COLORS) != 0) {
        greenflame::core::Blend_annotations_onto_pixels(pixels, capture.width,
                                                        capture.height, row_bytes,
                                                        annotations, target_bounds);
        ok = SetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                       pixels.data(), reinterpret_cast<BITMAPINFO *>(&bmi),
                       DIB_RGB_COLORS) != 0;
    }

    ReleaseDC(nullptr, dc);
    return ok;
}

[[nodiscard]] bool Has_installed_font_family(IDWriteFontCollection *font_collection,
                                             std::wstring_view family) {
    if (font_collection == nullptr || family.empty()) {
        return false;
    }

    UINT32 family_index = 0;
    BOOL exists = FALSE;
    if (FAILED(
            font_collection->FindFamilyName(family.data(), &family_index, &exists))) {
        return false;
    }
    return exists != FALSE;
}

template <typename T>
[[nodiscard]] bool Is_rasterized_ready(T const &annotation) noexcept {
    return annotation.bitmap_width_px > 0 && annotation.bitmap_height_px > 0 &&
           annotation.bitmap_row_bytes > 0 && !annotation.premultiplied_bgra.empty() &&
           static_cast<size_t>(annotation.bitmap_row_bytes) *
                   static_cast<size_t>(annotation.bitmap_height_px) ==
               annotation.premultiplied_bgra.size();
}

[[nodiscard]] greenflame::core::CaptureSaveResult
Save_exact_source_capture_to_file(greenflame::GdiCaptureResult &source_capture,
                                  greenflame::core::CaptureSaveRequest const &request,
                                  std::wstring_view path,
                                  greenflame::core::ImageSaveFormat format) {
    int32_t source_width = 0;
    int32_t source_height = 0;
    int32_t output_width = 0;
    int32_t output_height = 0;
    if (!Try_compute_render_sizes(request, source_width, source_height, output_width,
                                  output_height)) {
        return Make_capture_save_result(
            greenflame::core::CaptureSaveStatus::SaveFailed,
            L"Error: Failed to prepare the capture bitmap.");
    }

    if (source_capture.width != source_width ||
        source_capture.height != source_height) {
        return Make_capture_save_result(
            greenflame::core::CaptureSaveStatus::SaveFailed,
            L"Error: Failed to prepare the capture bitmap.");
    }

    if (request.padding_px.Is_zero()) {
        if (!Composite_annotations_into_capture(source_capture, request.annotations,
                                                request.source_rect_screen)) {
            return Make_capture_save_result(
                greenflame::core::CaptureSaveStatus::SaveFailed,
                L"Error: Failed to compose annotations onto the capture.");
        }
        return Save_bitmap_to_file(source_capture, path, format);
    }

    greenflame::GdiCaptureResult final_capture{};
    if (!greenflame::Create_solid_capture(output_width, output_height,
                                          request.fill_color, final_capture)) {
        return Make_capture_save_result(
            greenflame::core::CaptureSaveStatus::SaveFailed,
            L"Error: Failed to prepare the capture bitmap.");
    }

    greenflame::core::CaptureSaveResult result =
        Make_capture_save_result(greenflame::core::CaptureSaveStatus::Success);
    bool const blitted = greenflame::Blit_capture(
        source_capture, 0, 0, source_width, source_height, final_capture,
        request.padding_px.left, request.padding_px.top);
    if (!blitted) {
        result =
            Make_capture_save_result(greenflame::core::CaptureSaveStatus::SaveFailed,
                                     L"Error: Failed to prepare the capture bitmap.");
    } else {
        greenflame::core::RectPx annotation_target_bounds = {};
        if (!Try_compute_annotation_target_bounds(request, annotation_target_bounds) ||
            !Composite_annotations_into_capture(final_capture, request.annotations,
                                                annotation_target_bounds)) {
            result = Make_capture_save_result(
                greenflame::core::CaptureSaveStatus::SaveFailed,
                L"Error: Failed to compose annotations onto the capture.");
        } else {
            result = Save_bitmap_to_file(final_capture, path, format);
        }
    }

    final_capture.Free();
    return result;
}

} // namespace

namespace greenflame {

core::PointPx Win32DisplayQueries::Get_cursor_pos_px() const {
    return greenflame::Get_cursor_pos_px();
}

core::RectPx Win32DisplayQueries::Get_virtual_desktop_bounds_px() const {
    return greenflame::Get_virtual_desktop_bounds_px();
}

std::vector<core::MonitorWithBounds>
Win32DisplayQueries::Get_monitors_with_bounds() const {
    return greenflame::Get_monitors_with_bounds();
}

std::optional<core::RectPx> Win32WindowInspector::Get_window_rect(HWND hwnd) const {
    return window_query_.Get_window_rect(hwnd);
}

std::optional<core::WindowCandidateInfo>
Win32WindowInspector::Get_window_info(HWND hwnd) const {
    return Try_get_window_candidate_info(hwnd, &window_query_, false);
}

bool Win32WindowInspector::Is_window_valid(HWND hwnd) const {
    return IsWindow(hwnd) != 0;
}

bool Win32WindowInspector::Is_window_minimized(HWND hwnd) const {
    return IsIconic(hwnd) != 0;
}

WindowObscuration Win32WindowInspector::Get_window_obscuration(HWND hwnd) const {
    return window_query_.Get_window_obscuration(hwnd);
}

std::optional<core::RectPx>
Win32WindowInspector::Get_foreground_window_rect(HWND exclude_hwnd) const {
    return window_query_.Get_foreground_window_rect(exclude_hwnd);
}

std::optional<core::RectPx>
Win32WindowInspector::Get_window_rect_under_cursor(POINT screen_pt,
                                                   HWND exclude_hwnd) const {
    return window_query_.Get_window_rect_under_cursor(screen_pt, exclude_hwnd);
}

std::vector<WindowMatch>
Win32WindowInspector::Find_windows_by_title(std::wstring_view needle) const {
    WindowSearchState state{};
    state.needle = needle;
    state.window_query = &window_query_;
    (void)EnumWindows(Enum_windows_by_title_proc, reinterpret_cast<LPARAM>(&state));
    if (state.had_exception) {
        return {};
    }
    return state.matches;
}

size_t
Win32WindowInspector::Count_minimized_windows_by_title(std::wstring_view needle) const {
    MinimizedWindowSearchState state{};
    state.needle = needle;
    (void)EnumWindows(Enum_minimized_windows_by_title_proc,
                      reinterpret_cast<LPARAM>(&state));
    if (state.had_exception) {
        return 0;
    }
    return state.match_count;
}

bool Win32CaptureService::Copy_rect_to_clipboard(core::RectPx screen_rect) {
    if (screen_rect.Is_empty()) {
        return false;
    }

    core::RectPx const virtual_bounds = greenflame::Get_virtual_desktop_bounds_px();
    std::optional<core::RectPx> const clipped_screen =
        core::RectPx::Clip(screen_rect, virtual_bounds);
    if (!clipped_screen.has_value()) {
        return false;
    }

    GdiCaptureResult capture{};
    if (!greenflame::Capture_virtual_desktop(capture)) {
        return false;
    }

    core::RectPx const capture_rect =
        Capture_rect_from_screen_rect(*clipped_screen, virtual_bounds);
    GdiCaptureResult cropped{};
    bool const cropped_ok =
        greenflame::Crop_capture(capture, capture_rect.left, capture_rect.top,
                                 capture_rect.Width(), capture_rect.Height(), cropped);
    capture.Free();
    if (!cropped_ok) {
        return false;
    }

    bool const copied = greenflame::Copy_capture_to_clipboard(cropped, nullptr);
    cropped.Free();
    return copied;
}

core::CaptureSaveResult
Win32CaptureService::Save_capture_to_file(core::CaptureSaveRequest const &request,
                                          std::wstring_view path,
                                          core::ImageSaveFormat format) {
    if (request.source_rect_screen.Is_empty() || path.empty()) {
        return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                        L"Error: Failed to encode or write image "
                                        L"file.");
    }

    if (request.source_kind == core::CaptureSourceKind::Window &&
        request.window_capture_backend == core::WindowCaptureBackend::Wgc) {
        if (request.source_window == nullptr) {
            return Make_capture_save_result(
                core::CaptureSaveStatus::BackendFailed,
                L"Error: WGC window capture requires a valid target window.");
        }

        GdiCaptureResult source_capture{};
        core::CaptureSaveResult wgc_result = greenflame::Capture_window_with_wgc(
            request.source_window, request.source_rect_screen, source_capture);
        if (wgc_result.status != core::CaptureSaveStatus::Success) {
            source_capture.Free();
            return wgc_result;
        }

        core::CaptureSaveResult const save_result =
            Save_exact_source_capture_to_file(source_capture, request, path, format);
        source_capture.Free();
        return save_result;
    }

    core::RectPx const virtual_bounds = greenflame::Get_virtual_desktop_bounds_px();
    std::optional<core::RectPx> const clipped_screen =
        core::RectPx::Clip(request.source_rect_screen, virtual_bounds);
    if (!clipped_screen.has_value()) {
        return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                        L"Error: Failed to prepare the capture "
                                        L"bitmap.");
    }

    if (!request.preserve_source_extent && request.padding_px.Is_zero()) {
        GdiCaptureResult capture{};
        if (!greenflame::Capture_virtual_desktop(capture)) {
            return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                            L"Error: Failed to prepare the capture "
                                            L"bitmap.");
        }

        core::RectPx const capture_rect =
            Capture_rect_from_screen_rect(*clipped_screen, virtual_bounds);
        GdiCaptureResult cropped{};
        bool const cropped_ok = greenflame::Crop_capture(
            capture, capture_rect.left, capture_rect.top, capture_rect.Width(),
            capture_rect.Height(), cropped);
        capture.Free();
        if (!cropped_ok) {
            return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                            L"Error: Failed to prepare the capture "
                                            L"bitmap.");
        }

        if (!Composite_annotations_into_capture(cropped, request.annotations,
                                                request.source_rect_screen)) {
            cropped.Free();
            return Make_capture_save_result(
                core::CaptureSaveStatus::SaveFailed,
                L"Error: Failed to compose annotations onto the capture.");
        }

        core::CaptureSaveResult const save_result =
            Save_bitmap_to_file(cropped, path, format);
        cropped.Free();
        return save_result;
    }

    int32_t source_width = 0;
    int32_t source_height = 0;
    int32_t output_width = 0;
    int32_t output_height = 0;
    if (!Try_compute_render_sizes(request, source_width, source_height, output_width,
                                  output_height)) {
        return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                        L"Error: Failed to prepare the capture "
                                        L"bitmap.");
    }

    GdiCaptureResult capture{};
    if (!greenflame::Capture_virtual_desktop(capture)) {
        return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                        L"Error: Failed to prepare the capture "
                                        L"bitmap.");
    }

    GdiCaptureResult source_canvas{};
    if (!greenflame::Create_solid_capture(source_width, source_height,
                                          request.fill_color, source_canvas)) {
        capture.Free();
        return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                        L"Error: Failed to prepare the capture "
                                        L"bitmap.");
    }

    core::RectPx const capture_rect =
        Capture_rect_from_screen_rect(*clipped_screen, virtual_bounds);
    int32_t dst_left = 0;
    int32_t dst_top = 0;
    bool const have_offsets =
        Try_compute_offset(clipped_screen->left, request.source_rect_screen.left,
                           dst_left) &&
        Try_compute_offset(clipped_screen->top, request.source_rect_screen.top,
                           dst_top);
    bool const blitted_source =
        have_offsets &&
        greenflame::Blit_capture(capture, capture_rect.left, capture_rect.top,
                                 capture_rect.Width(), capture_rect.Height(),
                                 source_canvas, dst_left, dst_top);
    capture.Free();
    if (!blitted_source) {
        source_canvas.Free();
        return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                        L"Error: Failed to prepare the capture "
                                        L"bitmap.");
    }

    GdiCaptureResult final_capture{};
    GdiCaptureResult const *capture_to_save = &source_canvas;
    if (!request.padding_px.Is_zero()) {
        if (!greenflame::Create_solid_capture(output_width, output_height,
                                              request.fill_color, final_capture)) {
            source_canvas.Free();
            return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                            L"Error: Failed to prepare the capture "
                                            L"bitmap.");
        }
        bool const blitted_final = greenflame::Blit_capture(
            source_canvas, 0, 0, source_width, source_height, final_capture,
            request.padding_px.left, request.padding_px.top);
        source_canvas.Free();
        if (!blitted_final) {
            final_capture.Free();
            return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                            L"Error: Failed to prepare the capture "
                                            L"bitmap.");
        }
        capture_to_save = &final_capture;
    }

    core::RectPx annotation_target_bounds = {};
    if (!Try_compute_annotation_target_bounds(request, annotation_target_bounds) ||
        !Composite_annotations_into_capture(
            capture_to_save == &source_canvas ? source_canvas : final_capture,
            request.annotations, annotation_target_bounds)) {
        if (capture_to_save == &source_canvas) {
            source_canvas.Free();
        } else {
            final_capture.Free();
        }
        return Make_capture_save_result(
            core::CaptureSaveStatus::SaveFailed,
            L"Error: Failed to compose annotations onto the capture.");
    }

    core::CaptureSaveResult const save_result =
        Save_bitmap_to_file(*capture_to_save, path, format);
    if (capture_to_save == &source_canvas) {
        source_canvas.Free();
    } else {
        final_capture.Free();
    }
    return save_result;
}

core::AnnotationPreparationResult
Win32AnnotationPreparationService::Prepare_annotations(
    core::AnnotationPreparationRequest const &request) {
    core::AnnotationPreparationResult result{};
    if (request.annotations.empty()) {
        result.status = core::AnnotationPreparationStatus::Success;
        return result;
    }
    result.annotations = request.annotations;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   d2d_factory.GetAddressOf());
    if (FAILED(hr) || !d2d_factory) {
        result.error_message = L"Error: Failed to initialize Direct2D for --annotate.";
        return result;
    }

    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory;
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown **>(dwrite_factory.GetAddressOf()));
    if (FAILED(hr) || !dwrite_factory) {
        result.error_message =
            L"Error: Failed to initialize DirectWrite for --annotate.";
        return result;
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection> font_collection;
    hr = dwrite_factory->GetSystemFontCollection(font_collection.GetAddressOf(), FALSE);
    if (FAILED(hr) || !font_collection) {
        result.error_message =
            L"Error: Failed to enumerate installed fonts for --annotate.";
        return result;
    }

    D2DTextLayoutEngine engine(d2d_factory.Get(), dwrite_factory.Get());
    std::array<std::wstring_view, 4> preset_font_families = {
        request.preset_font_families[0], request.preset_font_families[1],
        request.preset_font_families[2], request.preset_font_families[3]};
    engine.Set_font_families(preset_font_families);

    for (core::Annotation &annotation : result.annotations) {
        if (core::TextAnnotation *const text =
                std::get_if<core::TextAnnotation>(&annotation.data);
            text != nullptr) {
            if (!text->base_style.font_family.empty() &&
                !Has_installed_font_family(font_collection.Get(),
                                           text->base_style.font_family)) {
                result.status = core::AnnotationPreparationStatus::InputInvalid;
                result.error_message = L"--annotate: font family \"" +
                                       text->base_style.font_family +
                                       L"\" is not installed.";
                return result;
            }

            if (!engine.Prepare_for_cli(*text) || !Is_rasterized_ready(*text)) {
                result.error_message =
                    L"Error: Failed to rasterize a text annotation for --annotate.";
                return result;
            }
            continue;
        }

        if (core::BubbleAnnotation *const bubble =
                std::get_if<core::BubbleAnnotation>(&annotation.data);
            bubble != nullptr) {
            if (!bubble->font_family.empty() &&
                !Has_installed_font_family(font_collection.Get(),
                                           bubble->font_family)) {
                result.status = core::AnnotationPreparationStatus::InputInvalid;
                result.error_message = L"--annotate: font family \"" +
                                       bubble->font_family + L"\" is not installed.";
                return result;
            }

            engine.Rasterize_bubble(*bubble);
            if (!Is_rasterized_ready(*bubble)) {
                result.error_message =
                    L"Error: Failed to rasterize a bubble annotation for --annotate.";
                return result;
            }
        }
    }

    result.status = core::AnnotationPreparationStatus::Success;
    return result;
}

std::vector<std::wstring>
Win32FileSystemService::List_directory_filenames(std::wstring_view dir) const {
    return greenflame::List_directory_filenames(dir);
}

std::wstring
Win32FileSystemService::Reserve_unique_file_path(std::wstring_view desired) const {
    return greenflame::Reserve_unique_file_path(desired);
}

bool Win32FileSystemService::Try_reserve_exact_file_path(std::wstring_view path,
                                                         bool &already_exists) const {
    already_exists = false;
    if (path.empty()) {
        return false;
    }

    std::wstring const path_string(path);
    HANDLE const handle =
        CreateFileW(path_string.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return true;
    }

    DWORD const error = GetLastError();
    already_exists = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
    return false;
}

std::wstring Win32FileSystemService::Resolve_save_directory(
    std::wstring const &configured_dir) const {
    std::wstring dir = configured_dir;
    if (dir.empty()) {
        wchar_t pictures_dir[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pictures_dir);
        dir = pictures_dir;
        dir += L"\\greenflame";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring
Win32FileSystemService::Resolve_absolute_path(std::wstring_view path) const {
    if (path.empty()) {
        return {};
    }

    std::wstring input(path);
    DWORD const required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (required == 0) {
        return input;
    }

    std::wstring result;
    result.resize(required);
    DWORD const written =
        GetFullPathNameW(input.c_str(), required, result.data(), nullptr);
    if (written == 0) {
        return input;
    }
    if (written < result.size()) {
        result.resize(written);
    }
    return result;
}

bool Win32FileSystemService::Try_read_text_file_utf8(
    std::wstring_view path, std::string &utf8_text, std::wstring &error_message) const {
    utf8_text.clear();
    error_message.clear();
    if (path.empty()) {
        error_message = L"Path is empty.";
        return false;
    }

    std::wstring const path_string(path);
    HANDLE const handle =
        CreateFileW(path_string.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error_message = Format_windows_error_message(GetLastError());
        return false;
    }

    LARGE_INTEGER file_size = {};
    if (GetFileSizeEx(handle, &file_size) == 0) {
        error_message = Format_windows_error_message(GetLastError());
        CloseHandle(handle);
        return false;
    }
    if (file_size.QuadPart < 0) {
        error_message = L"File is too large.";
        CloseHandle(handle);
        return false;
    }

    utf8_text.resize(static_cast<size_t>(file_size.QuadPart));
    size_t total_read = 0;
    std::span<char> utf8_bytes(utf8_text);
    while (total_read < utf8_text.size()) {
        std::span<char> remaining_bytes = utf8_bytes.subspan(total_read);
        DWORD const chunk_size = static_cast<DWORD>(
            std::min<size_t>(remaining_bytes.size(), static_cast<size_t>(1u << 20)));
        DWORD bytes_read = 0;
        if (ReadFile(handle, remaining_bytes.data(), chunk_size, &bytes_read,
                     nullptr) == 0) {
            error_message = Format_windows_error_message(GetLastError());
            CloseHandle(handle);
            utf8_text.clear();
            return false;
        }
        if (bytes_read == 0) {
            break;
        }
        total_read += static_cast<size_t>(bytes_read);
    }

    CloseHandle(handle);
    utf8_text.resize(total_read);
    Strip_utf8_bom(utf8_text);
    return true;
}

void Win32FileSystemService::Delete_file_if_exists(std::wstring_view path) const {
    if (path.empty()) {
        return;
    }
    std::wstring const path_string(path);
    (void)DeleteFileW(path_string.c_str());
}

core::SaveTimestamp Win32FileSystemService::Get_current_timestamp() const {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    core::SaveTimestamp timestamp{};
    timestamp.day = st.wDay;
    timestamp.month = st.wMonth;
    timestamp.year = st.wYear;
    timestamp.hour = st.wHour;
    timestamp.minute = st.wMinute;
    timestamp.second = st.wSecond;
    return timestamp;
}

} // namespace greenflame
