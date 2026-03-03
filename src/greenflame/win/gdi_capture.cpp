// Phase 3.1: GDI full-screen capture. Virtual desktop bounds, then
// GetDC(GetDesktopWindow) -> CreateCompatibleDC -> CreateDIBSection ->
// BitBlt(SRCCOPY | CAPTUREBLT). Save_capture_to_bmp for validation.

#include "gdi_capture.h"
#include "greenflame_core/bmp.h"
#include "greenflame_core/rect_px.h"
#include "win/display_queries.h"

namespace greenflame {

namespace {

constexpr DWORD kCaptureRop = SRCCOPY | CAPTUREBLT;

} // namespace

void Fill_bmi32_top_down(BITMAPINFOHEADER &bmi, int width, int height) {
    bmi = {};
    bmi.biSize = sizeof(BITMAPINFOHEADER);
    bmi.biWidth = width;
    bmi.biHeight = -height; // top-down
    bmi.biPlanes = 1;
    bmi.biBitCount = 32;
    bmi.biCompression = BI_RGB;
}

int Row_bytes32(int width) { return (width * 4 + 3) & ~3; }

void GdiCaptureResult::Free() noexcept {
    if (bitmap) {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }
    width = 0;
    height = 0;
}

bool Capture_virtual_desktop(GdiCaptureResult &out) {
    out.Free();

    greenflame::core::RectPx bounds = Get_virtual_desktop_bounds_px();
    int const w = bounds.Width();
    int const h = bounds.Height();
    if (w <= 0 || h <= 0) return false;

    HDC const desktop_dc = GetDC(GetDesktopWindow());
    if (!desktop_dc) return false;

    bool ok = false;
    HBITMAP dib = nullptr;
    HDC const mem_dc = CreateCompatibleDC(desktop_dc);
    if (mem_dc) {
        BITMAPINFOHEADER bmi;
        Fill_bmi32_top_down(bmi, w, h);
        void *bits = nullptr;
        dib = CreateDIBSection(desktop_dc, reinterpret_cast<BITMAPINFO *>(&bmi),
                               DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib && bits) {
            HGDIOBJ const old = SelectObject(mem_dc, dib);
            if (old && old != HGDI_ERROR) {
                ok = BitBlt(mem_dc, 0, 0, w, h, desktop_dc, bounds.left, bounds.top,
                            kCaptureRop) != 0;
                SelectObject(mem_dc, old);
            }
        }
        DeleteDC(mem_dc);
    }
    ReleaseDC(GetDesktopWindow(), desktop_dc);

    if (ok) {
        out.bitmap = dib;
        out.width = w;
        out.height = h;
    } else if (dib) {
        DeleteObject(dib);
    }
    return ok;
}

bool Save_capture_to_bmp(GdiCaptureResult const &capture, wchar_t const *path) {
    if (!capture.Is_valid() || !path) return false;

    HDC const dc = GetDC(nullptr);
    if (!dc) return false;

    int const row_bytes = Row_bytes32(capture.width);
    size_t const image_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(capture.height);

    BITMAPINFOHEADER info;
    Fill_bmi32_top_down(info, capture.width, capture.height);
    info.biHeight = capture.height; // positive = bottom-up for BMP file

    std::vector<uint8_t> pixels(image_size);
    if (GetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                  pixels.data(), reinterpret_cast<BITMAPINFO *>(&info),
                  DIB_RGB_COLORS) == 0) {
        ReleaseDC(nullptr, dc);
        return false;
    }
    ReleaseDC(nullptr, dc);

    std::vector<uint8_t> bmp_bytes = greenflame::core::Build_bmp_bytes(
        pixels, capture.width, capture.height, row_bytes);
    if (bmp_bytes.empty()) return false;

    HANDLE const f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    bool ok = WriteFile(f, bmp_bytes.data(), static_cast<DWORD>(bmp_bytes.size()),
                        &written, nullptr) &&
              written == static_cast<DWORD>(bmp_bytes.size());
    CloseHandle(f);
    return ok;
}

bool Crop_capture(GdiCaptureResult const &source, int left, int top, int width,
                  int height, GdiCaptureResult &out) {
    out.Free();
    if (width <= 0 || height <= 0 || !source.Is_valid()) return false;
    if (left < 0 || top < 0 || left + width > source.width ||
        top + height > source.height) {
        return false;
    }

    HDC const dc = GetDC(nullptr);
    if (!dc) return false;

    bool ok = false;
    HBITMAP dib = nullptr;
    HDC const src_dc = CreateCompatibleDC(dc);
    if (src_dc) {
        HGDIOBJ const src_old = SelectObject(src_dc, source.bitmap);
        if (src_old && src_old != HGDI_ERROR) {
            BITMAPINFOHEADER bmi;
            Fill_bmi32_top_down(bmi, width, height);
            void *bits = nullptr;
            dib = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&bmi),
                                   DIB_RGB_COLORS, &bits, nullptr, 0);
            if (dib) {
                HDC const dst_dc = CreateCompatibleDC(dc);
                if (dst_dc) {
                    HGDIOBJ const dst_old = SelectObject(dst_dc, dib);
                    if (dst_old && dst_old != HGDI_ERROR) {
                        ok = BitBlt(dst_dc, 0, 0, width, height, src_dc, left, top,
                                    SRCCOPY) != 0;
                        SelectObject(dst_dc, dst_old);
                    }
                    DeleteDC(dst_dc);
                }
            }
            SelectObject(src_dc, src_old);
        }
        DeleteDC(src_dc);
    }
    ReleaseDC(nullptr, dc);

    if (ok) {
        out.bitmap = dib;
        out.width = width;
        out.height = height;
    } else if (dib) {
        DeleteObject(dib);
    }
    return ok;
}

bool Copy_capture_to_clipboard(GdiCaptureResult const &capture, HWND owner_window) {
    if (!capture.Is_valid()) {
        return false;
    }

    int const row_bytes = Row_bytes32(capture.width);
    size_t const image_size =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(capture.height);
    BITMAPINFOHEADER info{};
    Fill_bmi32_top_down(info, capture.width, capture.height);
    info.biHeight = capture.height;

    HGLOBAL memory = nullptr;
    HDC const dc = GetDC(nullptr);
    if (dc != nullptr) {
        size_t const dib_size = sizeof(BITMAPINFOHEADER) + image_size;
        memory = GlobalAlloc(GMEM_MOVEABLE, dib_size);
        if (memory != nullptr) {
            void *const raw = GlobalLock(memory);
            bool ok = false;
            if (raw != nullptr) {
                std::vector<uint8_t> buf(dib_size);
                std::span<uint8_t> buf_span(buf);
                std::copy_n(reinterpret_cast<uint8_t const *>(&info),
                            sizeof(BITMAPINFOHEADER), buf_span.begin());
                ok = GetDIBits(dc, capture.bitmap, 0, static_cast<UINT>(capture.height),
                               buf_span.subspan(sizeof(BITMAPINFOHEADER)).data(),
                               reinterpret_cast<BITMAPINFO *>(&info),
                               DIB_RGB_COLORS) != 0;
                if (ok) {
                    std::copy(buf.begin(), buf.end(), static_cast<uint8_t *>(raw));
                }
                GlobalUnlock(memory);
            }
            if (!ok) {
                GlobalFree(memory);
                memory = nullptr;
            }
        }
        ReleaseDC(nullptr, dc);
    }

    bool copied_to_clipboard = false;
    HWND const clipboard_owner =
        (owner_window != nullptr && IsWindow(owner_window) != 0) ? owner_window
                                                                 : nullptr;
    if (memory != nullptr && OpenClipboard(clipboard_owner) != 0) {
        if (EmptyClipboard() != 0 && SetClipboardData(CF_DIB, memory) != nullptr) {
            copied_to_clipboard = true;
            memory = nullptr; // Clipboard owns memory after SetClipboardData succeeds.
        }
        CloseClipboard();
    }
    if (memory != nullptr) {
        GlobalFree(memory);
    }
    return copied_to_clipboard;
}

HBITMAP Scale_bitmap_to_thumbnail(HBITMAP src_bitmap, int src_width, int src_height,
                                  int max_width, int max_height) {
    float const scale_w = static_cast<float>(max_width) / static_cast<float>(src_width);
    float const scale_h =
        static_cast<float>(max_height) / static_cast<float>(src_height);
    float scale = (std::min)(scale_w, scale_h);
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    int const tw = std::max(1, static_cast<int>(static_cast<float>(src_width) * scale));
    int const th =
        std::max(1, static_cast<int>(static_cast<float>(src_height) * scale));
    HBITMAP result = nullptr;
    HDC const screen_dc = GetDC(nullptr);
    if (screen_dc != nullptr) {
        HDC const src_dc = CreateCompatibleDC(screen_dc);
        HDC const dst_dc = CreateCompatibleDC(screen_dc);
        result = CreateCompatibleBitmap(screen_dc, tw, th);
        if (src_dc != nullptr && dst_dc != nullptr && result != nullptr) {
            HGDIOBJ const old_src = SelectObject(src_dc, src_bitmap);
            HGDIOBJ const old_dst = SelectObject(dst_dc, result);
            SetStretchBltMode(dst_dc, HALFTONE);
            SetBrushOrgEx(dst_dc, 0, 0, nullptr);
            StretchBlt(dst_dc, 0, 0, tw, th, src_dc, 0, 0, src_width, src_height,
                       SRCCOPY);
            SelectObject(dst_dc, old_dst);
            SelectObject(src_dc, old_src);
        } else if (result != nullptr) {
            DeleteObject(result);
            result = nullptr;
        }
        if (dst_dc != nullptr) {
            DeleteDC(dst_dc);
        }
        if (src_dc != nullptr) {
            DeleteDC(src_dc);
        }
        ReleaseDC(nullptr, screen_dc);
    }
    return result;
}

} // namespace greenflame
