#pragma once

#include "greenflame_core/mouse_move_history.h"
#include "greenflame_core/overlay_controller.h"
#include "greenflame_core/overlay_help_content.h"
#include "greenflame_core/overlay_surface_layout.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_wheel.h"
#include "win/d2d_text_layout_engine.h"
#include "win/overlay_button.h"
#include "win/overlay_help_overlay.h"
#include "win/overlay_warning_dialog.h"
#include "win/win32_services.h"

namespace greenflame {

namespace core {
struct AppConfig;
}
class IWindowQuery;
struct GdiCaptureResult;
struct D2DOverlayResources;

class IOverlayEvents {
  public:
    virtual ~IOverlayEvents() = default;
    virtual void On_overlay_closed() = 0;
    virtual void On_selection_copied_to_clipboard(core::RectPx screen_rect,
                                                  std::optional<HWND> window) = 0;
    virtual bool On_selection_pinned_to_desktop(core::RectPx screen_rect,
                                                GdiCaptureResult &capture) = 0;
    virtual void On_selection_saved_to_file(core::RectPx screen_rect,
                                            std::optional<HWND> window,
                                            HBITMAP thumbnail,
                                            std::wstring_view saved_path,
                                            bool file_copied_to_clipboard) = 0;
    // Called when one or more language tags in spell_check_languages are not
    // supported by the Windows spell checking API on this machine.
    virtual void On_spell_check_languages_unsupported(std::wstring_view warning) = 0;
    // The overlay could not open, or closed itself and cancelled the capture because
    // something went wrong (display change, lost device, lost mouse capture). reason
    // is one short sentence for the user.
    virtual void On_overlay_failed(std::wstring_view reason) = 0;
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
    void Set_hotkey_help_content(core::OverlayHelpContent const *content) noexcept;
    void Set_testing_toolbar(bool enable) noexcept;

    [[nodiscard]] bool Is_open() const;

    // Call after the app config has been reloaded from disk. If the overlay is
    // open, the spell check service is rebuilt immediately; otherwise it will
    // be rebuilt on the next Open(). Fires On_spell_check_languages_unsupported
    // if any requested language is unavailable on this machine.
    void On_config_updated();

  private:
    void Rebuild_spell_check_service();
    enum class ToolbarButtonAction : uint8_t {
        SelectAnnotationTool,
        ToggleCapturedCursor,
        PinToDesktop,
        ShowHelp,
    };

    enum class ToolbarLayoutItemKind : uint8_t {
        Button,
        Spacer,
    };

    struct OverlayResources;
    struct ObfuscateSourceProvider;
    class ModalScope;

    struct ToolbarButtonModel final {
        ToolbarButtonAction action = ToolbarButtonAction::SelectAnnotationTool;
        std::optional<core::AnnotationToolId> tool_id = std::nullopt;
        OverlayToolbarGlyphId glyph = OverlayToolbarGlyphId::None;
        std::wstring tooltip = {};
        std::wstring label = {};
        bool active = false;
    };

    struct ToolbarLayoutItem final {
        ToolbarLayoutItemKind kind = ToolbarLayoutItemKind::Spacer;
        std::optional<ToolbarButtonModel> button = std::nullopt;
    };

    static LRESULT CALLBACK Static_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam);
    // hwnd is the window the message is for.
    LRESULT Wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void Apply_action(core::OverlayAction action);
    LRESULT On_key_down(HWND hwnd, WPARAM wparam, LPARAM lparam);
    LRESULT On_key_up(HWND hwnd, WPARAM wparam, LPARAM lparam);
    LRESULT On_char(WPARAM wparam);
    LRESULT On_l_button_down();
    LRESULT On_l_button_dbl_clk();
    LRESULT On_mouse_move();
    LRESULT On_mouse_wheel(WPARAM wparam);
    LRESULT On_l_button_up();
    LRESULT On_r_button_down();
    LRESULT On_timer(HWND hwnd, WPARAM wparam);

    // The overlay's coordinates are virtual-desktop pixels: client pixel (0, 0) is the
    // virtual desktop's top-left, wherever the overlay windows sit.
    [[nodiscard]] core::PointPx Desktop_origin_px() const noexcept;
    // The virtual desktop in screen pixels, as captured at open.
    [[nodiscard]] core::RectPx Desktop_rect_screen() const noexcept;
    [[nodiscard]] core::PointPx Cursor_client_px() const;
    // Asks for a repaint of the overlay; Repaint_now paints before returning.
    void Request_repaint() const;
    void Repaint_now() const;

    void Build_default_save_name(std::wstring_view save_dir_for_num_scan,
                                 std::span<wchar_t> out) const;
    [[nodiscard]] std::wstring Resolve_default_save_directory() const;
    [[nodiscard]] core::RectPx Selection_screen_rect() const;
    [[nodiscard]] core::RectPx Selection_visible_screen_rect() const;
    void Save_directly_and_close(bool copy_saved_file_to_clipboard);
    void Save_as_and_close(bool copy_saved_file_to_clipboard);
    void Copy_to_clipboard_and_close();
    void Pin_to_desktop_and_close();
    [[nodiscard]] bool Build_selection_capture(GdiCaptureResult &out) const;
    [[nodiscard]] bool Build_rendered_selection_capture(GdiCaptureResult &out) const;
    void Notify_save_and_close(GdiCaptureResult &cropped, std::wstring_view saved_path,
                               bool file_copied_to_clipboard);

    LRESULT On_paint(HWND hwnd);
    LRESULT On_destroy();
    LRESULT On_close();

    // One overlay window. Each draws its part of the overlay into its own target of
    // a D2DOverlayResources (one device, so resources_index is 0 for now).
    struct OverlaySurface final {
        HWND hwnd = nullptr;
        core::RectPx rect_screen = {};
        core::RectPx rect_client = {};
        size_t resources_index = 0;
        size_t target_index = 0;
    };
    // Creates the windows and render targets for layout. On failure, everything it
    // made is gone again and failure says why.
    [[nodiscard]] bool Create_surfaces(core::OverlayLayout const &layout,
                                       std::wstring &failure);
    // Undoes a failed Create_surfaces without any session teardown.
    void Destroy_surfaces_quietly();
    [[nodiscard]] bool Is_surface(HWND hwnd) const noexcept;
    void Reassert_topmost() const;
    // Closes the overlay and cancels the capture, with a balloon saying reason. Runs
    // from a posted message, never inside painting or a capture change.
    void Post_fail_close(std::wstring_view reason);
    void On_fail_close_message();
    // Cancels the Save As dialog or message box that is up, so its caller returns.
    void End_modal_dialog() const;
    // True once a close is waiting: the overlay must not save or show anything more.
    [[nodiscard]] bool Close_requested() const noexcept;
    // While the session is locked or the display is off, DWM takes no frames: paints
    // are skipped instead of timing out, and one repaint follows the return.
    [[nodiscard]] bool Presentation_paused() const noexcept;
    void Set_presentation_paused(bool session_locked, bool display_off);
    // Closes the overlay when the monitors (if recheck_monitors) or a window moved away
    // from the layout.
    void Check_layout_still_matches(bool recheck_monitors);
    void Take_mouse_capture(HWND hwnd);
    void Release_mouse_capture();
    void On_capture_changed(HWND new_owner);
    LRESULT On_mouse_activate(HWND hwnd, WPARAM wparam, LPARAM lparam);
    void Log_gesture_frames();
    void Start_gesture_frames();
    void Note_gesture_input();
    LRESULT On_set_cursor(HWND hwnd, WPARAM wparam, LPARAM lparam);
    void Refresh_cursor();
    bool Refresh_hover_handle();
    [[nodiscard]] bool Handle_tool_size_delta(int32_t delta_steps);
    // True when the brush or the highlighter is the armed tool.
    [[nodiscard]] bool Is_freehand_tool_armed() const;
    // Brush and highlighter strokes take every mouse position Windows recorded, not one
    // per handled WM_MOUSEMOVE (GetMouseMovePointsEx). True while one is drawing.
    [[nodiscard]] bool Is_freehand_stroke_active() const;
    // Pointer refresh for a Shift, Ctrl or Alt change. During a freehand stroke it adds
    // no stroke point. Returns the cursor in client pixels.
    core::PointPx Refresh_pointer_for_modifier_keys(core::OverlayModifierState mods);
    // Feeds the positions recorded since the last one consumed, up to the current
    // message's, to the controller. Returns the newest point in client pixels.
    core::PointPx Feed_stroke_history(core::OverlayModifierState mods);
    void Log_stroke_input() const;
    void Show_tool_size_overlay(int32_t step);
    void Clear_transient_center_label(bool repaint);
    [[nodiscard]] bool Read_clipboard_text(std::wstring &out) const;
    void Write_clipboard_text(std::wstring_view text) const;
    // Writes CF_UNICODETEXT, CF_RTF, and "HTML Format" in a single clipboard session.
    void Write_clipboard_rich_text(std::wstring_view plain_text, std::string_view rtf,
                                   std::string_view html) const;
    [[nodiscard]] bool Read_clipboard_rtf(std::string &out) const;
    [[nodiscard]] bool Read_clipboard_html(std::string &out) const;
    // Core reader: opens clipboard, copies format data to out, closes clipboard.
    [[nodiscard]] bool Read_clipboard_bytes(UINT fmt, std::string &out) const;
    void Reset_caret_blink();
    void Update_selected_annotation_marquee_timer() noexcept;
    void Cancel_highlighter_straighten_pending() noexcept;
    [[nodiscard]] bool Can_show_selection_wheel() const noexcept;
    [[nodiscard]] std::span<const COLORREF> Current_tool_color_palette() const noexcept;
    [[nodiscard]] size_t Current_annotation_color_index() const noexcept;
    [[nodiscard]] size_t Current_selection_wheel_segment_count() const noexcept;
    // Interactive segment count: layout count minus the phantom slot (if any).
    [[nodiscard]] size_t Nav_segment_count() const noexcept;
    // Ring angle offset for the current wheel; non-zero only for clamped-nav wheels.
    [[nodiscard]] float Effective_ring_angle_offset() const noexcept;
    // Set highlighter_mode and keep clamp_nav in sync.
    void Set_highlighter_mode(core::HighlighterWheelMode mode) noexcept;
    [[nodiscard]] std::optional<size_t>
    Current_selection_wheel_selected_segment() const noexcept;
    void Show_selection_wheel(core::PointPx center);
    void Dismiss_selection_wheel(bool repaint);
    [[nodiscard]] bool Update_selection_wheel_hover(core::PointPx cursor);
    void Select_wheel_segment(size_t index);
    [[nodiscard]] bool Selection_wheel_has_multiple_views() const noexcept;
    [[nodiscard]] std::optional<size_t> Explicit_wheel_segment_hover() const noexcept;
    [[nodiscard]] std::optional<size_t> Effective_wheel_segment_hover() const noexcept;
    void Navigate_wheel(int steps);
    [[nodiscard]] bool Clear_toolbar_hover_states();
    [[nodiscard]] bool Update_toolbar_hover_states(core::PointPx cursor);
    [[nodiscard]] bool Should_show_brush_cursor_preview() const;
    [[nodiscard]] bool Should_show_square_cursor_preview() const;
    [[nodiscard]] bool Should_force_obfuscate_repaint() const;
    [[nodiscard]] bool Is_selection_stable_for_help() const;
    void Show_help_overlay_at_cursor();
    void Hide_help_overlay(bool suppress_next_lbutton_up);
    void Show_obfuscate_warning_at_cursor();
    void Hide_obfuscate_warning();
    void Accept_obfuscate_warning();
    void Reject_obfuscate_warning();
    void Toggle_captured_cursor_visibility();
    [[nodiscard]] bool Is_captured_cursor_visible() const noexcept;
    [[nodiscard]] bool Current_capture_has_captured_cursor() const noexcept;
    [[nodiscard]] std::wstring Build_captured_cursor_tooltip() const;
    [[nodiscard]] std::wstring Build_pin_tooltip() const;
    [[nodiscard]] bool Rebuild_display_capture();
    [[nodiscard]] core::RectPx
    Visible_window_rect_screen(core::RectPx window_rect_screen) const noexcept;
    [[nodiscard]] std::optional<HWND>
    Resolve_ctrl_preview_window(core::PointPx cursor_screen,
                                std::optional<core::RectPx> &window_rect_screen) const;
    [[nodiscard]] bool Update_ctrl_window_preview(HWND window,
                                                  core::RectPx capture_rect_screen);
    [[nodiscard]] core::PointPx
    Update_pointer_state_from_current_input(core::OverlayModifierState mods);
    void Refresh_pointer_visual_overlays(core::PointPx cursor_client);
    void Restore_selection_state(core::OverlaySelectionState const &state);
    [[nodiscard]] bool Maybe_show_obfuscate_warning();
    [[nodiscard]] IOverlayTopLayer *Active_top_layer() noexcept;
    [[nodiscard]] std::wstring_view Hovered_toolbar_tooltip_text() const noexcept;
    [[nodiscard]] std::optional<core::RectPx> Hovered_toolbar_button_bounds() const;
    [[nodiscard]] OverlayButtonGlyph const *
    Resolve_toolbar_button_glyph(OverlayToolbarGlyphId glyph) const noexcept;
    [[nodiscard]] core::SnapEdges Collect_visible_snap_edges() const;

    void Rebuild_toolbar_buttons();
    [[nodiscard]] std::vector<core::PointPx>
    Compute_toolbar_positions(int button_count) const;

    struct ToolbarButtonEntry final {
        ToolbarButtonAction action = ToolbarButtonAction::SelectAnnotationTool;
        std::optional<core::AnnotationToolId> tool_id = std::nullopt;
        OverlayToolbarGlyphId glyph = OverlayToolbarGlyphId::None;
        std::wstring tooltip = {};
        std::unique_ptr<IOverlayButton> button = {};

        ToolbarButtonEntry() = default;
        ToolbarButtonEntry(ToolbarButtonEntry const &) = delete;
        ToolbarButtonEntry &operator=(ToolbarButtonEntry const &) = delete;
        ToolbarButtonEntry(ToolbarButtonEntry &&) noexcept = default;
        ToolbarButtonEntry &operator=(ToolbarButtonEntry &&) noexcept = default;
    };

    struct SelectionWheelState final {
        bool visible = false;
        core::PointPx center = {};
        // Mouse hover: set by Update_selection_wheel_hover on WM_MOUSEMOVE.
        std::optional<size_t> mouse_hovered_segment = std::nullopt;
        // Nav hover: set by keyboard/scroll-wheel navigation; cleared when
        // the cursor enters a ring segment.
        std::optional<size_t> nav_hovered_segment = std::nullopt;
        core::TextWheelMode text_mode = core::TextWheelMode::Color;
        std::optional<core::TextWheelHubSide> hovered_hub = std::nullopt;
        core::HighlighterWheelMode highlighter_mode = core::HighlighterWheelMode::Color;
        std::optional<core::HighlighterWheelHubSide> highlighter_hovered_hub =
            std::nullopt;
        // When true: last slot is a phantom (not drawn/interactive), and keyboard/
        // scroll-wheel navigation clamps at the first/last real segment.
        bool clamp_nav = false;
        // Accumulator for fractional scroll-wheel ticks; reset on show/dismiss.
        int scroll_delta_remainder = 0;
    };

    struct StrokeInput final {
        core::MouseMoveSample last_consumed = {};
        size_t history_points = 0; // points taken from the mouse history
        size_t gaps = 0;           // history overflowed: movement was lost
        size_t fallbacks = 0;      // history unavailable: message point only
    };
    // Member order keeps clang-analyzer-optin.performance.Padding quiet: 8-byte-aligned
    // members first, then 4-byte-aligned, then 1-byte-aligned. The unique_ptr owners
    // keep their relative order (it is their destruction order). The GREENFLAME_LOG
    // block stays last.
    IOverlayEvents *events_ = nullptr;
    core::AppConfig *config_ = nullptr;
    IWindowQuery *window_query_ = nullptr;
    // The window on the cursor's monitor when the overlay opened: it has keyboard
    // focus, owns the other windows, the timers, the clipboard and the dialogs.
    HWND primary_hwnd_ = nullptr;
    HPOWERNOTIFY display_state_notify_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    std::unique_ptr<OverlayResources> resources_;
    std::unique_ptr<ObfuscateSourceProvider> obfuscate_source_provider_;
    std::unique_ptr<D2DOverlayResources> d2d_resources_;
    std::unique_ptr<D2DTextLayoutEngine> text_layout_engine_;
    std::unique_ptr<Win32SpellCheckService> spell_check_service_;
    std::vector<OverlaySurface> surfaces_ = {};
    std::vector<ToolbarButtonEntry> toolbar_buttons_;
    StrokeInput stroke_input_ = {};
    std::wstring fail_close_reason_ = {};
    std::wstring transient_center_label_text_ = {};
    core::OverlayLayout layout_ = {};
    SelectionWheelState selection_wheel_ = {};
    OverlayHelpOverlay hotkey_help_overlay_ = {};
    OverlayWarningDialog obfuscate_warning_dialog_ = {};
    core::OverlayController controller_;
    int modal_depth_ = 0; // a Save As dialog or message box is up
    int wait_timeout_streak_ = 0;
#if defined(DEBUG) || defined(GREENFLAME_LOG)
    // Debug hooks (Ctrl+Shift+Alt+F11 / F12): the next paints report a lost device or
    // frame-wait timeouts, to exercise the close paths.
    int debug_force_wait_timeouts_ = 0;
#endif
    int mouse_wheel_delta_remainder_ = 0;
    int32_t selected_annotation_marquee_phase_px_ = 0;
    core::PointPx highlighter_straighten_ref_pos_ = {};
    bool highlighter_straighten_pending_ = false;
    bool creating_ = false;     // Create_surfaces is running
    bool closing_ = false;      // Destroy is running
    bool capture_held_ = false; // SetCapture since the last press
    bool releasing_capture_ = false;
    bool fail_close_posted_ = false;
    bool fail_close_pending_ = false; // waits for the modal dialog to end
    bool close_after_modal_ = false;  // Destroy asked for during a modal dialog
    bool session_locked_ = false;
    bool display_off_ = false;
#if defined(DEBUG) || defined(GREENFLAME_LOG)
    bool debug_force_device_lost_ = false;
#endif
    bool testing_toolbar_ = false;
    bool caret_blink_visible_ = true;
    bool selected_annotation_marquee_timer_running_ = false;
    bool suppress_next_lbutton_up_ = false;
    std::optional<core::SelectionHandle> last_hover_handle_;
#if defined(GREENFLAME_LOG)
    // Frames of the current press-to-release gesture, for the debug log. QPC ticks.
    struct GestureFrame final {
        int64_t end_qpc = 0;         // the frame's paint ended
        int64_t input_qpc = 0;       // first input since the previous frame
        int64_t paint_start_qpc = 0; // WM_PAINT began
        int64_t wait_qpc = 0;        // ticks in the frame-latency wait
    };
    static constexpr size_t kGestureFrameCapacity = 4096;
    std::vector<GestureFrame> gesture_frames_ = {};
    bool gesture_frames_active_ = false;
    int64_t gesture_pending_input_qpc_ = 0; // 0: no input since the last frame
#endif
};

} // namespace greenflame
