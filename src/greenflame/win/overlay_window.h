#pragma once

#include "greenflame_core/color_wheel.h"
#include "greenflame_core/overlay_controller.h"
#include "greenflame_core/overlay_help_content.h"
#include "greenflame_core/rect_px.h"
#include "win/d2d_text_layout_engine.h"
#include "win/overlay_button.h"
#include "win/overlay_help_overlay.h"

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
        ShowHelp,
    };

    enum class ToolbarLayoutItemKind : uint8_t {
        Button,
        Spacer,
    };

    struct OverlayResources;

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
    LRESULT On_mouse_move();
    LRESULT On_mouse_wheel(WPARAM wparam);
    LRESULT On_l_button_up();
    LRESULT On_r_button_down();
    LRESULT On_timer(WPARAM wparam);

    void Build_default_save_name(std::wstring_view save_dir_for_num_scan,
                                 std::span<wchar_t> out) const;
    [[nodiscard]] std::wstring Resolve_default_save_directory() const;
    [[nodiscard]] std::wstring Resolve_save_as_initial_directory() const;
    [[nodiscard]] core::RectPx Selection_screen_rect() const;
    void Save_directly_and_close(bool copy_saved_file_to_clipboard);
    void Save_as_and_close(bool copy_saved_file_to_clipboard);
    void Copy_to_clipboard_and_close();
    void Notify_save_and_close(GdiCaptureResult &cropped, std::wstring_view saved_path,
                               bool file_copied_to_clipboard);
    [[nodiscard]] bool
    Composite_annotations_into_capture(GdiCaptureResult &capture,
                                       core::RectPx target_bounds) const;

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
    void Reset_caret_blink();
    void Cancel_highlighter_straighten_pending() noexcept;
    void Handle_device_loss();
    [[nodiscard]] bool Can_show_color_wheel() const noexcept;
    [[nodiscard]] std::span<const COLORREF> Current_tool_color_palette() const noexcept;
    [[nodiscard]] size_t Current_annotation_color_index() const noexcept;
    [[nodiscard]] size_t Current_color_wheel_segment_count() const noexcept;
    void Show_color_wheel(core::PointPx center);
    void Dismiss_color_wheel(bool repaint);
    [[nodiscard]] bool Update_color_wheel_hover(core::PointPx cursor);
    void Select_color_wheel_segment(size_t index);
    [[nodiscard]] bool Clear_toolbar_hover_states();
    [[nodiscard]] bool Update_toolbar_hover_states(core::PointPx cursor);
    [[nodiscard]] bool Should_show_brush_cursor_preview() const;
    [[nodiscard]] bool Should_show_square_cursor_preview() const;
    [[nodiscard]] bool Is_selection_stable_for_help() const;
    void Show_help_overlay_at_cursor();
    void Hide_help_overlay(bool suppress_next_lbutton_up);
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

    struct ColorWheelState final {
        bool visible = false;
        core::PointPx center = {};
        std::optional<size_t> hovered_segment = std::nullopt;
        core::TextWheelMode text_mode = core::TextWheelMode::Color;
        std::optional<core::TextWheelHubSide> hovered_hub = std::nullopt;
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
    std::unique_ptr<D2DOverlayResources> d2d_resources_;
    std::unique_ptr<D2DTextLayoutEngine> text_layout_engine_;
    std::optional<core::SelectionHandle> last_hover_handle_;
    OverlayHelpOverlay hotkey_help_overlay_ = {};
    bool testing_toolbar_ = false;
    int mouse_wheel_delta_remainder_ = 0;
    std::wstring transient_center_label_text_ = {};
    bool caret_blink_visible_ = true;
    bool suppress_next_lbutton_up_ = false;
    std::vector<ToolbarButtonEntry> toolbar_buttons_;
    ColorWheelState color_wheel_ = {};
};

} // namespace greenflame
