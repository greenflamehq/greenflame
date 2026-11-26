// Save GdiCaptureResult to PNG or JPEG via Windows Imaging Component (WIC).

#include "win/save_image.h"

#include "greenflame_core/app_config.h"
#include "greenflame_core/save_image_policy.h"

#pragma comment(lib, "Windowscodecs.lib")

namespace greenflame {

namespace {

// Minimal RAII wrapper for COM pointers (auto-Release on scope exit).
template <typename T> struct ComPtr {
    T *p = nullptr;
    ComPtr() = default;
    ~ComPtr() {
        if (p) p->Release();
    }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    T **operator&() { return &p; }
    T *operator->() { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// RAII guard for CoInitializeEx / CoUninitialize.
struct CoInitGuard {
    bool owned;
    explicit CoInitGuard(bool b) : owned(b) {}
    ~CoInitGuard() {
        if (owned) CoUninitialize();
    }
    CoInitGuard(const CoInitGuard &) = delete;
    CoInitGuard &operator=(const CoInitGuard &) = delete;
};

bool Save_capture_via_wic(GdiCaptureResult const &capture, wchar_t const *path,
                          REFGUID container_format) {
    if (!capture.Is_valid() || !path) return false;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coinit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        coinit = false;
    } else if (FAILED(hr)) {
        return false;
    }
    CoInitGuard co_guard(coinit);

    ComPtr<IWICImagingFactory> factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    ComPtr<IWICBitmap> wic_bitmap;
    // GDI capture bitmaps are treated as opaque screenshots. Their alpha bytes are
    // not a reliable part of the image content, so preserve RGB and force opaque
    // output.
    hr = factory->CreateBitmapFromHBITMAP(
        capture.bitmap, nullptr, WICBitmapAlphaChannelOption::WICBitmapIgnoreAlpha,
        &wic_bitmap);
    if (FAILED(hr) || !wic_bitmap) return false;

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) return false;

    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(container_format, nullptr, &encoder);
    if (FAILED(hr) || !encoder) return false;

    hr = encoder->Initialize(stream.p, WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame_encode;
    IPropertyBag2 *props = nullptr;
    hr = encoder->CreateNewFrame(&frame_encode, &props);
    if (FAILED(hr) || !frame_encode) return false;
    if (props) {
        props->Release();
        props = nullptr;
    }

    hr = frame_encode->Initialize(nullptr);
    if (FAILED(hr)) return false;

    hr = frame_encode->SetSize(static_cast<UINT>(capture.width),
                               static_cast<UINT>(capture.height));
    if (FAILED(hr)) return false;

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    hr = frame_encode->SetPixelFormat(&pixel_format);
    if (FAILED(hr)) return false;

    hr = frame_encode->WriteSource(wic_bitmap.p, nullptr);
    if (FAILED(hr)) return false;

    hr = frame_encode->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    return SUCCEEDED(hr);
}

[[nodiscard]] std::wstring Build_suffixed_path(std::wstring_view path,
                                               uint32_t suffix) {
    size_t const last_separator = path.find_last_of(L"\\/");
    size_t const last_dot = path.find_last_of(L'.');
    bool const has_extension =
        last_dot != std::wstring_view::npos &&
        (last_separator == std::wstring_view::npos || last_dot > last_separator + 0);

    std::wstring out;
    if (has_extension) {
        out.assign(path.substr(0, last_dot));
    } else {
        out.assign(path);
    }
    out += L"-";
    out += std::to_wstring(suffix);
    if (has_extension) {
        out += path.substr(last_dot);
    }
    return out;
}

[[nodiscard]] std::wstring Try_reserve_exact_path(std::wstring_view path) noexcept {
    if (path.empty()) {
        return {};
    }
    std::wstring path_string(path);
    HANDLE const handle =
        CreateFileW(path_string.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return path_string;
    }
    return {};
}

} // namespace

bool Save_capture_to_png(GdiCaptureResult const &capture, wchar_t const *path) {
    return Save_capture_via_wic(capture, path, GUID_ContainerFormatPng);
}

bool Save_capture_to_file(GdiCaptureResult const &capture, wchar_t const *path,
                          core::ImageSaveFormat format) {
    if (!capture.Is_valid() || !path) return false;
    auto const write = [&](wchar_t const *target) {
        if (format == core::ImageSaveFormat::Jpeg) {
            return Save_capture_to_jpeg(capture, target);
        }
        if (format == core::ImageSaveFormat::Bmp) {
            return Save_capture_to_bmp(capture, target);
        }
        return Save_capture_to_png(capture, target);
    };

    // Written to a temporary file beside the target and moved over it only once
    // complete, so a failure never truncates or removes an existing file.
    std::wstring_view const target(path);
    size_t const last_separator = target.find_last_of(L"\\/");
    std::wstring const directory =
        last_separator == std::wstring_view::npos
            ? std::wstring(L".")
            : std::wstring(target.substr(0, last_separator + 1));
    // GetTempFileNameW needs the directory within MAX_PATH - 14 (the exe is not long
    // path aware). ponytail: a target in a longer directory is written directly, as
    // before; long-path support would lift this.
    constexpr size_t kMaxTempDirectoryChars = MAX_PATH - 14;
    if (directory.size() > kMaxTempDirectoryChars) {
        return write(path);
    }
    std::array<wchar_t, MAX_PATH> temp_path{};
    if (GetTempFileNameW(directory.c_str(), L"gf", 0, temp_path.data()) == 0) {
        return false;
    }
    if (!write(temp_path.data()) ||
        !MoveFileExW(temp_path.data(), path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temp_path.data());
        return false;
    }
    return true;
}

bool Save_capture_to_jpeg(GdiCaptureResult const &capture, wchar_t const *path) {
    return Save_capture_via_wic(capture, path, GUID_ContainerFormatJpeg);
}

std::wstring Reserve_unique_file_path(std::wstring_view desired_path) noexcept {
    if (desired_path.empty()) {
        return {};
    }
    if (std::wstring const exact = Try_reserve_exact_path(desired_path);
        !exact.empty()) {
        return exact;
    }
    DWORD const first_error = GetLastError();
    if (first_error != ERROR_FILE_EXISTS && first_error != ERROR_ALREADY_EXISTS) {
        return {};
    }

    constexpr uint32_t k_max_suffix = 10000;
    for (uint32_t suffix = 1; suffix <= k_max_suffix; ++suffix) {
        std::wstring const candidate = Build_suffixed_path(desired_path, suffix);
        if (std::wstring const reserved = Try_reserve_exact_path(candidate);
            !reserved.empty()) {
            return reserved;
        }
        DWORD const error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return {};
        }
    }
    return {};
}

std::vector<std::wstring> List_directory_filenames(std::wstring_view dir) {
    std::vector<std::wstring> result;
    std::wstring search_path(dir);
    if (!search_path.empty() && search_path.back() != L'\\') {
        search_path += L'\\';
    }
    search_path += L'*';
    WIN32_FIND_DATAW fd{};
    HANDLE const h = FindFirstFileW(search_path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return result;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            result.emplace_back(fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

std::wstring Resolve_initial_save_directory(core::AppConfig const *config) {
    if (config != nullptr && !config->last_save_as_dir.empty()) {
        return config->last_save_as_dir;
    }
    if (config != nullptr && !config->default_save_dir.empty()) {
        return config->default_save_dir;
    }

    wchar_t pictures_dir[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, pictures_dir);
    std::wstring dir = pictures_dir;
    dir += L"\\greenflame";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

} // namespace greenflame
