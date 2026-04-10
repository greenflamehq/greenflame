#include "win/win32_services.h"

#include "app_config_store.h"
#include "greenflame/win/annotation_capture_renderer.h"
#include "greenflame/win/d2d_text_layout_engine.h"
#include "greenflame_core/string_utils.h"
#include "win/display_queries.h"
#include "win/gdi_capture.h"
#include "win/save_image.h"
#include "win/wgc_window_capture.h"

namespace {

constexpr std::array<unsigned char, 3> kUtf8BomBytes = {{0xEFu, 0xBBu, 0xBFu}};
constexpr UINT kBytesPerPixel32 = 4u;
constexpr wchar_t kInputTransparencyUnsupportedMessage[] =
    L"--input: image transparency is not supported with --input in V1.";

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

struct ScopedComApartment final {
    explicit ScopedComApartment(bool owns_apartment) noexcept
        : owns_apartment_(owns_apartment) {}
    ~ScopedComApartment() {
        if (owns_apartment_) {
            CoUninitialize();
        }
    }

    ScopedComApartment(ScopedComApartment const &) = delete;
    ScopedComApartment &operator=(ScopedComApartment const &) = delete;

  private:
    bool owns_apartment_ = false;
};

struct DecodedInputImage final {
    int32_t width = 0;
    int32_t height = 0;
    int row_bytes = 0;
    greenflame::core::ImageSaveFormat format = greenflame::core::ImageSaveFormat::Png;
    std::vector<uint8_t> pixels = {};
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
    info.uncapturable = greenflame::Is_window_excluded_from_capture(hwnd);
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

[[nodiscard]] bool Ends_with_no_case(std::wstring_view text,
                                     std::wstring_view suffix) noexcept {
    if (suffix.size() > text.size()) {
        return false;
    }

    size_t const offset = text.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        wchar_t const text_ch = static_cast<wchar_t>(std::towlower(text[offset + i]));
        wchar_t const suffix_ch = static_cast<wchar_t>(std::towlower(suffix[i]));
        if (text_ch != suffix_ch) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::wstring Format_hresult_message(HRESULT hr) {
    LPWSTR buffer = nullptr;
    DWORD const flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD const length = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr),
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        constexpr std::array<wchar_t, 16> hex_chars = {
            {L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'A', L'B',
             L'C', L'D', L'E', L'F'}};
        constexpr int nibbles = 8;
        constexpr uint32_t nibble_mask = 0xFu;
        uint32_t const value = static_cast<uint32_t>(hr);
        std::wstring message = L"HRESULT 0x00000000";
        for (int i = 0; i < nibbles; ++i) {
            message[message.size() - 1u - static_cast<size_t>(i)] =
                hex_chars[(value >> (i * 4)) & nibble_mask];
        }
        return message;
    }

    std::wstring message(buffer, static_cast<size_t>(length));
    LocalFree(buffer);
    return Trim_trailing_wspace(std::move(message));
}

[[nodiscard]] std::wstring Build_hresult_error(std::wstring_view prefix, HRESULT hr) {
    std::wstring message(prefix);
    std::wstring const details = Format_hresult_message(hr);
    if (!details.empty()) {
        message += L": ";
        message += details;
    }
    return message;
}

[[nodiscard]] bool Has_supported_input_extension(std::wstring_view path) noexcept {
    return Ends_with_no_case(path, L".png") || Ends_with_no_case(path, L".jpg") ||
           Ends_with_no_case(path, L".jpeg") || Ends_with_no_case(path, L".bmp");
}

[[nodiscard]] bool
Try_map_container_format(GUID const &container_format,
                         greenflame::core::ImageSaveFormat &format) noexcept {
    if (container_format == GUID_ContainerFormatPng) {
        format = greenflame::core::ImageSaveFormat::Png;
        return true;
    }
    if (container_format == GUID_ContainerFormatJpeg) {
        format = greenflame::core::ImageSaveFormat::Jpeg;
        return true;
    }
    if (container_format == GUID_ContainerFormatBmp) {
        format = greenflame::core::ImageSaveFormat::Bmp;
        return true;
    }
    return false;
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

[[nodiscard]] bool Maybe_composite_captured_cursor(
    greenflame::core::CaptureSaveRequest const &request,
    greenflame::CapturedCursorSnapshot const *cursor_snapshot,
    greenflame::core::PointPx target_origin_px, greenflame::GdiCaptureResult &target) {
    if (!request.include_cursor || cursor_snapshot == nullptr ||
        !cursor_snapshot->Is_valid()) {
        return true;
    }
    return greenflame::Composite_cursor_snapshot(*cursor_snapshot, target_origin_px,
                                                 target);
}

[[nodiscard]] bool Has_installed_font_family(IDWriteFontCollection *font_collection,
                                             std::wstring_view family) {
    if (font_collection == nullptr || family.empty()) {
        return false;
    }

    UINT32 family_index = 0;
    BOOL exists = FALSE;
    std::wstring const family_name(family);
    if (FAILED(
            font_collection->FindFamilyName(family_name.c_str(), &family_index, &exists))) {
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

[[nodiscard]] greenflame::core::CaptureSaveResult Save_exact_source_capture_to_file(
    greenflame::GdiCaptureResult &source_capture,
    greenflame::core::CaptureSaveRequest const &request,
    greenflame::CapturedCursorSnapshot const *cursor_snapshot, std::wstring_view path,
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
        if (!Maybe_composite_captured_cursor(request, cursor_snapshot,
                                             request.source_rect_screen.Top_left(),
                                             source_capture)) {
            return Make_capture_save_result(
                greenflame::core::CaptureSaveStatus::SaveFailed,
                L"Error: Failed to prepare the capture bitmap.");
        }
        if (!greenflame::Render_annotations_into_capture(
                source_capture, request.annotations, request.source_rect_screen)) {
            return Make_capture_save_result(
                greenflame::core::CaptureSaveStatus::SaveFailed,
                L"Error: Failed to compose annotations onto the capture.");
        }
        return Save_bitmap_to_file(source_capture, path, format);
    }

    if (!Maybe_composite_captured_cursor(request, cursor_snapshot,
                                         request.source_rect_screen.Top_left(),
                                         source_capture)) {
        return Make_capture_save_result(
            greenflame::core::CaptureSaveStatus::SaveFailed,
            L"Error: Failed to prepare the capture bitmap.");
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
            !greenflame::Render_annotations_into_capture(
                final_capture, request.annotations, annotation_target_bounds)) {
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

[[nodiscard]] greenflame::core::InputImageProbeResult
Make_probe_result(greenflame::core::InputImageProbeStatus status,
                  std::wstring_view error_message = {}) {
    return greenflame::core::InputImageProbeResult{
        status, 0, 0, greenflame::core::ImageSaveFormat::Png,
        std::wstring(error_message)};
}

[[nodiscard]] greenflame::core::InputImageSaveResult
Make_input_save_result(greenflame::core::InputImageSaveStatus status,
                       std::wstring_view error_message = {}) {
    return greenflame::core::InputImageSaveResult{status, std::wstring(error_message)};
}

[[nodiscard]] bool Try_decode_input_image(std::wstring_view path,
                                          DecodedInputImage &decoded_image,
                                          std::wstring &error_message) {
    decoded_image = {};
    error_message.clear();

    if (path.empty()) {
        error_message = L"--input: path is empty.";
        return false;
    }
    if (!Has_supported_input_extension(path)) {
        error_message = L"--input: unsupported input image extension. Supported "
                        L"extensions are .png, .jpg/.jpeg, and .bmp.";
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool owns_apartment = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        owns_apartment = false;
    } else if (FAILED(hr)) {
        error_message =
            Build_hresult_error(L"Error: Failed to initialize COM for --input.", hr);
        return false;
    }
    ScopedComApartment const apartment(owns_apartment);

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr) || !factory) {
        error_message = Build_hresult_error(
            L"Error: Failed to initialize Windows Imaging Component for --input.", hr);
        return false;
    }

    std::wstring const path_string(path);
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path_string.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand,
                                            decoder.GetAddressOf());
    if (FAILED(hr) || !decoder) {
        error_message = L"--input: unable to read image file \"" + path_string +
                        L"\": " + Format_hresult_message(hr);
        return false;
    }

    GUID container_format = GUID_ContainerFormatPng;
    hr = decoder->GetContainerFormat(&container_format);
    if (FAILED(hr) ||
        !Try_map_container_format(container_format, decoded_image.format)) {
        error_message = L"--input: unsupported input image format. Supported formats "
                        L"are png, jpg/jpeg, and bmp.";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr) || !frame) {
        error_message = L"--input: unable to read image file \"" + path_string +
                        L"\": " + Format_hresult_message(hr);
        return false;
    }

    UINT width_u = 0;
    UINT height_u = 0;
    hr = frame->GetSize(&width_u, &height_u);
    if (FAILED(hr) || width_u == 0 || height_u == 0 ||
        width_u > static_cast<UINT>(INT32_MAX) ||
        height_u > static_cast<UINT>(INT32_MAX)) {
        error_message = L"--input: unable to read image file \"" + path_string +
                        L"\": invalid image dimensions.";
        return false;
    }

    if (width_u > static_cast<UINT>(INT32_MAX) / kBytesPerPixel32) {
        error_message = L"--input: unable to read image file \"" + path_string +
                        L"\": image dimensions are too large.";
        return false;
    }

    UINT const stride = width_u * kBytesPerPixel32;
    uint64_t const pixel_bytes64 =
        static_cast<uint64_t>(stride) * static_cast<uint64_t>(height_u);
    if (pixel_bytes64 == 0 || pixel_bytes64 > static_cast<uint64_t>(UINT_MAX)) {
        error_message = L"--input: unable to read image file \"" + path_string +
                        L"\": image dimensions are too large.";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr) || !converter) {
        error_message = Build_hresult_error(
            L"Error: Failed to convert the input image into BGRA pixels.", hr);
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        error_message = Build_hresult_error(
            L"Error: Failed to convert the input image into BGRA pixels.", hr);
        return false;
    }

    decoded_image.width = static_cast<int32_t>(width_u);
    decoded_image.height = static_cast<int32_t>(height_u);
    decoded_image.row_bytes = static_cast<int>(stride);
    decoded_image.pixels.resize(static_cast<size_t>(pixel_bytes64));
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixel_bytes64),
                               decoded_image.pixels.data());
    if (FAILED(hr)) {
        decoded_image = {};
        error_message = L"--input: unable to read image file \"" + path_string +
                        L"\": " + Format_hresult_message(hr);
        return false;
    }

    for (size_t i = 3; i < decoded_image.pixels.size(); i += kBytesPerPixel32) {
        if (decoded_image.pixels[i] != 255u) {
            decoded_image = {};
            error_message = kInputTransparencyUnsupportedMessage;
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool
Try_create_capture_from_input_image(DecodedInputImage const &decoded_image,
                                    greenflame::GdiCaptureResult &capture,
                                    std::wstring &error_message) {
    capture.Free();
    error_message.clear();

    if (decoded_image.width <= 0 || decoded_image.height <= 0 ||
        decoded_image.row_bytes <= 0 || decoded_image.pixels.empty()) {
        error_message = L"Error: Failed to prepare the input image bitmap.";
        return false;
    }

    BITMAPINFO bitmap_info = {};
    greenflame::Fill_bmi32_top_down(bitmap_info.bmiHeader, decoded_image.width,
                                    decoded_image.height);

    HDC const screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        error_message = L"Error: Failed to acquire a screen DC while preparing the "
                        L"input image bitmap.";
        return false;
    }

    void *bitmap_bits = nullptr;
    HBITMAP const bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS,
                                            &bitmap_bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);
    if (bitmap == nullptr || bitmap_bits == nullptr) {
        error_message = L"Error: Failed to create a 32bpp DIB for the input image.";
        return false;
    }

    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage-in-container")
    std::span<uint8_t> destination_bytes{reinterpret_cast<uint8_t *>(bitmap_bits),
                                         decoded_image.pixels.size()};
    CLANG_WARN_IGNORE_POP()
    std::copy(decoded_image.pixels.begin(), decoded_image.pixels.end(),
              destination_bytes.begin());

    capture.bitmap = bitmap;
    capture.width = decoded_image.width;
    capture.height = decoded_image.height;
    return true;
}

[[nodiscard]] std::wstring Directory_from_path(std::wstring_view path) {
    size_t const separator = path.find_last_of(L"\\/");
    if (separator == std::wstring_view::npos) {
        return L".";
    }
    if (separator == 0) {
        return std::wstring(path.substr(0, 1));
    }
    return std::wstring(path.substr(0, separator));
}

[[nodiscard]] bool Try_create_sibling_temp_path(std::wstring_view output_path,
                                                std::wstring &temp_path,
                                                std::wstring &error_message) {
    temp_path.clear();
    error_message.clear();

    std::wstring const parent_dir = Directory_from_path(output_path);
    constexpr size_t buffer_size = MAX_PATH + 1u;
    std::array<wchar_t, buffer_size> temp_path_buffer = {};
    if (GetTempFileNameW(parent_dir.c_str(), L"gfi", 0, temp_path_buffer.data()) == 0) {
        error_message = L"Error: Failed to create a temporary output file: ";
        error_message += Format_windows_error_message(GetLastError());
        return false;
    }

    temp_path = temp_path_buffer.data();
    return true;
}

void Delete_file_if_exists(std::wstring_view path) {
    if (path.empty()) {
        return;
    }

    std::wstring const path_string(path);
    (void)DeleteFileW(path_string.c_str());
}

[[nodiscard]] bool Try_replace_file(std::wstring_view source_path,
                                    std::wstring_view destination_path,
                                    std::wstring &error_message) {
    error_message.clear();

    std::wstring const source_path_string(source_path);
    std::wstring const destination_path_string(destination_path);
    if (MoveFileExW(source_path_string.c_str(), destination_path_string.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        error_message = L"Error: Failed to replace the original input image: ";
        error_message += Format_windows_error_message(GetLastError());
        return false;
    }
    return true;
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

bool Win32CaptureService::Copy_rect_to_clipboard(core::RectPx screen_rect,
                                                 bool include_cursor) {
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
    greenflame::CapturedCursorSnapshot cursor_snapshot = {};
    if (include_cursor) {
        (void)greenflame::Capture_cursor_snapshot(cursor_snapshot);
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

    if (include_cursor && !greenflame::Composite_cursor_snapshot(
                              cursor_snapshot, clipped_screen->Top_left(), cropped)) {
        cropped.Free();
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

        greenflame::CapturedCursorSnapshot cursor_snapshot = {};
        if (request.include_cursor) {
            (void)greenflame::Capture_cursor_snapshot(cursor_snapshot);
        }
        core::CaptureSaveResult const save_result = Save_exact_source_capture_to_file(
            source_capture, request, &cursor_snapshot, path, format);
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
        greenflame::CapturedCursorSnapshot cursor_snapshot = {};
        if (request.include_cursor) {
            (void)greenflame::Capture_cursor_snapshot(cursor_snapshot);
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

        if (!Maybe_composite_captured_cursor(request, &cursor_snapshot,
                                             clipped_screen->Top_left(), cropped)) {
            cropped.Free();
            return Make_capture_save_result(core::CaptureSaveStatus::SaveFailed,
                                            L"Error: Failed to prepare the capture "
                                            L"bitmap.");
        }

        if (!greenflame::Render_annotations_into_capture(cropped, request.annotations,
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
    greenflame::CapturedCursorSnapshot cursor_snapshot = {};
    if (request.include_cursor) {
        (void)greenflame::Capture_cursor_snapshot(cursor_snapshot);
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

    if (!Maybe_composite_captured_cursor(request, &cursor_snapshot,
                                         request.source_rect_screen.Top_left(),
                                         source_canvas)) {
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
        !greenflame::Render_annotations_into_capture(
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

core::InputImageProbeResult
Win32InputImageService::Probe_input_image(std::wstring_view path) {
    DecodedInputImage decoded_image{};
    std::wstring error_message = {};
    if (!Try_decode_input_image(path, decoded_image, error_message)) {
        return Make_probe_result(core::InputImageProbeStatus::SourceReadFailed,
                                 error_message);
    }

    core::InputImageProbeResult result{};
    result.status = core::InputImageProbeStatus::Success;
    result.width = decoded_image.width;
    result.height = decoded_image.height;
    result.format = decoded_image.format;
    return result;
}

core::InputImageSaveResult Win32InputImageService::Save_input_image_to_file(
    core::InputImageSaveRequest const &request, std::wstring_view input_path,
    std::wstring_view output_path, core::ImageSaveFormat format) {
    if (input_path.empty() || output_path.empty()) {
        return Make_input_save_result(
            core::InputImageSaveStatus::SaveFailed,
            L"Error: Input and output paths are required for --input.");
    }

    DecodedInputImage decoded_image{};
    std::wstring error_message = {};
    if (!Try_decode_input_image(input_path, decoded_image, error_message)) {
        return Make_input_save_result(core::InputImageSaveStatus::SourceReadFailed,
                                      error_message);
    }

    GdiCaptureResult source_capture{};
    if (!Try_create_capture_from_input_image(decoded_image, source_capture,
                                             error_message)) {
        return Make_input_save_result(core::InputImageSaveStatus::SaveFailed,
                                      error_message);
    }

    core::CaptureSaveRequest capture_request{};
    capture_request.source_rect_screen =
        core::RectPx::From_ltrb(0, 0, decoded_image.width, decoded_image.height);
    capture_request.padding_px = request.padding_px;
    capture_request.fill_color = request.fill_color;
    capture_request.annotations = request.annotations;

    std::wstring const input_path_string(input_path);
    std::wstring const output_path_string(output_path);
    core::CaptureSaveResult save_result{};
    if (core::Equals_no_case(input_path_string, output_path_string)) {
        std::wstring temp_path = {};
        if (!Try_create_sibling_temp_path(output_path_string, temp_path,
                                          error_message)) {
            source_capture.Free();
            return Make_input_save_result(core::InputImageSaveStatus::SaveFailed,
                                          error_message);
        }

        save_result = Save_exact_source_capture_to_file(source_capture, capture_request,
                                                        nullptr, temp_path, format);
        if (save_result.status != core::CaptureSaveStatus::Success) {
            Delete_file_if_exists(temp_path);
            source_capture.Free();
            return Make_input_save_result(core::InputImageSaveStatus::SaveFailed,
                                          save_result.error_message);
        }

        if (!Try_replace_file(temp_path, output_path_string, error_message)) {
            Delete_file_if_exists(temp_path);
            source_capture.Free();
            return Make_input_save_result(core::InputImageSaveStatus::SaveFailed,
                                          error_message);
        }
    } else {
        save_result = Save_exact_source_capture_to_file(
            source_capture, capture_request, nullptr, output_path_string, format);
        if (save_result.status != core::CaptureSaveStatus::Success) {
            source_capture.Free();
            return Make_input_save_result(core::InputImageSaveStatus::SaveFailed,
                                          save_result.error_message);
        }
    }

    source_capture.Free();
    return Make_input_save_result(core::InputImageSaveStatus::Success);
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

std::wstring Win32FileSystemService::Get_app_config_file_path() const {
    return Get_config_file_path().wstring();
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
