#pragma once

#include "greenflame_core/overlay_controller.h"
#include "greenflame_core/overlay_help_content.h"
#include "greenflame_core/rect_px.h"
#include "greenflame_core/selection_wheel.h"
#include "win/d2d_text_layout_engine.h"
#include "win/overlay_button.h"
#include "win/overlay_help_overlay.h"
#include "win/overlay_warning_dialog.h"

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

  private:
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
    LRESULT Wnd_proc(UINT msg, WPARAM wparam, LPARAM lparam);
    void Apply_action(core::OverlayAction action);
    LRESULT On_key_down(WPARAM wparam, LPARAM lparam);
    LRESULT On_key_up(WPARAM wparam, LPARAM lparam);
    LRESULT On_char(WPARAM wparam);
    LRESULT On_l_button_down();
    LRESULT On_l_button_dbl_clk();
    LRESULT On_mouse_move();
    LRESULT On_mouse_wheel(WPARAM wparam);
    LRESULT On_l_button_up();
    LRESULT On_r_button_down();
    LRESULT On_timer(WPARAM wparam);

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

    LRESULT On_paint();
    LRESULT On_destroy();
    LRESULT On_close();
    LRESULT On_set_cursor(WPARAM wparam, LPARAM lparam);
    void Refresh_cursor();
    bool Refresh_hover_handle();
    [[nodiscard]] bool Handle_tool_size_delta(int32_t delta_steps);
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
    void Handle_device_loss();
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

    bool highlighter_straighten_pending_ = false;
    core::PointPx highlighter_straighten_ref_pos_ = {};

    IOverlayEvents *events_ = nullptr;
    core::AppConfig *config_ = nullptr;
    IWindowQuery *window_query_ = nullptr;
    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    core::OverlayController controller_;
    std::unique_ptr<OverlayResources> resources_;
    std::unique_ptr<ObfuscateSourceProvider> obfuscate_source_provider_;
    std::unique_ptr<D2DOverlayResources> d2d_resources_;
    std::unique_ptr<D2DTextLayoutEngine> text_layout_engine_;
    std::optional<core::SelectionHandle> last_hover_handle_;
    OverlayHelpOverlay hotkey_help_overlay_ = {};
    OverlayWarningDialog obfuscate_warning_dialog_ = {};
    bool testing_toolbar_ = false;
    int mouse_wheel_delta_remainder_ = 0;
    std::wstring transient_center_label_text_ = {};
    bool caret_blink_visible_ = true;
    int32_t selected_annotation_marquee_phase_px_ = 0;
    bool selected_annotation_marquee_timer_running_ = false;
    bool suppress_next_lbutton_up_ = false;
    std::vector<ToolbarButtonEntry> toolbar_buttons_;
    SelectionWheelState selection_wheel_ = {};
};

} // namespace greenflame
