#include "win/wgc_window_capture.h"
#include "win/debug_log.h"

namespace {

using Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

constexpr uint32_t kWgcFrameTimeoutMs = 2000u;
constexpr int32_t kWgcFramePoolBufferCount = 2;
constexpr UINT kWgcD3dDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
constexpr std::array<D3D_FEATURE_LEVEL, 6> kWgcFeatureLevels = {
    {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
     D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_1}};

class ScopedHandle final {
  public:
    explicit ScopedHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~ScopedHandle() { Reset(); }

    ScopedHandle(ScopedHandle const &) = delete;
    ScopedHandle &operator=(ScopedHandle const &) = delete;

    void Reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

    [[nodiscard]] HANDLE Release() noexcept {
        HANDLE const handle = handle_;
        handle_ = nullptr;
        return handle;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE handle_ = nullptr;
};

class FrameArrivalState final {
  public:
    explicit FrameArrivalState(HANDLE event_handle) noexcept
        : frame_arrived_event(event_handle) {}
    ~FrameArrivalState() {
        if (frame_arrived_event != nullptr &&
            frame_arrived_event != INVALID_HANDLE_VALUE) {
            CloseHandle(frame_arrived_event);
        }
    }

    FrameArrivalState(FrameArrivalState const &) = delete;
    FrameArrivalState &operator=(FrameArrivalState const &) = delete;

    HANDLE frame_arrived_event = nullptr;
    Direct3D11CaptureFrame captured_frame{nullptr};
    std::wstring frame_error_message = {};
    bool frame_ready = false;
    bool frame_failed = false;
    std::mutex frame_mutex = {};
};

class ScopedTextureMap final {
  public:
    ScopedTextureMap() = default;
    ~ScopedTextureMap() { Reset(); }

    ScopedTextureMap(ScopedTextureMap const &) = delete;
    ScopedTextureMap &operator=(ScopedTextureMap const &) = delete;

    bool Try_map(ID3D11DeviceContext *context, ID3D11Texture2D *texture) noexcept {
        Reset();
        if (context == nullptr || texture == nullptr) {
            return false;
        }

        HRESULT const hr =
            context->Map(texture, 0, D3D11_MAP_READ, 0, &mapped_resource_);
        if (FAILED(hr)) {
            return false;
        }

        context_ = context;
        texture_ = texture;
        mapped_ = true;
        return true;
    }

    void Reset() noexcept {
        if (mapped_ && context_ != nullptr && texture_ != nullptr) {
            context_->Unmap(texture_, 0);
        }
        mapped_ = false;
        context_ = nullptr;
        texture_ = nullptr;
        mapped_resource_ = {};
    }

    [[nodiscard]] D3D11_MAPPED_SUBRESOURCE const &Get() const noexcept {
        return mapped_resource_;
    }

  private:
    ID3D11DeviceContext *context_ = nullptr;
    ID3D11Texture2D *texture_ = nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped_resource_ = {};
    bool mapped_ = false;
};

enum class WgcSupportState : uint8_t {
    Unknown = 0,
    Supported = 1,
    Unsupported = 2,
    SehFailed = 3,
};

enum class WgcApartmentState : uint8_t {
    Unknown = 0,
    InitializedSta = 1,
    ExistingApartment = 2,
};

[[nodiscard]] std::wstring Build_hresult_error(std::wstring_view prefix, HRESULT hr);

[[nodiscard]] std::wstring Trim_trailing_wspace(std::wstring text) {
    while (!text.empty() && std::iswspace(text.back()) != 0) {
        text.pop_back();
    }
    return text;
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
        auto const value = static_cast<uint32_t>(hr);
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

[[nodiscard]] std::wstring Format_exception_code(DWORD code) {
    constexpr std::array<wchar_t, 16> hex_chars = {{L'0', L'1', L'2', L'3', L'4', L'5',
                                                    L'6', L'7', L'8', L'9', L'A', L'B',
                                                    L'C', L'D', L'E', L'F'}};
    constexpr int nibbles = 8;
    constexpr uint32_t nibble_mask = 0xFu;
    std::wstring message = L"0x00000000";
    for (int i = 0; i < nibbles; ++i) {
        message[message.size() - 1u - static_cast<size_t>(i)] =
            hex_chars[(code >> (i * 4)) & nibble_mask];
    }
    return message;
}

#define LOG_WGC_MESSAGE(message_expression)                                            \
    GREENFLAME_LOG_WRITE(L"wgc", (message_expression))

[[nodiscard]] std::mutex &Wgc_support_mutex() noexcept {
    static std::mutex mutex = {};
    return mutex;
}

[[nodiscard]] WgcSupportState &Cached_wgc_support_state() noexcept {
    static WgcSupportState state = WgcSupportState::Unknown;
    return state;
}

[[nodiscard]] WgcApartmentState &Capture_thread_apartment_state() noexcept {
    thread_local WgcApartmentState state = WgcApartmentState::Unknown;
    return state;
}

LONG Capture_support_probe_exception(EXCEPTION_POINTERS *exception_pointers,
                                     DWORD *exception_code) noexcept {
    if (exception_code != nullptr) {
        *exception_code = (exception_pointers != nullptr &&
                           exception_pointers->ExceptionRecord != nullptr)
                              ? exception_pointers->ExceptionRecord->ExceptionCode
                              : EXCEPTION_ACCESS_VIOLATION;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

[[nodiscard]] bool Probe_wgc_support_with_seh(bool &is_supported,
                                              DWORD &exception_code) noexcept {
    is_supported = false;
    exception_code = ERROR_SUCCESS;
    __try {
        is_supported = GraphicsCaptureSession::IsSupported();
        return true;
    } __except (
        Capture_support_probe_exception(GetExceptionInformation(), &exception_code)) {
        return false;
    }
}

[[nodiscard]] bool
Ensure_wgc_support(std::wstring &error_message,
                   [[maybe_unused]] std::wstring const &log_context) {
    std::lock_guard<std::mutex> const lock(Wgc_support_mutex());
    WgcSupportState &cached_state = Cached_wgc_support_state();
    switch (cached_state) {
    case WgcSupportState::Supported:
        LOG_WGC_MESSAGE(log_context + L" support_cache_hit=supported");
        return true;
    case WgcSupportState::Unsupported:
        LOG_WGC_MESSAGE(log_context + L" support_cache_hit=unsupported");
        error_message = L"Error: WGC window capture is not supported on this system.";
        return false;
    case WgcSupportState::SehFailed:
        LOG_WGC_MESSAGE(log_context + L" support_cache_hit=seh_failed");
        error_message =
            L"Error: WGC support probing previously faulted in this process.";
        return false;
    case WgcSupportState::Unknown:
        break;
    }

    LOG_WGC_MESSAGE(log_context + L" checking_support");
    bool is_supported = false;
    DWORD exception_code = ERROR_SUCCESS;
    if (!Probe_wgc_support_with_seh(is_supported, exception_code)) {
        cached_state = WgcSupportState::SehFailed;
        error_message = L"Error: WGC support probing faulted with exception " +
                        Format_exception_code(exception_code) + L".";
        LOG_WGC_MESSAGE(log_context + L" support_check_seh=" +
                        Format_exception_code(exception_code));
        return false;
    }

    cached_state =
        is_supported ? WgcSupportState::Supported : WgcSupportState::Unsupported;
    LOG_WGC_MESSAGE(log_context + std::wstring(L" support_check_result=") +
                    (is_supported ? L"supported" : L"unsupported"));
    if (!is_supported) {
        error_message = L"Error: WGC window capture is not supported on this system.";
        return false;
    }
    return true;
}

[[nodiscard]] bool
Ensure_wgc_capture_thread_apartment(std::wstring &error_message,
                                    [[maybe_unused]] std::wstring const &log_context) {
    WgcApartmentState &cached_state = Capture_thread_apartment_state();
    switch (cached_state) {
    case WgcApartmentState::InitializedSta:
    case WgcApartmentState::ExistingApartment:
        LOG_WGC_MESSAGE(log_context +
                        (cached_state == WgcApartmentState::InitializedSta
                             ? std::wstring(L" apartment_cache_hit=initialized_sta")
                             : std::wstring(L" apartment_cache_hit=existing")));
        return true;
    case WgcApartmentState::Unknown:
        break;
    }

    try {
        // Keep the apartment alive for the UI thread lifetime. Repeated
        // init/uninit around interactive preview destabilized the capture-item
        // activation path on subsequent captures.
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        cached_state = WgcApartmentState::InitializedSta;
        LOG_WGC_MESSAGE(log_context + L" apartment_initialized=sta");
        return true;
    } catch (winrt::hresult_error const &error) {
        if (error.code() == RPC_E_CHANGED_MODE) {
            cached_state = WgcApartmentState::ExistingApartment;
            LOG_WGC_MESSAGE(log_context + L" apartment_initialized=existing");
            return true;
        }

        error_message =
            Build_hresult_error(L"Error: Failed to initialize the WinRT apartment "
                                L"for WGC window capture",
                                error.code());
        return false;
    }
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

[[nodiscard]] greenflame::core::CaptureSaveResult
Make_backend_failure(std::wstring const &message) {
    return greenflame::core::CaptureSaveResult{
        greenflame::core::CaptureSaveStatus::BackendFailed, message};
}

[[nodiscard]] greenflame::core::CaptureSaveResult Make_success() {
    return greenflame::core::CaptureSaveResult{
        greenflame::core::CaptureSaveStatus::Success, {}};
}

[[nodiscard]] bool
Try_create_d3d11_device(Microsoft::WRL::ComPtr<ID3D11Device> &device,
                        Microsoft::WRL::ComPtr<ID3D11DeviceContext> &context,
                        std::wstring &error_message) {
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, kWgcD3dDeviceFlags,
        kWgcFeatureLevels.data(), static_cast<UINT>(kWgcFeatureLevels.size()),
        D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, kWgcD3dDeviceFlags,
            kWgcFeatureLevels.data(), static_cast<UINT>(kWgcFeatureLevels.size()),
            D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
    }
    if (FAILED(hr) || !device || !context) {
        error_message =
            Build_hresult_error(L"Error: Failed to initialize Direct3D for CLI "
                                L"WGC window capture",
                                hr);
        return false;
    }
    return true;
}

[[nodiscard]] bool Try_create_winrt_d3d_device(ID3D11Device *device,
                                               IDirect3DDevice &winrt_device,
                                               std::wstring &error_message) {
    if (device == nullptr) {
        error_message = L"Error: WGC Direct3D device is null.";
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(dxgi_device.GetAddressOf()));
    if (FAILED(hr) || !dxgi_device) {
        error_message =
            Build_hresult_error(L"Error: Failed to query IDXGIDevice for CLI "
                                L"WGC window capture",
                                hr);
        return false;
    }

    winrt::com_ptr<IInspectable> inspectable_device;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(),
                                              inspectable_device.put());
    if (FAILED(hr) || !inspectable_device) {
        error_message = Build_hresult_error(
            L"Error: Failed to create a WinRT Direct3D device for CLI WGC "
            L"window capture",
            hr);
        return false;
    }

    try {
        winrt_device = inspectable_device.as<IDirect3DDevice>();
    } catch (winrt::hresult_error const &error) {
        error_message = Build_hresult_error(
            L"Error: Failed to bridge the Direct3D device into the CLI WGC "
            L"window capture pipeline",
            error.code());
        return false;
    }
    return true;
}

[[nodiscard]] bool
Try_create_capture_item(HWND hwnd, GraphicsCaptureItem &item,
                        std::wstring &error_message,
                        [[maybe_unused]] std::wstring const &log_context) {
    if (hwnd == nullptr) {
        error_message = L"Error: WGC window capture requires a valid HWND.";
        return false;
    }

    LOG_WGC_MESSAGE(log_context + L" acquiring_capture_item_factory");
    std::wstring_view const class_name_view = winrt::name_of<GraphicsCaptureItem>();
    winrt::hstring const class_name(
        class_name_view.data(),
        static_cast<winrt::hstring::size_type>(class_name_view.size()));
    winrt::com_ptr<IGraphicsCaptureItemInterop> interop;
    HRESULT const factory_hr = RoGetActivationFactory(
        reinterpret_cast<HSTRING>(winrt::get_abi(class_name)),
        __uuidof(IGraphicsCaptureItemInterop), interop.put_void());
    if (FAILED(factory_hr) || !interop) {
        error_message =
            Build_hresult_error(L"Error: Failed to acquire the WGC capture-item "
                                L"activation factory",
                                factory_hr);
        return false;
    }
    LOG_WGC_MESSAGE(log_context + L" capture_item_factory_ready");

    LOG_WGC_MESSAGE(log_context + L" invoking_CreateForWindow");
    HRESULT const create_hr = interop->CreateForWindow(
        hwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item));
    if (FAILED(create_hr)) {
        error_message =
            Build_hresult_error(L"Error: Failed to create a WGC capture item for "
                                L"the target window",
                                create_hr);
        return false;
    }
    LOG_WGC_MESSAGE(log_context + L" CreateForWindow_succeeded");
    return true;
}

[[nodiscard]] bool Try_get_texture_from_surface(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const &surface,
    Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture, std::wstring &error_message) {
    if (!surface) {
        error_message =
            L"Error: WGC window capture returned an empty Direct3D surface.";
        return false;
    }

    try {
        auto const access = surface.as<IDirect3DDxgiInterfaceAccess>();
        HRESULT const hr =
            access->GetInterface(IID_PPV_ARGS(texture.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || !texture) {
            error_message =
                Build_hresult_error(L"Error: Failed to read the WGC surface as "
                                    L"an ID3D11Texture2D",
                                    hr);
            return false;
        }
    } catch (winrt::hresult_error const &error) {
        error_message = Build_hresult_error(L"Error: Failed to query the WGC surface "
                                            L"interop interface",
                                            error.code());
        return false;
    }
    return true;
}

[[nodiscard]] bool Try_create_top_down_capture(int32_t width, int32_t height,
                                               greenflame::GdiCaptureResult &capture,
                                               void *&bitmap_bits,
                                               std::wstring &error_message) {
    if (width <= 0 || height <= 0) {
        error_message = L"Error: WGC window capture returned an invalid frame size.";
        return false;
    }

    BITMAPINFO bitmap_info = {};
    greenflame::Fill_bmi32_top_down(bitmap_info.bmiHeader, width, height);

    HDC const screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        error_message =
            L"Error: Failed to acquire a screen DC while creating the WGC window "
            L"capture bitmap.";
        return false;
    }

    HBITMAP const bitmap = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS,
                                            &bitmap_bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);

    if (bitmap == nullptr || bitmap_bits == nullptr) {
        error_message = L"Error: Failed to create a 32bpp DIB for the WGC window "
                        L"capture frame.";
        return false;
    }

    capture.Free();
    capture.bitmap = bitmap;
    capture.width = width;
    capture.height = height;
    return true;
}

[[nodiscard]] bool Try_copy_frame_to_capture(ID3D11Device *device,
                                             ID3D11DeviceContext *context,
                                             Direct3D11CaptureFrame const &frame,
                                             greenflame::GdiCaptureResult &capture,
                                             std::wstring &error_message) {
    if (device == nullptr || context == nullptr || !frame) {
        error_message =
            L"Error: WGC window capture frame conversion received invalid state.";
        return false;
    }

    winrt::Windows::Graphics::SizeInt32 const content_size = frame.ContentSize();
    if (content_size.Width <= 0 || content_size.Height <= 0) {
        error_message = L"Error: WGC window capture returned an empty frame.";
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_texture;
    if (!Try_get_texture_from_surface(frame.Surface(), source_texture, error_message)) {
        return false;
    }

    D3D11_TEXTURE2D_DESC source_desc = {};
    source_texture->GetDesc(&source_desc);
    if (source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        error_message =
            L"Error: WGC window capture returned an unexpected pixel format.";
        return false;
    }

    D3D11_TEXTURE2D_DESC staging_desc = source_desc;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    staging_desc.Usage = D3D11_USAGE_STAGING;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;
    HRESULT const create_texture_hr =
        device->CreateTexture2D(&staging_desc, nullptr, staging_texture.GetAddressOf());
    if (FAILED(create_texture_hr) || !staging_texture) {
        error_message = Build_hresult_error(
            L"Error: Failed to create a staging texture for the WGC window "
            L"capture frame",
            create_texture_hr);
        return false;
    }

    context->CopyResource(staging_texture.Get(), source_texture.Get());

    ScopedTextureMap map;
    if (!map.Try_map(context, staging_texture.Get())) {
        error_message = L"Error: Failed to map the WGC window capture staging "
                        L"texture for CPU readback.";
        return false;
    }

    void *bitmap_bits = nullptr;
    if (!Try_create_top_down_capture(content_size.Width, content_size.Height, capture,
                                     bitmap_bits, error_message)) {
        return false;
    }

    int const row_bytes = greenflame::Row_bytes32(content_size.Width);
    if (row_bytes <= 0) {
        capture.Free();
        error_message = L"Error: Failed to compute WGC window capture row bytes.";
        return false;
    }
    if (map.Get().RowPitch < static_cast<UINT>(row_bytes)) {
        capture.Free();
        error_message =
            L"Error: WGC window capture returned an invalid staging row pitch.";
        return false;
    }

    size_t const destination_byte_count =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(content_size.Height);
    size_t const source_row_bytes = static_cast<size_t>(map.Get().RowPitch);
    size_t const height_size = static_cast<size_t>(content_size.Height);
    if (height_size > std::numeric_limits<size_t>::max() / source_row_bytes) {
        capture.Free();
        error_message = L"Error: WGC window capture source frame size overflowed.";
        return false;
    }
    size_t const source_byte_count = source_row_bytes * height_size;
    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage-in-container")
    std::span<uint8_t> destination_bytes{reinterpret_cast<uint8_t *>(bitmap_bits),
                                         destination_byte_count};
    std::span<uint8_t const> source_bytes{
        reinterpret_cast<uint8_t const *>(map.Get().pData), source_byte_count};
    CLANG_WARN_IGNORE_POP()
    for (int32_t row = 0; row < content_size.Height; ++row) {
        size_t const source_offset =
            static_cast<size_t>(row) * static_cast<size_t>(map.Get().RowPitch);
        size_t const destination_offset =
            static_cast<size_t>(row) * static_cast<size_t>(row_bytes);
        std::span<uint8_t const> const source_row =
            source_bytes.subspan(source_offset, static_cast<size_t>(row_bytes));
        std::span<uint8_t> const destination_row = destination_bytes.subspan(
            destination_offset, static_cast<size_t>(row_bytes));
        std::copy_n(source_row.begin(), row_bytes, destination_row.begin());
    }

    return true;
}

} // namespace

namespace greenflame {

core::CaptureSaveResult Capture_window_with_wgc(HWND hwnd,
                                                core::RectPx window_rect_screen,
                                                GdiCaptureResult &capture_out) {
    capture_out.Free();
    core::RectPx const normalized_window_rect = window_rect_screen.Normalized();
#if defined(GREENFLAME_LOG)
    std::wstring const log_context = std::wstring(L"hwnd=") +
                                     Format_hwnd_for_debug_log(hwnd) + L" rect=" +
                                     Format_rect_for_debug_log(normalized_window_rect);
#else
    std::wstring const log_context = {};
#endif
    auto log_and_fail = [&](std::wstring const &message) {
        LOG_WGC_MESSAGE(log_context + L" failure: " + message);
        return Make_backend_failure(message);
    };

    if (hwnd == nullptr || normalized_window_rect.Is_empty()) {
        return log_and_fail(
            L"Error: WGC window capture requires a valid window target.");
    }
    LOG_WGC_MESSAGE(log_context + L" begin");

    std::wstring error_message = {};
    if (!Ensure_wgc_capture_thread_apartment(error_message, log_context)) {
        return log_and_fail(error_message);
    }
    LOG_WGC_MESSAGE(log_context + L" apartment_ready");

    std::wstring failure_context = L"checking WGC support";
    try {
        error_message.clear();
        if (!Ensure_wgc_support(error_message, log_context)) {
            return log_and_fail(error_message);
        }
        LOG_WGC_MESSAGE(log_context + L" support_confirmed");

        int32_t expected_width = 0;
        int32_t expected_height = 0;
        if (!normalized_window_rect.Try_get_size(expected_width, expected_height)) {
            return log_and_fail(
                L"Error: WGC window capture received an invalid window rect.");
        }
        LOG_WGC_MESSAGE(log_context + L" expected_size=" +
                        std::to_wstring(expected_width) + L"x" +
                        std::to_wstring(expected_height));

        Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context;
        error_message.clear();
        LOG_WGC_MESSAGE(log_context + L" creating_d3d_device");
        if (!Try_create_d3d11_device(d3d_device, d3d_context, error_message)) {
            return log_and_fail(error_message);
        }
        LOG_WGC_MESSAGE(log_context + L" d3d_device_ready");

        IDirect3DDevice winrt_device{nullptr};
        LOG_WGC_MESSAGE(log_context + L" creating_winrt_device");
        if (!Try_create_winrt_d3d_device(d3d_device.Get(), winrt_device,
                                         error_message)) {
            return log_and_fail(error_message);
        }
        LOG_WGC_MESSAGE(log_context + L" winrt_device_ready");

        GraphicsCaptureItem item{nullptr};
        LOG_WGC_MESSAGE(log_context + L" creating_capture_item");
        if (!Try_create_capture_item(hwnd, item, error_message, log_context)) {
            return log_and_fail(error_message);
        }
        LOG_WGC_MESSAGE(log_context + L" capture_item_ready");

        failure_context = L"reading the WGC capture item size";
        LOG_WGC_MESSAGE(log_context + L" reading_item_size");
        winrt::Windows::Graphics::SizeInt32 const initial_size = item.Size();
        if (initial_size.Width <= 0 || initial_size.Height <= 0) {
            return log_and_fail(
                L"Error: WGC window capture item reported an invalid window size.");
        }
        LOG_WGC_MESSAGE(log_context + L" item_size=" +
                        std::to_wstring(initial_size.Width) + L"x" +
                        std::to_wstring(initial_size.Height));

        ScopedHandle frame_arrived_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!frame_arrived_event) {
            return log_and_fail(L"Error: Failed to create the WGC frame wait event.");
        }
        std::shared_ptr<FrameArrivalState> const frame_state =
            std::make_shared<FrameArrivalState>(frame_arrived_event.Release());

        failure_context = L"creating the WGC frame pool";
        Direct3D11CaptureFramePool frame_pool =
            Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrt_device,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::
                    B8G8R8A8UIntNormalized,
                kWgcFramePoolBufferCount, initial_size);
        [[maybe_unused]] auto frame_arrived_revoker = frame_pool.FrameArrived(
            winrt::auto_revoke, [frame_state, log_context](
                                    Direct3D11CaptureFramePool const &sender,
                                    winrt::Windows::Foundation::IInspectable const &) {
                std::lock_guard<std::mutex> const lock(frame_state->frame_mutex);
                if (frame_state->frame_ready || frame_state->frame_failed) {
                    return;
                }

                try {
                    frame_state->captured_frame = sender.TryGetNextFrame();
                    if (!frame_state->captured_frame) {
                        frame_state->frame_failed = true;
                        frame_state->frame_error_message =
                            L"Error: WGC window capture did not yield a frame.";
                        LOG_WGC_MESSAGE(log_context +
                                        L" FrameArrived returned an empty frame");
                    } else {
                        frame_state->frame_ready = true;
                        LOG_WGC_MESSAGE(log_context + L" FrameArrived captured frame");
                    }
                } catch (winrt::hresult_error const &error) {
                    frame_state->frame_failed = true;
                    frame_state->frame_error_message = Build_hresult_error(
                        L"Error: WGC window capture failed while acquiring a frame",
                        error.code());
                    LOG_WGC_MESSAGE(log_context + L" FrameArrived hresult failure");
                } catch (std::bad_alloc const &) {
                    frame_state->frame_failed = true;
                    LOG_WGC_MESSAGE(log_context + L" FrameArrived bad_alloc");
                } catch (...) {
                    frame_state->frame_failed = true;
                    LOG_WGC_MESSAGE(log_context +
                                    L" FrameArrived unexpected exception");
                }

                if (frame_state->frame_arrived_event != nullptr &&
                    frame_state->frame_arrived_event != INVALID_HANDLE_VALUE) {
                    (void)SetEvent(frame_state->frame_arrived_event);
                }
            });

        failure_context = L"creating the WGC capture session";
        GraphicsCaptureSession session = frame_pool.CreateCaptureSession(item);
        failure_context = L"disabling WGC cursor capture";
        session.IsCursorCaptureEnabled(false);
        LOG_WGC_MESSAGE(log_context + L" cursor_capture_disabled");
        failure_context = L"starting the WGC capture session";
        session.StartCapture();
        LOG_WGC_MESSAGE(log_context + L" session started");

        auto stop_capture = [&]() noexcept {
            frame_arrived_revoker.revoke();
            try {
                session.Close();
            } catch ([[maybe_unused]] winrt::hresult_error const &error) {
                LOG_WGC_MESSAGE(log_context + L" session.Close hresult failure " +
                                std::to_wstring(static_cast<uint32_t>(error.code())));
            } catch (...) {
                LOG_WGC_MESSAGE(log_context + L" session.Close unexpected exception");
            }
            try {
                frame_pool.Close();
            } catch ([[maybe_unused]] winrt::hresult_error const &error) {
                LOG_WGC_MESSAGE(log_context + L" frame_pool.Close hresult failure " +
                                std::to_wstring(static_cast<uint32_t>(error.code())));
            } catch (...) {
                LOG_WGC_MESSAGE(log_context +
                                L" frame_pool.Close unexpected exception");
            }
        };

        DWORD const wait_result =
            WaitForSingleObject(frame_state->frame_arrived_event, kWgcFrameTimeoutMs);
        stop_capture();
        LOG_WGC_MESSAGE(log_context + L" wait_result=" + std::to_wstring(wait_result));
        if (wait_result == WAIT_TIMEOUT) {
            return log_and_fail(
                L"Error: WGC window capture timed out waiting for the first frame.");
        }
        if (wait_result != WAIT_OBJECT_0) {
            if (wait_result == WAIT_FAILED) {
                LOG_WGC_MESSAGE(log_context + L" wait_failed_last_error=" +
                                std::to_wstring(GetLastError()));
            }
            return log_and_fail(
                L"Error: WGC window capture failed while waiting for the first frame.");
        }
        LOG_WGC_MESSAGE(log_context + L" first frame signaled");

        Direct3D11CaptureFrame frame_to_convert{nullptr};
        {
            std::lock_guard<std::mutex> const lock(frame_state->frame_mutex);
            if (frame_state->frame_failed) {
                return log_and_fail(frame_state->frame_error_message.empty()
                                        ? L"Error: WGC window capture failed while "
                                          L"acquiring a frame."
                                        : frame_state->frame_error_message);
            }
            if (!frame_state->frame_ready || !frame_state->captured_frame) {
                return log_and_fail(
                    L"Error: WGC window capture did not receive a usable frame.");
            }
            frame_to_convert = frame_state->captured_frame;
        }

        failure_context = L"converting the WGC frame";
        if (!Try_copy_frame_to_capture(d3d_device.Get(), d3d_context.Get(),
                                       frame_to_convert, capture_out, error_message)) {
            capture_out.Free();
            return log_and_fail(error_message);
        }

        int const actual_width = capture_out.width;
        int const actual_height = capture_out.height;
        if (actual_width != expected_width || actual_height != expected_height) {
            capture_out.Free();
            std::wstring message = L"Error: WGC window capture returned ";
            message += std::to_wstring(actual_width);
            message += L"x";
            message += std::to_wstring(actual_height);
            message += L" pixels, but the target window rect is ";
            message += std::to_wstring(expected_width);
            message += L"x";
            message += std::to_wstring(expected_height);
            message += L".";
            return log_and_fail(message);
        }

        LOG_WGC_MESSAGE(log_context + L" success capture_size=" +
                        std::to_wstring(actual_width) + L"x" +
                        std::to_wstring(actual_height));
        return Make_success();
    } catch (winrt::hresult_error const &error) {
        std::wstring prefix = L"Error: WGC window capture failed while ";
        prefix += failure_context;
        capture_out.Free();
        return log_and_fail(Build_hresult_error(prefix, error.code()));
    } catch (std::bad_alloc const &) {
        capture_out.Free();
        return log_and_fail(L"Error: WGC window capture ran out of memory.");
    } catch (std::exception const &) {
        capture_out.Free();
        std::wstring message = L"Error: WGC window capture hit a standard exception.";
        LOG_WGC_MESSAGE(log_context + L" std::exception while " + failure_context);
        return Make_backend_failure(message);
    } catch (...) {
        capture_out.Free();
        return log_and_fail(L"Error: WGC window capture hit an unexpected exception.");
    }
}

} // namespace greenflame
