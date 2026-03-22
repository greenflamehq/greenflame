#pragma once

#include "greenflame_core/app_services.h"
#include "greenflame_core/cli_options.h"
#include "greenflame_core/overlay_help_content.h"
#include "greenflame_core/process_exit_code.h"

namespace greenflame {

namespace core {
struct AppConfig;
}

struct SelectionSavedResult final {
    std::wstring balloon_message = {};
    std::wstring file_path = {};
};

struct ClipboardCopyResult final {
    std::wstring balloon_message = {};
    bool success = false;
};

struct CliResult final {
    std::wstring stdout_message = {};
    std::wstring stderr_message = {};
    ProcessExitCode exit_code = ProcessExitCode::Success;
};

class AppController final {
  public:
    AppController(core::AppConfig &config, IDisplayQueries &display_queries,
                  IWindowInspector &window_inspector, ICaptureService &capture_service,
                  IAnnotationPreparationService &annotation_preparation_service,
                  IFileSystemService &file_system_service);
    AppController(AppController const &) = delete;
    AppController &operator=(AppController const &) = delete;
    AppController(AppController &&) = delete;
    AppController &operator=(AppController &&) = delete;
    ~AppController() = default;

    [[nodiscard]] ClipboardCopyResult
    On_copy_window_to_clipboard_requested(HWND target_window);
    [[nodiscard]] ClipboardCopyResult On_copy_monitor_to_clipboard_requested();
    [[nodiscard]] ClipboardCopyResult On_copy_desktop_to_clipboard_requested();
    [[nodiscard]] ClipboardCopyResult On_copy_last_region_to_clipboard_requested();
    [[nodiscard]] ClipboardCopyResult On_copy_last_window_to_clipboard_requested();
    [[nodiscard]] ClipboardCopyResult
    On_selection_copied_to_clipboard(core::RectPx screen_rect,
                                     std::optional<HWND> window);
    [[nodiscard]] SelectionSavedResult
    On_selection_saved_to_file(core::RectPx screen_rect, std::optional<HWND> window,
                               std::wstring_view saved_path, bool file_copied);
    [[nodiscard]] core::OverlayHelpContent Build_overlay_help_content() const;
    [[nodiscard]] CliResult Run_cli_capture_mode(core::CliOptions const &cli_options);

  private:
    [[nodiscard]] std::wstring
    Build_default_output_path(core::SaveSelectionSource source,
                              std::optional<size_t> monitor_index_zero_based,
                              std::wstring_view window_title,
                              core::ImageSaveFormat format) const;
    void Store_last_capture(core::RectPx screen_rect, std::optional<HWND> window);

    core::AppConfig &config_;
    IDisplayQueries &display_queries_;
    IWindowInspector &window_inspector_;
    ICaptureService &capture_service_;
    IAnnotationPreparationService &annotation_preparation_service_;
    IFileSystemService &file_system_service_;

    std::optional<core::RectPx> last_capture_screen_rect_ = std::nullopt;
    std::optional<HWND> last_capture_window_ = std::nullopt;
};

} // namespace greenflame
