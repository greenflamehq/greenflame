#pragma once

#include "greenflame_core/app_config.h"
#include "greenflame_core/app_controller.h"
#include "greenflame_core/cli_options.h"
#include "greenflame_core/process_exit_code.h"
#include "win/overlay_window.h"
#include "win/tray_window.h"
#include "win/win32_services.h"
#include "win/window_query.h"

namespace greenflame {

class GreenflameApp final : public ITrayEvents, public IOverlayEvents {
  public:
    explicit GreenflameApp(HINSTANCE hinstance,
                           core::CliOptions const &cli_options = {});
    GreenflameApp(GreenflameApp const &) = delete;
    GreenflameApp &operator=(GreenflameApp const &) = delete;
    GreenflameApp(GreenflameApp &&) = delete;
    GreenflameApp &operator=(GreenflameApp &&) = delete;
    ~GreenflameApp() override = default;

    [[nodiscard]] uint8_t Run();

  private:
    [[nodiscard]] ProcessExitCode Run_cli_capture_mode();
    void On_start_capture_requested() override;
    void On_copy_window_to_clipboard_requested(HWND target_window) override;
    void On_copy_monitor_to_clipboard_requested() override;
    void On_copy_desktop_to_clipboard_requested() override;
    void On_copy_last_region_to_clipboard_requested() override;
    void On_copy_last_window_to_clipboard_requested() override;
    [[nodiscard]] bool On_set_start_with_windows_enabled(bool enabled) override;
    void On_exit_requested() override;
    void On_overlay_closed() override;
    void On_selection_copied_to_clipboard(core::RectPx screen_rect,
                                          std::optional<HWND> window) override;
    void On_selection_saved_to_file(core::RectPx screen_rect,
                                    std::optional<HWND> window, HBITMAP thumbnail,
                                    std::wstring_view saved_path,
                                    bool file_copied_to_clipboard) override;

    HINSTANCE hinstance_ = nullptr;
    TrayWindow tray_window_;
    Win32WindowQuery window_query_;
    core::OverlayHelpContent overlay_help_content_ = {};
    OverlayWindow overlay_window_;
    Win32DisplayQueries display_queries_;
    Win32WindowInspector window_inspector_;
    Win32CaptureService capture_service_;
    Win32AnnotationPreparationService annotation_preparation_service_;
    Win32FileSystemService file_system_service_;
    AppController app_controller_;
    core::CliOptions cli_options_ = {};
    core::AppConfig config_ = {};
};

} // namespace greenflame
