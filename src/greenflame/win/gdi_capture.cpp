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

class ScopedIconInfo final {
  public:
    ScopedIconInfo() = default;
    ScopedIconInfo(ScopedIconInfo const &) = delete;
    ScopedIconInfo &operator=(ScopedIconInfo const &) = delete;
    ~ScopedIconInfo() {
        if (info_.hbmColor != nullptr) {
            DeleteObject(info_.hbmColor);
        }
        if (info_.hbmMask != nullptr) {
            DeleteObject(info_.hbmMask);
        }
    }

    [[nodiscard]] ICONINFO *Get() noexcept { return &info_; }
    [[nodiscard]] ICONINFO const &Value() const noexcept { return info_; }

  private:
    ICONINFO info_ = {};
};

[[nodiscard]] bool Try_get_bitmap_dimensions(HBITMAP bitmap, int &width,
                                             int &height) noexcept {
    width = 0;
    height = 0;
    if (bitmap == nullptr) {
        return false;
    }

    BITMAP info = {};
    if (GetObjectW(bitmap, sizeof(info), &info) != sizeof(info)) {
        return false;
    }
    if (info.bmWidth <= 0 || info.bmHeight <= 0) {
        return false;
    }

    width = info.bmWidth;
    height = info.bmHeight;
    return true;
}

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

CapturedCursorSnapshot::~CapturedCursorSnapshot() { Free(); }

CapturedCursorSnapshot::CapturedCursorSnapshot(CapturedCursorSnapshot &&other) noexcept {
    *this = std::move(other);
}

CapturedCursorSnapshot &
CapturedCursorSnapshot::operator=(CapturedCursorSnapshot &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    Free();
    cursor = other.cursor;
    other.cursor = nullptr;
    image_width = other.image_width;
    image_height = other.image_height;
    other.image_width = 0;
    other.image_height = 0;
    hotspot_screen_px = other.hotspot_screen_px;
    hotspot_offset_px = other.hotspot_offset_px;
    other.hotspot_screen_px = {};
    other.hotspot_offset_px = {};
    return *this;
}

void CapturedCursorSnapshot::Free() noexcept {
    if (cursor != nullptr) {
        DestroyIcon(cursor);
        cursor = nullptr;
    }
    image_width = 0;
    image_height = 0;
    hotspot_screen_px = {};
    hotspot_offset_px = {};
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

bool Capture_cursor_snapshot(CapturedCursorSnapshot &out) {
    out.Free();

    CURSORINFO cursor_info = {};
    cursor_info.cbSize = sizeof(cursor_info);
    if (GetCursorInfo(&cursor_info) == 0 ||
        (cursor_info.flags & CURSOR_SHOWING) == 0 || cursor_info.hCursor == nullptr) {
        return false;
    }

    HCURSOR const copied_cursor = CopyIcon(cursor_info.hCursor);
    if (copied_cursor == nullptr) {
        return false;
    }

    ScopedIconInfo icon_info = {};
    if (GetIconInfo(copied_cursor, icon_info.Get()) == 0) {
        DestroyIcon(copied_cursor);
        return false;
    }

    int width = 0;
    int height = 0;
    bool have_size = false;
    if (icon_info.Value().hbmColor != nullptr) {
        have_size =
            Try_get_bitmap_dimensions(icon_info.Value().hbmColor, width, height);
    } else if (icon_info.Value().hbmMask != nullptr) {
        int mask_width = 0;
        int mask_height = 0;
        have_size =
            Try_get_bitmap_dimensions(icon_info.Value().hbmMask, mask_width, mask_height) &&
            mask_height >= 2 && (mask_height % 2) == 0;
        if (have_size) {
            width = mask_width;
            height = mask_height / 2;
        }
    }
    if (!have_size || width <= 0 || height <= 0) {
        DestroyIcon(copied_cursor);
        return false;
    }

    out.cursor = copied_cursor;
    out.image_width = width;
    out.image_height = height;
    out.hotspot_screen_px = {cursor_info.ptScreenPos.x, cursor_info.ptScreenPos.y};
    out.hotspot_offset_px = {static_cast<int32_t>(icon_info.Value().xHotspot),
                             static_cast<int32_t>(icon_info.Value().yHotspot)};
    return true;
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

bool Create_solid_capture(int width, int height, COLORREF fill_color,
                          GdiCaptureResult &out) {
    out.Free();
    if (width <= 0 || height <= 0) {
        return false;
    }

    HDC const dc = GetDC(nullptr);
    if (!dc) {
        return false;
    }

    bool ok = false;
    HBITMAP dib = nullptr;
    HDC const mem_dc = CreateCompatibleDC(dc);
    if (mem_dc) {
        BITMAPINFOHEADER bmi;
        Fill_bmi32_top_down(bmi, width, height);
        void *bits = nullptr;
        dib = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&bmi), DIB_RGB_COLORS,
                               &bits, nullptr, 0);
        if (dib && bits) {
            HGDIOBJ const old = SelectObject(mem_dc, dib);
            if (old && old != HGDI_ERROR) {
                HBRUSH const brush = CreateSolidBrush(fill_color);
                if (brush != nullptr) {
                    RECT const rect = {0, 0, width, height};
                    ok = FillRect(mem_dc, &rect, brush) != 0;
                    DeleteObject(brush);
                }
                SelectObject(mem_dc, old);
            }
        }
        DeleteDC(mem_dc);
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

bool Blit_capture(GdiCaptureResult const &source, int src_left, int src_top, int width,
                  int height, GdiCaptureResult &dest, int dst_left, int dst_top) {
    if (!source.Is_valid() || !dest.Is_valid() || width <= 0 || height <= 0 ||
        src_left < 0 || src_top < 0 || dst_left < 0 || dst_top < 0 ||
        src_left + width > source.width || src_top + height > source.height ||
        dst_left + width > dest.width || dst_top + height > dest.height) {
        return false;
    }

    HDC const dc = GetDC(nullptr);
    if (!dc) {
        return false;
    }

    bool ok = false;
    HDC const src_dc = CreateCompatibleDC(dc);
    HDC const dst_dc = CreateCompatibleDC(dc);
    if (src_dc && dst_dc) {
        HGDIOBJ const old_src = SelectObject(src_dc, source.bitmap);
        HGDIOBJ const old_dst = SelectObject(dst_dc, dest.bitmap);
        if (old_src && old_src != HGDI_ERROR && old_dst && old_dst != HGDI_ERROR) {
            ok = BitBlt(dst_dc, dst_left, dst_top, width, height, src_dc, src_left,
                        src_top, SRCCOPY) != 0;
        }
        if (old_dst && old_dst != HGDI_ERROR) {
            SelectObject(dst_dc, old_dst);
        }
        if (old_src && old_src != HGDI_ERROR) {
            SelectObject(src_dc, old_src);
        }
    }
    if (dst_dc) {
        DeleteDC(dst_dc);
    }
    if (src_dc) {
        DeleteDC(src_dc);
    }
    ReleaseDC(nullptr, dc);
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

bool Composite_cursor_snapshot(CapturedCursorSnapshot const &cursor_snapshot,
                               core::PointPx target_origin_px,
                               GdiCaptureResult &target) {
    if (!cursor_snapshot.Is_valid() || !target.Is_valid()) {
        return true;
    }

    int64_t const draw_left64 = static_cast<int64_t>(cursor_snapshot.hotspot_screen_px.x) -
                                cursor_snapshot.hotspot_offset_px.x -
                                target_origin_px.x;
    int64_t const draw_top64 = static_cast<int64_t>(cursor_snapshot.hotspot_screen_px.y) -
                               cursor_snapshot.hotspot_offset_px.y -
                               target_origin_px.y;
    if (draw_left64 < static_cast<int64_t>(INT32_MIN) ||
        draw_left64 > static_cast<int64_t>(INT32_MAX) ||
        draw_top64 < static_cast<int64_t>(INT32_MIN) ||
        draw_top64 > static_cast<int64_t>(INT32_MAX)) {
        return true;
    }
    int64_t const draw_right64 =
        draw_left64 + static_cast<int64_t>(cursor_snapshot.image_width);
    int64_t const draw_bottom64 =
        draw_top64 + static_cast<int64_t>(cursor_snapshot.image_height);

    int const draw_left = static_cast<int>(draw_left64);
    int const draw_top = static_cast<int>(draw_top64);
    if (draw_left64 >= target.width || draw_top64 >= target.height || draw_right64 <= 0 ||
        draw_bottom64 <= 0) {
        return true;
    }

    HDC const screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return false;
    }

    bool ok = false;
    HDC const target_dc = CreateCompatibleDC(screen_dc);
    if (target_dc != nullptr) {
        HGDIOBJ const old = SelectObject(target_dc, target.bitmap);
        if (old != nullptr && old != HGDI_ERROR) {
            ok = DrawIconEx(target_dc, draw_left, draw_top, cursor_snapshot.cursor,
                            cursor_snapshot.image_width, cursor_snapshot.image_height, 0,
                            nullptr, DI_NORMAL) != 0;
            SelectObject(target_dc, old);
        }
        DeleteDC(target_dc);
    }
    ReleaseDC(nullptr, screen_dc);
    return ok;
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
