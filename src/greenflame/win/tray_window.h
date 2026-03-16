#pragma once

namespace greenflame {

enum class TrayBalloonIcon {
    Info,
    Warning,
    Error,
};

class ITrayEvents {
  public:
    virtual ~ITrayEvents() = default;
    virtual void On_start_capture_requested() = 0;
    virtual void On_copy_window_to_clipboard_requested(HWND target_window) = 0;
    virtual void On_copy_monitor_to_clipboard_requested() = 0;
    virtual void On_copy_desktop_to_clipboard_requested() = 0;
    virtual void On_copy_last_region_to_clipboard_requested() = 0;
    virtual void On_copy_last_window_to_clipboard_requested() = 0;
    [[nodiscard]] virtual bool On_set_start_with_windows_enabled(bool enabled) = 0;
    virtual void On_exit_requested() = 0;
};

class TrayWindow final {
  public:
    explicit TrayWindow(ITrayEvents *events);
    ~TrayWindow();

    TrayWindow(TrayWindow const &) = delete;
    TrayWindow &operator=(TrayWindow const &) = delete;

    [[nodiscard]] static bool Register_window_class(HINSTANCE hinstance);
    [[nodiscard]] bool Create(HINSTANCE hinstance, bool enable_testing_hotkeys = false,
                              bool start_with_windows_enabled = false);
    void Destroy();
    void Show_balloon(TrayBalloonIcon icon, wchar_t const *message,
                      HBITMAP thumbnail = nullptr, std::wstring_view file_path = {});

    [[nodiscard]] bool Is_open() const;

  private:
    class ToastPopup;

    static LRESULT CALLBACK Static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam);
    LRESULT Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam);
    void Show_context_menu();
    void Show_about_dialog();
    void Open_config_file();
    void Notify_start_capture();
    void Notify_copy_window_to_clipboard();
    void Notify_copy_monitor_to_clipboard();
    void Notify_copy_desktop_to_clipboard();
    void Notify_copy_last_region_to_clipboard();
    void Notify_copy_last_window_to_clipboard();
    void Notify_toggle_start_with_windows();

    ITrayEvents *events_ = nullptr;
    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    bool testing_hotkeys_enabled_ = false;
    bool start_with_windows_enabled_ = false;
    std::unique_ptr<ToastPopup> toast_popup_;
};

} // namespace greenflame
