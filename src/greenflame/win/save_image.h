#pragma once

// Save GdiCaptureResult to PNG or JPEG via Windows Imaging Component (WIC).

#include "win/gdi_capture.h"

namespace greenflame::core {
enum class ImageSaveFormat : uint8_t;
} // namespace greenflame::core

namespace greenflame {

// Writes the capture to PNG or JPEG. Path is UTF-16 (wchar_t).
bool Save_capture_to_png(GdiCaptureResult const &capture, wchar_t const *path);
bool Save_capture_to_jpeg(GdiCaptureResult const &capture, wchar_t const *path);

// Dispatches to the appropriate encoder based on format.
bool Save_capture_to_file(GdiCaptureResult const &capture, wchar_t const *path,
                          core::ImageSaveFormat format);

// Atomically reserves a writable file path. The returned file path exists
// (created as an empty placeholder) and is unique at reservation time.
// If the requested path is already taken, a numeric suffix is appended.
// Returns empty string on failure.
[[nodiscard]] std::wstring
Reserve_unique_file_path(std::wstring_view desired_path) noexcept;

// Returns the filenames (not full paths) of all non-directory entries in dir.
[[nodiscard]] std::vector<std::wstring> List_directory_filenames(std::wstring_view dir);

} // namespace greenflame
