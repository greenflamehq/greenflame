#include "win/wgc_window_capture.h"

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

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE handle_ = nullptr;
};

class ScopedApartment final {
  public:
    explicit ScopedApartment(bool owns_apartment) noexcept
        : owns_apartment_(owns_apartment) {}
    ~ScopedApartment() {
        if (owns_apartment_) {
            winrt::uninit_apartment();
        }
    }

    ScopedApartment(ScopedApartment const &) = delete;
    ScopedApartment &operator=(ScopedApartment const &) = delete;

  private:
    bool owns_apartment_ = false;
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

[[nodiscard]] bool Try_create_capture_item(HWND hwnd, GraphicsCaptureItem &item,
                                           std::wstring &error_message) {
    if (hwnd == nullptr) {
        error_message = L"Error: WGC window capture requires a valid HWND.";
        return false;
    }

    try {
        auto const interop =
            winrt::get_activation_factory<GraphicsCaptureItem,
                                          IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(
            hwnd,
            winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
            winrt::put_abi(item)));
    } catch (winrt::hresult_error const &error) {
        error_message =
            Build_hresult_error(L"Error: Failed to create a WGC capture item for "
                                L"the target window",
                                error.code());
        return false;
    }
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

    size_t const destination_byte_count =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(content_size.Height);
    size_t const source_byte_count = static_cast<size_t>(map.Get().RowPitch) *
                                     static_cast<size_t>(content_size.Height);
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
    if (hwnd == nullptr || window_rect_screen.Is_empty()) {
        return Make_backend_failure(
            L"Error: WGC window capture requires a valid window target.");
    }

    bool owns_apartment = false;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        owns_apartment = true;
    } catch (winrt::hresult_error const &error) {
        if (error.code() != RPC_E_CHANGED_MODE) {
            return Make_backend_failure(Build_hresult_error(
                L"Error: Failed to initialize the WinRT apartment for CLI WGC "
                L"window capture",
                error.code()));
        }
    }
    ScopedApartment const apartment(owns_apartment);

    std::wstring failure_context = L"checking WGC support";
    try {
        if (!GraphicsCaptureSession::IsSupported()) {
            return Make_backend_failure(
                L"Error: WGC window capture is not supported on this system.");
        }

        int32_t expected_width = 0;
        int32_t expected_height = 0;
        if (!window_rect_screen.Try_get_size(expected_width, expected_height)) {
            return Make_backend_failure(
                L"Error: WGC window capture received an invalid window rect.");
        }

        Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context;
        std::wstring error_message = {};
        if (!Try_create_d3d11_device(d3d_device, d3d_context, error_message)) {
            return Make_backend_failure(error_message);
        }

        IDirect3DDevice winrt_device{nullptr};
        if (!Try_create_winrt_d3d_device(d3d_device.Get(), winrt_device,
                                         error_message)) {
            return Make_backend_failure(error_message);
        }

        GraphicsCaptureItem item{nullptr};
        if (!Try_create_capture_item(hwnd, item, error_message)) {
            return Make_backend_failure(error_message);
        }

        failure_context = L"reading the WGC capture item size";
        winrt::Windows::Graphics::SizeInt32 const initial_size = item.Size();
        if (initial_size.Width <= 0 || initial_size.Height <= 0) {
            return Make_backend_failure(
                L"Error: WGC window capture item reported an invalid window size.");
        }

        ScopedHandle frame_arrived_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!frame_arrived_event) {
            return Make_backend_failure(
                L"Error: Failed to create the WGC frame wait event.");
        }

        Direct3D11CaptureFrame captured_frame{nullptr};
        std::wstring frame_error_message = {};
        bool frame_ready = false;
        bool frame_failed = false;
        std::mutex frame_mutex;

        failure_context = L"creating the WGC frame pool";
        Direct3D11CaptureFramePool frame_pool =
            Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrt_device,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::
                    B8G8R8A8UIntNormalized,
                kWgcFramePoolBufferCount, initial_size);
        [[maybe_unused]] auto const frame_arrived_revoker = frame_pool.FrameArrived(
            winrt::auto_revoke, [&](Direct3D11CaptureFramePool const &sender,
                                    winrt::Windows::Foundation::IInspectable const &) {
                std::lock_guard<std::mutex> const lock(frame_mutex);
                if (frame_ready || frame_failed) {
                    return;
                }

                try {
                    captured_frame = sender.TryGetNextFrame();
                    if (!captured_frame) {
                        frame_failed = true;
                        frame_error_message =
                            L"Error: WGC window capture did not yield a frame.";
                    } else {
                        frame_ready = true;
                    }
                } catch (winrt::hresult_error const &error) {
                    frame_failed = true;
                    frame_error_message = Build_hresult_error(
                        L"Error: WGC window capture failed while acquiring a frame",
                        error.code());
                }

                (void)SetEvent(frame_arrived_event.Get());
            });

        failure_context = L"creating the WGC capture session";
        GraphicsCaptureSession const session = frame_pool.CreateCaptureSession(item);
        failure_context = L"starting the WGC capture session";
        session.StartCapture();

        DWORD const wait_result =
            WaitForSingleObject(frame_arrived_event.Get(), kWgcFrameTimeoutMs);
        if (wait_result == WAIT_TIMEOUT) {
            return Make_backend_failure(
                L"Error: WGC window capture timed out waiting for the first frame.");
        }
        if (wait_result != WAIT_OBJECT_0) {
            return Make_backend_failure(
                L"Error: WGC window capture failed while waiting for the first frame.");
        }

        Direct3D11CaptureFrame frame_to_convert{nullptr};
        {
            std::lock_guard<std::mutex> const lock(frame_mutex);
            if (frame_failed) {
                return Make_backend_failure(frame_error_message.empty()
                                                ? L"Error: WGC window capture "
                                                  L"failed while acquiring a frame."
                                                : frame_error_message);
            }
            if (!frame_ready || !captured_frame) {
                return Make_backend_failure(
                    L"Error: WGC window capture did not receive a usable frame.");
            }
            frame_to_convert = captured_frame;
        }

        failure_context = L"converting the WGC frame";
        if (!Try_copy_frame_to_capture(d3d_device.Get(), d3d_context.Get(),
                                       frame_to_convert, capture_out, error_message)) {
            capture_out.Free();
            return Make_backend_failure(error_message);
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
            return Make_backend_failure(message);
        }

        return Make_success();
    } catch (winrt::hresult_error const &error) {
        std::wstring prefix = L"Error: WGC window capture failed while ";
        prefix += failure_context;
        capture_out.Free();
        return Make_backend_failure(Build_hresult_error(prefix, error.code()));
    }
}

} // namespace greenflame
