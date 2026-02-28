#pragma once

#include "greenflame_core/overlay_controller.h"
#include "greenflame_core/rect_px.h"

namespace greenflame {

namespace core {
struct AppConfig;
}
class IWindowQuery;

class IOverlayEvents {
  public:
    virtual ~IOverlayEvents() = default;
    virtual void On_overlay_closed() = 0;
    virtual void On_selection_copied_to_clipboard(core::RectPx screen_rect,
                                                  std::optional<HWND> window) = 0;
    virtual void On_selection_saved_to_file(core::RectPx screen_rect,
                                            std::optional<HWND> window,
                                            HBITMAP thumbnail,
                                            std::wstring_view saved_path,
                                            bool file_copied_to_clipboard) = 0;
};

class OverlayWindow final {
  public:
    OverlayWindow(IOverlayEvents *events, core::AppConfig *config,
                  IWindowQuery *window_query);
    ~OverlayWindow();

    OverlayWindow(OverlayWindow const &) = delete;
    OverlayWindow &operator=(OverlayWindow const &) = delete;

    [[nodiscard]] static bool Register_window_class(HINSTANCE hinstance);
    [[nodiscard]] bool Create_and_show(HINSTANCE hinstance);
    void Destroy();

    [[nodiscard]] bool Is_open() const;

  private:
    struct OverlayResources;

    static LRESULT CALLBACK Static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam);
    LRESULT Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam);
    void Apply_action(core::OverlayAction action);
    LRESULT On_key_down(WPARAM wparam, LPARAM lparam);
    LRESULT On_key_up(WPARAM wparam, LPARAM lparam);
    LRESULT On_l_button_down();
    LRESULT On_mouse_move();
    LRESULT On_l_button_up();

    void Build_default_save_name(std::wstring_view save_dir_for_num_scan,
                                 std::span<wchar_t> out) const;
    [[nodiscard]] std::wstring Resolve_default_save_directory() const;
    [[nodiscard]] std::wstring Resolve_save_as_initial_directory() const;
    [[nodiscard]] core::RectPx Selection_screen_rect() const;
    void Save_directly_and_close(bool copy_saved_file_to_clipboard);
    void Save_as_and_close(bool copy_saved_file_to_clipboard);
    void Copy_to_clipboard_and_close();

    LRESULT On_paint();
    LRESULT On_destroy();
    LRESULT On_close();
    LRESULT On_set_cursor(WPARAM wparam, LPARAM lparam);

    IOverlayEvents *events_ = nullptr;
    core::AppConfig *config_ = nullptr;
    IWindowQuery *window_query_ = nullptr;
    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    core::OverlayController controller_;
    std::unique_ptr<OverlayResources> resources_;
    std::optional<core::SelectionHandle> last_hover_handle_;
};

} // namespace greenflame
