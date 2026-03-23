#pragma once

#include "greenflame_core/annotation_types.h"
#include "greenflame_core/monitor_rules.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/save_image_policy.h"
#include "greenflame_core/window_capture_backend.h"
#include "greenflame_core/window_filter.h"
#include "greenflame_core/window_query.h"

namespace greenflame::core {

enum class CaptureSourceKind : uint8_t {
    ScreenRect = 0,
    Window = 1,
};

struct CaptureSaveRequest final {
    CaptureSourceKind source_kind = CaptureSourceKind::ScreenRect;
    WindowCaptureBackend window_capture_backend = WindowCaptureBackend::Auto;
    RectPx source_rect_screen = {};
    HWND source_window = nullptr;
    InsetsPx padding_px = {};
    COLORREF fill_color = static_cast<COLORREF>(0);
    bool preserve_source_extent = false;
    std::vector<Annotation> annotations = {};

    constexpr bool operator==(const CaptureSaveRequest &) const noexcept = default;
};

enum class CaptureSaveStatus : uint8_t {
    Success = 0,
    BackendFailed = 1,
    SaveFailed = 2,
};

struct CaptureSaveResult final {
    CaptureSaveStatus status = CaptureSaveStatus::SaveFailed;
    std::wstring error_message = {};

    bool operator==(const CaptureSaveResult &) const noexcept = default;
};

enum class AnnotationPreparationStatus : uint8_t {
    Success = 0,
    InputInvalid = 1,
    RenderFailed = 2,
};

struct AnnotationPreparationRequest final {
    std::vector<Annotation> annotations = {};
    std::array<std::wstring, 4> preset_font_families = {};

    bool operator==(const AnnotationPreparationRequest &) const noexcept = default;
};

struct AnnotationPreparationResult final {
    AnnotationPreparationStatus status = AnnotationPreparationStatus::RenderFailed;
    std::wstring error_message = {};
    std::vector<Annotation> annotations = {};

    bool operator==(const AnnotationPreparationResult &) const noexcept = default;
};

enum class InputImageProbeStatus : uint8_t {
    Success = 0,
    SourceReadFailed = 1,
};

struct InputImageProbeResult final {
    InputImageProbeStatus status = InputImageProbeStatus::SourceReadFailed;
    int32_t width = 0;
    int32_t height = 0;
    ImageSaveFormat format = ImageSaveFormat::Png;
    std::wstring error_message = {};

    bool operator==(const InputImageProbeResult &) const noexcept = default;
};

enum class InputImageSaveStatus : uint8_t {
    Success = 0,
    SourceReadFailed = 1,
    SaveFailed = 2,
};

struct InputImageSaveRequest final {
    InsetsPx padding_px = {};
    COLORREF fill_color = static_cast<COLORREF>(0);
    std::vector<Annotation> annotations = {};

    constexpr bool operator==(const InputImageSaveRequest &) const noexcept = default;
};

struct InputImageSaveResult final {
    InputImageSaveStatus status = InputImageSaveStatus::SaveFailed;
    std::wstring error_message = {};

    bool operator==(const InputImageSaveResult &) const noexcept = default;
};

} // namespace greenflame::core

namespace greenflame {

struct WindowMatch final {
    core::WindowCandidateInfo info = {};
    HWND hwnd = nullptr;
};

class IDisplayQueries {
  public:
    virtual ~IDisplayQueries() = default;
    [[nodiscard]] virtual core::PointPx Get_cursor_pos_px() const = 0;
    [[nodiscard]] virtual core::RectPx Get_virtual_desktop_bounds_px() const = 0;
    [[nodiscard]] virtual std::vector<core::MonitorWithBounds>
    Get_monitors_with_bounds() const = 0;
};

class IWindowInspector {
  public:
    virtual ~IWindowInspector() = default;
    [[nodiscard]] virtual std::optional<core::RectPx>
    Get_window_rect(HWND hwnd) const = 0;
    [[nodiscard]] virtual std::optional<core::WindowCandidateInfo>
    Get_window_info(HWND hwnd) const = 0;
    [[nodiscard]] virtual bool Is_window_valid(HWND hwnd) const = 0;
    [[nodiscard]] virtual bool Is_window_minimized(HWND hwnd) const = 0;
    [[nodiscard]] virtual WindowObscuration Get_window_obscuration(HWND hwnd) const = 0;
    [[nodiscard]] virtual std::optional<core::RectPx>
    Get_foreground_window_rect(HWND exclude_hwnd) const = 0;
    [[nodiscard]] virtual std::optional<core::RectPx>
    Get_window_rect_under_cursor(POINT screen_pt, HWND exclude_hwnd) const = 0;
    [[nodiscard]] virtual std::vector<WindowMatch>
    Find_windows_by_title(std::wstring_view needle) const = 0;
    [[nodiscard]] virtual size_t
    Count_minimized_windows_by_title(std::wstring_view needle) const = 0;
};

class ICaptureService {
  public:
    virtual ~ICaptureService() = default;
    [[nodiscard]] virtual bool Copy_rect_to_clipboard(core::RectPx screen_rect) = 0;
    [[nodiscard]] virtual core::CaptureSaveResult
    Save_capture_to_file(core::CaptureSaveRequest const &request,
                         std::wstring_view path, core::ImageSaveFormat format) = 0;
};

class IAnnotationPreparationService {
  public:
    virtual ~IAnnotationPreparationService() = default;
    [[nodiscard]] virtual core::AnnotationPreparationResult
    Prepare_annotations(core::AnnotationPreparationRequest const &request) = 0;
};

class IInputImageService {
  public:
    virtual ~IInputImageService() = default;
    [[nodiscard]] virtual core::InputImageProbeResult
    Probe_input_image(std::wstring_view path) = 0;
    [[nodiscard]] virtual core::InputImageSaveResult Save_input_image_to_file(
        core::InputImageSaveRequest const &request, std::wstring_view input_path,
        std::wstring_view output_path, core::ImageSaveFormat format) = 0;
};

class IFileSystemService {
  public:
    virtual ~IFileSystemService() = default;
    [[nodiscard]] virtual std::vector<std::wstring>
    List_directory_filenames(std::wstring_view dir) const = 0;
    [[nodiscard]] virtual std::wstring
    Reserve_unique_file_path(std::wstring_view desired) const = 0;
    [[nodiscard]] virtual bool
    Try_reserve_exact_file_path(std::wstring_view path, bool &already_exists) const = 0;
    [[nodiscard]] virtual std::wstring
    Resolve_save_directory(std::wstring const &configured_dir) const = 0;
    [[nodiscard]] virtual std::wstring
    Resolve_absolute_path(std::wstring_view path) const = 0;
    [[nodiscard]] virtual bool
    Try_read_text_file_utf8(std::wstring_view path, std::string &utf8_text,
                            std::wstring &error_message) const = 0;
    virtual void Delete_file_if_exists(std::wstring_view path) const = 0;
    [[nodiscard]] virtual core::SaveTimestamp Get_current_timestamp() const = 0;
};

} // namespace greenflame
