#pragma once

#include "app_config.h"
#include "greenflame_core/rect_px.h"
#include "win/overlay_window.h"
#include "win/tray_window.h"

namespace greenflame {

class GreenflameApp final : public ITrayEvents, public IOverlayEvents {
  public:
    explicit GreenflameApp(HINSTANCE hinstance);

    [[nodiscard]] int Run();

  private:
    void On_start_capture_requested() override;
    void On_copy_window_to_clipboard_requested(HWND target_window) override;
    void On_copy_monitor_to_clipboard_requested() override;
    void On_copy_desktop_to_clipboard_requested() override;
    void On_copy_last_region_to_clipboard_requested() override;
    void On_copy_last_window_to_clipboard_requested() override;
    void On_exit_requested() override;
    void On_overlay_closed() override;
    void On_selection_copied_to_clipboard(core::RectPx screen_rect,
                                          std::optional<HWND> window) override;
    void On_selection_saved_to_file(core::RectPx screen_rect,
                                    std::optional<HWND> window,
                                    HBITMAP thumbnail) override;

    void Store_last_capture(core::RectPx screen_rect, std::optional<HWND> window);

    HINSTANCE hinstance_ = nullptr;
    AppConfig config_ = {};
    TrayWindow tray_window_;
    OverlayWindow overlay_window_;
    std::optional<core::RectPx> last_capture_screen_rect_;
    std::optional<HWND> last_capture_window_;
};

} // namespace greenflame
