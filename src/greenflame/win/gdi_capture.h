#pragma once

// Phase 3.1: GDI full-screen capture of the virtual desktop.
// Capture sequence: GetDC(GetDesktopWindow), CreateCompatibleDC,
// CreateDIBSection, BitBlt(SRCCOPY | CAPTUREBLT). Caller must call
// Result::Free() when done (or DeleteObject(result.bitmap)).

namespace greenflame {

struct GdiCaptureResult {
    HBITMAP bitmap = nullptr;
    int width = 0;
    int height = 0;

    void Free() noexcept;
    bool Is_valid() const noexcept {
        return bitmap != nullptr && width > 0 && height > 0;
    }
};

// Captures the virtual desktop into a 32bpp top-down DIB.
// Returns true and fills out on success; false on failure (out is cleared).
bool Capture_virtual_desktop(GdiCaptureResult &out);

// Writes the capture to a BMP file (for Phase 3.1 validation).
// Returns true on success. Path is UTF-16 (wchar_t).
bool Save_capture_to_bmp(GdiCaptureResult const &capture, wchar_t const *path);

// Crops source to the given rect (in source coords). For Phase 3.5 commit.
// Caller must call out.Free() when done. Returns false if rect is empty or out of
// bounds.
bool Crop_capture(GdiCaptureResult const &source, int left, int top, int width,
                  int height, GdiCaptureResult &out);

// Copies a 32bpp capture to clipboard using CF_DIB. Returns true on success.
// If owner_window is null, the current task owns the clipboard while open.
bool Copy_capture_to_clipboard(GdiCaptureResult const &capture, HWND owner_window);

// --- Helpers for 32bpp top-down DIB (used by capture and bitmap interop) ---
void Fill_bmi32_top_down(BITMAPINFOHEADER &bmi, int width, int height);
int Row_bytes32(int width);

// Scales src_bitmap to fit within max_width x max_height (preserving aspect
// ratio). Returns an HBITMAP the caller must DeleteObject, or nullptr on failure.
[[nodiscard]] HBITMAP Scale_bitmap_to_thumbnail(HBITMAP src_bitmap, int src_width,
                                                int src_height, int max_width,
                                                int max_height);

} // namespace greenflame
