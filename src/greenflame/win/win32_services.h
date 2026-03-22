#pragma once

#include "greenflame_core/app_services.h"
#include "win/window_query.h"

namespace greenflame {

class Win32DisplayQueries final : public IDisplayQueries {
  public:
    [[nodiscard]] core::PointPx Get_cursor_pos_px() const override;
    [[nodiscard]] core::RectPx Get_virtual_desktop_bounds_px() const override;
    [[nodiscard]] std::vector<core::MonitorWithBounds>
    Get_monitors_with_bounds() const override;
};

class Win32WindowInspector final : public IWindowInspector {
  public:
    [[nodiscard]] std::optional<core::RectPx> Get_window_rect(HWND hwnd) const override;
    [[nodiscard]] std::optional<core::WindowCandidateInfo>
    Get_window_info(HWND hwnd) const override;
    [[nodiscard]] bool Is_window_valid(HWND hwnd) const override;
    [[nodiscard]] bool Is_window_minimized(HWND hwnd) const override;
    [[nodiscard]] WindowObscuration Get_window_obscuration(HWND hwnd) const override;
    [[nodiscard]] std::optional<core::RectPx>
    Get_foreground_window_rect(HWND exclude_hwnd) const override;
    [[nodiscard]] std::optional<core::RectPx>
    Get_window_rect_under_cursor(POINT screen_pt, HWND exclude_hwnd) const override;
    [[nodiscard]] std::vector<WindowMatch>
    Find_windows_by_title(std::wstring_view needle) const override;
    [[nodiscard]] size_t
    Count_minimized_windows_by_title(std::wstring_view needle) const override;

  private:
    Win32WindowQuery window_query_ = {};
};

class Win32CaptureService final : public ICaptureService {
  public:
    [[nodiscard]] bool Copy_rect_to_clipboard(core::RectPx screen_rect) override;
    [[nodiscard]] core::CaptureSaveResult
    Save_capture_to_file(core::CaptureSaveRequest const &request,
                         std::wstring_view path, core::ImageSaveFormat format) override;
};

class Win32AnnotationPreparationService final : public IAnnotationPreparationService {
  public:
    [[nodiscard]] core::AnnotationPreparationResult
    Prepare_annotations(core::AnnotationPreparationRequest const &request) override;
};

class Win32FileSystemService final : public IFileSystemService {
  public:
    [[nodiscard]] std::vector<std::wstring>
    List_directory_filenames(std::wstring_view dir) const override;
    [[nodiscard]] std::wstring
    Reserve_unique_file_path(std::wstring_view desired) const override;
    [[nodiscard]] bool Try_reserve_exact_file_path(std::wstring_view path,
                                                   bool &already_exists) const override;
    [[nodiscard]] std::wstring
    Resolve_save_directory(std::wstring const &configured_dir) const override;
    [[nodiscard]] std::wstring
    Resolve_absolute_path(std::wstring_view path) const override;
    [[nodiscard]] bool
    Try_read_text_file_utf8(std::wstring_view path, std::string &utf8_text,
                            std::wstring &error_message) const override;
    void Delete_file_if_exists(std::wstring_view path) const override;
    [[nodiscard]] core::SaveTimestamp Get_current_timestamp() const override;
};

} // namespace greenflame
