#include "greenflame_core/app_config.h"
#include "greenflame_core/app_controller.h"
#include "greenflame_core/output_path.h"

using namespace greenflame;
using namespace greenflame::core;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

namespace {

class MockDisplayQueries : public IDisplayQueries {
  public:
    MockDisplayQueries() = default;
    MockDisplayQueries(MockDisplayQueries const &) = delete;
    MockDisplayQueries &operator=(MockDisplayQueries const &) = delete;
    MockDisplayQueries(MockDisplayQueries &&) = delete;
    MockDisplayQueries &operator=(MockDisplayQueries &&) = delete;
    ~MockDisplayQueries() override = default;

    MOCK_METHOD(core::PointPx, Get_cursor_pos_px, (), (const, override));
    MOCK_METHOD(core::RectPx, Get_virtual_desktop_bounds_px, (), (const, override));
    MOCK_METHOD(std::vector<core::MonitorWithBounds>, Get_monitors_with_bounds, (),
                (const, override));
};

class MockWindowInspector : public IWindowInspector {
  public:
    MockWindowInspector() = default;
    MockWindowInspector(MockWindowInspector const &) = delete;
    MockWindowInspector &operator=(MockWindowInspector const &) = delete;
    MockWindowInspector(MockWindowInspector &&) = delete;
    MockWindowInspector &operator=(MockWindowInspector &&) = delete;
    ~MockWindowInspector() override = default;

    MOCK_METHOD(std::optional<core::RectPx>, Get_window_rect, (HWND),
                (const, override));
    MOCK_METHOD(std::optional<core::WindowCandidateInfo>, Get_window_info, (HWND),
                (const, override));
    MOCK_METHOD(bool, Is_window_valid, (HWND), (const, override));
    MOCK_METHOD(bool, Is_window_minimized, (HWND), (const, override));
    MOCK_METHOD(WindowObscuration, Get_window_obscuration, (HWND), (const, override));
    MOCK_METHOD(std::optional<core::RectPx>, Get_foreground_window_rect, (HWND),
                (const, override));
    MOCK_METHOD(std::optional<core::RectPx>, Get_window_rect_under_cursor,
                (POINT, HWND), (const, override));
    MOCK_METHOD(std::vector<WindowMatch>, Find_windows_by_title, (std::wstring_view),
                (const, override));
};

class MockCaptureService : public ICaptureService {
  public:
    MockCaptureService() = default;
    MockCaptureService(MockCaptureService const &) = delete;
    MockCaptureService &operator=(MockCaptureService const &) = delete;
    MockCaptureService(MockCaptureService &&) = delete;
    MockCaptureService &operator=(MockCaptureService &&) = delete;
    ~MockCaptureService() override = default;

    MOCK_METHOD(bool, Copy_rect_to_clipboard, (core::RectPx), (override));
    MOCK_METHOD(bool, Save_rect_to_file,
                (core::CaptureSaveRequest const &, std::wstring_view,
                 core::ImageSaveFormat),
                (override));
};

class MockAnnotationPreparationService : public IAnnotationPreparationService {
  public:
    MockAnnotationPreparationService() = default;
    MockAnnotationPreparationService(MockAnnotationPreparationService const &) = delete;
    MockAnnotationPreparationService &
    operator=(MockAnnotationPreparationService const &) = delete;
    MockAnnotationPreparationService(MockAnnotationPreparationService &&) = delete;
    MockAnnotationPreparationService &
    operator=(MockAnnotationPreparationService &&) = delete;
    ~MockAnnotationPreparationService() override = default;

    MOCK_METHOD(core::AnnotationPreparationResult, Prepare_annotations,
                (core::AnnotationPreparationRequest const &), (override));
};

class MockFileSystemService : public IFileSystemService {
  public:
    MockFileSystemService() = default;
    MockFileSystemService(MockFileSystemService const &) = delete;
    MockFileSystemService &operator=(MockFileSystemService const &) = delete;
    MockFileSystemService(MockFileSystemService &&) = delete;
    MockFileSystemService &operator=(MockFileSystemService &&) = delete;
    ~MockFileSystemService() override = default;

    MOCK_METHOD(std::vector<std::wstring>, List_directory_filenames,
                (std::wstring_view), (const, override));
    MOCK_METHOD(std::wstring, Reserve_unique_file_path, (std::wstring_view),
                (const, override));
    MOCK_METHOD(bool, Try_reserve_exact_file_path, (std::wstring_view, bool &),
                (const, override));
    MOCK_METHOD(std::wstring, Resolve_save_directory, (std::wstring const &),
                (const, override));
    MOCK_METHOD(std::wstring, Resolve_absolute_path, (std::wstring_view),
                (const, override));
    MOCK_METHOD(bool, Try_read_text_file_utf8,
                (std::wstring_view, std::string &, std::wstring &), (const, override));
    MOCK_METHOD(void, Delete_file_if_exists, (std::wstring_view), (const, override));
    MOCK_METHOD(core::SaveTimestamp, Get_current_timestamp, (), (const, override));
};

struct ControllerFixture {
    AppConfig config = {};
    StrictMock<MockDisplayQueries> display = {};
    StrictMock<MockWindowInspector> windows = {};
    StrictMock<MockCaptureService> capture = {};
    StrictMock<MockAnnotationPreparationService> annotation_preparation = {};
    StrictMock<MockFileSystemService> file_system = {};
    AppController controller;

    ControllerFixture()
        : controller(config, display, windows, capture, annotation_preparation,
                     file_system) {}
    ControllerFixture(ControllerFixture const &) = delete;
    ControllerFixture &operator=(ControllerFixture const &) = delete;
    ControllerFixture(ControllerFixture &&) = delete;
    ControllerFixture &operator=(ControllerFixture &&) = delete;
    ~ControllerFixture() = default;
};

[[nodiscard]] core::CaptureSaveRequest
Make_save_request(core::RectPx source_rect, core::InsetsPx padding = {},
                  COLORREF fill_color = static_cast<COLORREF>(0),
                  bool preserve_source_extent = false) {
    core::CaptureSaveRequest request{};
    request.source_rect_screen = source_rect;
    request.padding_px = padding;
    request.fill_color = fill_color;
    request.preserve_source_extent = preserve_source_extent;
    return request;
}

[[nodiscard]] WindowMatch Make_window_match(HWND hwnd, std::wstring_view title,
                                            std::wstring_view class_name,
                                            core::RectPx rect) {
    WindowMatch match{};
    match.hwnd = hwnd;
    match.info.title = std::wstring(title);
    match.info.class_name = std::wstring(class_name);
    match.info.rect = rect;
    match.info.hwnd_value = reinterpret_cast<std::uintptr_t>(hwnd);
    return match;
}

} // namespace

TEST(app_controller, copy_window_uses_target_window_rect_and_copies) {
    ControllerFixture fixture;
    HWND const hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1111));
    RectPx const rect = RectPx::From_ltrb(10, 20, 310, 220);

    EXPECT_CALL(fixture.windows, Get_window_rect(hwnd)).WillOnce(Return(rect));
    EXPECT_CALL(fixture.capture, Copy_rect_to_clipboard(rect)).WillOnce(Return(true));

    ClipboardCopyResult const result =
        fixture.controller.On_copy_window_to_clipboard_requested(hwnd);
    EXPECT_TRUE(result.success);
    EXPECT_THAT(result.balloon_message, HasSubstr(L"copied"));
}

TEST(app_controller, copy_last_region_without_state_returns_warning) {
    ControllerFixture fixture;

    ClipboardCopyResult const result =
        fixture.controller.On_copy_last_region_to_clipboard_requested();
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.balloon_message, HasSubstr(L"No previously captured region"));
}

TEST(app_controller, copy_last_window_reports_minimized) {
    ControllerFixture fixture;
    HWND const hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x2222));
    RectPx const saved = RectPx::From_ltrb(5, 6, 105, 106);
    std::ignore = fixture.controller.On_selection_copied_to_clipboard(saved, hwnd);

    EXPECT_CALL(fixture.windows, Is_window_valid(hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(hwnd)).WillOnce(Return(true));

    ClipboardCopyResult const result =
        fixture.controller.On_copy_last_window_to_clipboard_requested();
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.balloon_message, HasSubstr(L"minimized"));
}

TEST(app_controller, copy_last_window_requeries_and_copies) {
    ControllerFixture fixture;
    HWND const hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x3333));
    RectPx const saved = RectPx::From_ltrb(1, 2, 51, 52);
    RectPx const current = RectPx::From_ltrb(10, 20, 210, 220);
    std::ignore = fixture.controller.On_selection_copied_to_clipboard(saved, hwnd);

    EXPECT_CALL(fixture.windows, Is_window_valid(hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(hwnd)).WillOnce(Return(false));
    EXPECT_CALL(fixture.windows, Get_window_rect(hwnd)).WillOnce(Return(current));
    EXPECT_CALL(fixture.capture, Copy_rect_to_clipboard(current))
        .WillOnce(Return(true));

    ClipboardCopyResult const result =
        fixture.controller.On_copy_last_window_to_clipboard_requested();
    EXPECT_TRUE(result.success);
}

TEST(app_controller, copy_monitor_uses_cursor_monitor_bounds) {
    ControllerFixture fixture;
    MonitorWithBounds monitor0{};
    monitor0.bounds = RectPx::From_ltrb(0, 0, 100, 100);
    MonitorWithBounds monitor1{};
    monitor1.bounds = RectPx::From_ltrb(100, 0, 300, 200);
    std::vector<MonitorWithBounds> const monitors = {monitor0, monitor1};

    EXPECT_CALL(fixture.display, Get_monitors_with_bounds()).WillOnce(Return(monitors));
    EXPECT_CALL(fixture.display, Get_cursor_pos_px())
        .WillOnce(Return(PointPx{150, 50}));
    EXPECT_CALL(fixture.capture, Copy_rect_to_clipboard(monitor1.bounds))
        .WillOnce(Return(true));

    ClipboardCopyResult const result =
        fixture.controller.On_copy_monitor_to_clipboard_requested();
    EXPECT_TRUE(result.success);
}

TEST(app_controller, copy_desktop_uses_virtual_desktop_bounds) {
    ControllerFixture fixture;
    RectPx const desktop = RectPx::From_ltrb(-100, 0, 900, 700);

    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .WillOnce(Return(desktop));
    EXPECT_CALL(fixture.capture, Copy_rect_to_clipboard(desktop))
        .WillOnce(Return(true));

    ClipboardCopyResult const result =
        fixture.controller.On_copy_desktop_to_clipboard_requested();
    EXPECT_TRUE(result.success);
}

TEST(app_controller, on_selection_saved_builds_balloon_message_and_stores_state) {
    ControllerFixture fixture;
    HWND const hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x4444));
    RectPx const rect = RectPx::From_ltrb(0, 0, 100, 100);

    SelectionSavedResult const saved = fixture.controller.On_selection_saved_to_file(
        rect, hwnd, L"C:\\shots\\saved.png", true);
    EXPECT_THAT(saved.balloon_message, HasSubstr(L"saved.png"));

    EXPECT_CALL(fixture.windows, Is_window_valid(hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(hwnd)).WillOnce(Return(false));
    EXPECT_CALL(fixture.windows, Get_window_rect(hwnd)).WillOnce(Return(rect));
    EXPECT_CALL(fixture.capture, Copy_rect_to_clipboard(rect)).WillOnce(Return(true));

    ClipboardCopyResult const copied =
        fixture.controller.On_copy_last_window_to_clipboard_requested();
    EXPECT_TRUE(copied.success);
}

TEST(app_controller, cli_window_mode_filters_invocation_window_and_saves) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Window;
    options.window_name = L"Notepad";
    options.output_path = L"C:\\shots\\note.png";
    options.overwrite_output = true;

    HWND const target_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x5151));
    RectPx const target_rect = RectPx::From_ltrb(10, 20, 210, 220);

    WindowMatch invocation{};
    invocation.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x6161));
    invocation.info.title = L"greenflame.exe --window Notepad";
    invocation.info.class_name = L"ConsoleWindowClass";
    invocation.info.rect = RectPx::From_ltrb(0, 0, 10, 10);

    WindowMatch target{};
    target.hwnd = target_hwnd;
    target.info.title = L"Notepad";
    target.info.class_name = L"Notepad";
    target.info.rect = target_rect;

    RectPx const virtual_bounds = RectPx::From_ltrb(0, 0, 1920, 1080);
    EXPECT_CALL(fixture.windows,
                Find_windows_by_title(Eq(std::wstring_view{L"Notepad"})))
        .WillOnce(Return(std::vector<WindowMatch>{invocation, target}));
    EXPECT_CALL(fixture.windows, Is_window_valid(target_hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(target_hwnd))
        .WillOnce(Return(false));
    EXPECT_CALL(fixture.windows, Get_window_rect(target_hwnd))
        .WillOnce(Return(target_rect));
    EXPECT_CALL(fixture.windows, Get_window_obscuration(target_hwnd))
        .WillOnce(Return(WindowObscuration::None));
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(1)
        .WillRepeatedly(Return(virtual_bounds));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\note.png"})))
        .WillOnce(Return(L"C:\\shots\\note.png"));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(Make_save_request(target_rect),
                                  Eq(std::wstring_view{L"C:\\shots\\note.png"}),
                                  ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
    EXPECT_THAT(result.stdout_message, HasSubstr(L"Saved: C:\\shots\\note.png"));
    EXPECT_TRUE(result.stderr_message.empty());
}

TEST(app_controller, cli_window_mode_prefers_unique_exact_title_match) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Window;
    options.window_name = L"Codex";
    options.output_path = L"C:\\shots\\codex.png";
    options.overwrite_output = true;

    HWND const exact_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x5151));
    HWND const broader_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x6262));
    RectPx const exact_rect = RectPx::From_ltrb(10, 20, 210, 220);
    RectPx const broader_rect = RectPx::From_ltrb(40, 50, 340, 260);
    WindowMatch const broader = Make_window_match(
        broader_hwnd, L"*screenshot-2026-03-20_141317-Codex.png - Paint.NET 5.1.12",
        L"PaintDotNet", broader_rect);
    WindowMatch const exact =
        Make_window_match(exact_hwnd, L"Codex", L"Chrome_WidgetWin_1", exact_rect);

    EXPECT_CALL(fixture.windows, Find_windows_by_title(Eq(std::wstring_view{L"Codex"})))
        .WillOnce(Return(std::vector<WindowMatch>{broader, exact}));
    EXPECT_CALL(fixture.windows, Is_window_valid(exact_hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(exact_hwnd))
        .WillOnce(Return(false));
    EXPECT_CALL(fixture.windows, Get_window_rect(exact_hwnd))
        .WillOnce(Return(exact_rect));
    EXPECT_CALL(fixture.windows, Get_window_obscuration(exact_hwnd))
        .WillOnce(Return(WindowObscuration::None));
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .WillOnce(Return(RectPx::From_ltrb(0, 0, 1920, 1080)));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\codex.png"})))
        .WillOnce(Return(L"C:\\shots\\codex.png"));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(Make_save_request(exact_rect),
                                  Eq(std::wstring_view{L"C:\\shots\\codex.png"}),
                                  ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
    EXPECT_TRUE(result.stderr_message.empty());
}

TEST(app_controller, cli_window_mode_ambiguous_output_includes_hwnd_class_and_rect) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Window;
    options.window_name = L"Codex";

    WindowMatch const match_a = Make_window_match(
        reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1111)), L"Codex",
        L"Chrome_WidgetWin_1", RectPx::From_ltrb(10, 20, 210, 220));
    WindowMatch const match_b =
        Make_window_match(reinterpret_cast<HWND>(static_cast<uintptr_t>(0x2222)),
                          L"Codex", L"Notepad", RectPx::From_ltrb(30, 40, 330, 240));

    EXPECT_CALL(fixture.windows, Find_windows_by_title(Eq(std::wstring_view{L"Codex"})))
        .WillOnce(Return(std::vector<WindowMatch>{match_a, match_b}));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliWindowAmbiguous);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"hwnd=0x1111"));
    EXPECT_THAT(result.stderr_message, HasSubstr(L"class=\"Chrome_WidgetWin_1\""));
    EXPECT_THAT(result.stderr_message, HasSubstr(L"(x=10, y=20, w=200, h=200)"));
}

TEST(app_controller, cli_window_hwnd_mode_selects_exact_window) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Window;
    options.window_hwnd = static_cast<std::uintptr_t>(0x5151u);
    options.output_path = L"C:\\shots\\hwnd.png";
    options.overwrite_output = true;

    HWND const hwnd = reinterpret_cast<HWND>(*options.window_hwnd);
    RectPx const rect = RectPx::From_ltrb(10, 20, 210, 220);
    core::WindowCandidateInfo info{};
    info.title = L"Codex";
    info.class_name = L"Chrome_WidgetWin_1";
    info.rect = rect;
    info.hwnd_value = *options.window_hwnd;

    EXPECT_CALL(fixture.windows, Is_window_valid(hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(hwnd)).WillOnce(Return(false));
    EXPECT_CALL(fixture.windows, Get_window_info(hwnd)).WillOnce(Return(info));
    EXPECT_CALL(fixture.windows, Get_window_obscuration(hwnd))
        .WillOnce(Return(WindowObscuration::None));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\hwnd.png"})))
        .WillOnce(Return(L"C:\\shots\\hwnd.png"));
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .WillOnce(Return(RectPx::From_ltrb(0, 0, 1920, 1080)));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(Make_save_request(rect),
                                  Eq(std::wstring_view{L"C:\\shots\\hwnd.png"}),
                                  ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
    EXPECT_TRUE(result.stderr_message.empty());
}

TEST(app_controller, cli_window_hwnd_mode_fails_when_handle_invalid) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Window;
    options.window_hwnd = static_cast<std::uintptr_t>(0xDEADu);

    HWND const hwnd = reinterpret_cast<HWND>(*options.window_hwnd);
    EXPECT_CALL(fixture.windows, Is_window_valid(hwnd)).WillOnce(Return(false));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliWindowUnavailable);
    EXPECT_FALSE(result.stderr_message.empty());
}

TEST(app_controller, cli_window_hwnd_mode_fails_when_not_visible_toplevel) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Window;
    options.window_hwnd = static_cast<std::uintptr_t>(0xDEADu);

    HWND const hwnd = reinterpret_cast<HWND>(*options.window_hwnd);
    EXPECT_CALL(fixture.windows, Is_window_valid(hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(hwnd)).WillOnce(Return(false));
    EXPECT_CALL(fixture.windows, Get_window_info(hwnd)).WillOnce(Return(std::nullopt));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliWindowUnavailable);
    EXPECT_FALSE(result.stderr_message.empty());
}

TEST(app_controller,
     cli_padded_window_mode_uses_config_color_and_preserves_source_extent) {
    ControllerFixture fixture;
    fixture.config.padding_color = Make_colorref(0x11, 0x22, 0x33);

    CliOptions options{};
    options.capture_mode = CliCaptureMode::Window;
    options.window_name = L"Notepad";
    options.output_path = L"C:\\shots\\note-pad.png";
    options.overwrite_output = true;
    options.padding_px = InsetsPx{4, 8, 12, 16};

    HWND const target_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x7171));
    RectPx const target_rect = RectPx::From_ltrb(10, 20, 210, 220);
    WindowMatch target{};
    target.hwnd = target_hwnd;
    target.info.title = L"Notepad";
    target.info.class_name = L"Notepad";
    target.info.rect = target_rect;

    RectPx const virtual_bounds = RectPx::From_ltrb(0, 0, 1920, 1080);
    EXPECT_CALL(fixture.windows,
                Find_windows_by_title(Eq(std::wstring_view{L"Notepad"})))
        .WillOnce(Return(std::vector<WindowMatch>{target}));
    EXPECT_CALL(fixture.windows, Is_window_valid(target_hwnd)).WillOnce(Return(true));
    EXPECT_CALL(fixture.windows, Is_window_minimized(target_hwnd))
        .WillOnce(Return(false));
    EXPECT_CALL(fixture.windows, Get_window_rect(target_hwnd))
        .WillOnce(Return(target_rect));
    EXPECT_CALL(fixture.windows, Get_window_obscuration(target_hwnd))
        .WillOnce(Return(WindowObscuration::Partial));
    EXPECT_CALL(
        fixture.file_system,
        Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\note-pad.png"})))
        .WillOnce(Return(L"C:\\shots\\note-pad.png"));
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .WillOnce(Return(virtual_bounds));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(Make_save_request(target_rect, InsetsPx{4, 8, 12, 16},
                                                    fixture.config.padding_color, true),
                                  Eq(std::wstring_view{L"C:\\shots\\note-pad.png"}),
                                  ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
    EXPECT_THAT(result.stderr_message,
                HasSubstr(L"partially obscured by other windows"));
}

TEST(app_controller, cli_padded_region_mode_emits_fill_warning) {
    ControllerFixture fixture;

    CliOptions options{};
    options.capture_mode = CliCaptureMode::Region;
    options.region_px = RectPx::From_ltrb(80, 80, 130, 130);
    options.output_path = L"C:\\shots\\region-pad.png";
    options.overwrite_output = true;
    options.padding_px = InsetsPx{6, 6, 6, 6};

    RectPx const virtual_bounds = RectPx::From_ltrb(0, 0, 100, 100);
    EXPECT_CALL(
        fixture.file_system,
        Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\region-pad.png"})))
        .WillOnce(Return(L"C:\\shots\\region-pad.png"));
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .WillOnce(Return(virtual_bounds));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(
                    Make_save_request(options.region_px.value(), InsetsPx{6, 6, 6, 6},
                                      Make_colorref(0x00, 0x00, 0x00), true),
                    Eq(std::wstring_view{L"C:\\shots\\region-pad.png"}),
                    ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"filled with the padding color"));
}

TEST(app_controller, cli_padded_monitor_mode_builds_save_request) {
    ControllerFixture fixture;

    CliOptions options{};
    options.capture_mode = CliCaptureMode::Monitor;
    options.monitor_id = 1;
    options.output_path = L"C:\\shots\\monitor-pad.png";
    options.overwrite_output = true;
    options.padding_px = InsetsPx{2, 4, 6, 8};

    MonitorWithBounds monitor{};
    monitor.bounds = RectPx::From_ltrb(10, 20, 210, 220);
    EXPECT_CALL(fixture.display, Get_monitors_with_bounds())
        .WillOnce(Return(std::vector<MonitorWithBounds>{monitor}));
    EXPECT_CALL(
        fixture.file_system,
        Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\monitor-pad.png"})))
        .WillOnce(Return(L"C:\\shots\\monitor-pad.png"));
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .WillOnce(Return(RectPx::From_ltrb(0, 0, 400, 300)));
    EXPECT_CALL(
        fixture.capture,
        Save_rect_to_file(Make_save_request(monitor.bounds, InsetsPx{2, 4, 6, 8},
                                            Make_colorref(0x00, 0x00, 0x00), true),
                          Eq(std::wstring_view{L"C:\\shots\\monitor-pad.png"}),
                          ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
}

TEST(app_controller, cli_padded_desktop_mode_prefers_cli_color_override) {
    ControllerFixture fixture;
    fixture.config.padding_color = Make_colorref(0x22, 0x33, 0x44);

    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\desktop-pad.png";
    options.overwrite_output = true;
    options.padding_px = InsetsPx{1, 2, 3, 4};
    options.padding_color_override = Make_colorref(0xAA, 0xBB, 0xCC);

    RectPx const desktop = RectPx::From_ltrb(-100, 0, 1820, 1080);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(
        fixture.file_system,
        Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\desktop-pad.png"})))
        .WillOnce(Return(L"C:\\shots\\desktop-pad.png"));
    EXPECT_CALL(
        fixture.capture,
        Save_rect_to_file(Make_save_request(desktop, InsetsPx{1, 2, 3, 4},
                                            Make_colorref(0xAA, 0xBB, 0xCC), true),
                          Eq(std::wstring_view{L"C:\\shots\\desktop-pad.png"}),
                          ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
}

TEST(app_controller, cli_padded_capture_fails_when_source_is_outside_virtual_desktop) {
    ControllerFixture fixture;

    CliOptions options{};
    options.capture_mode = CliCaptureMode::Region;
    options.region_px = RectPx::From_ltrb(200, 200, 250, 250);
    options.output_path = L"C:\\shots\\missing.png";
    options.overwrite_output = true;
    options.padding_px = InsetsPx{5, 5, 5, 5};

    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .WillOnce(Return(RectPx::From_ltrb(0, 0, 100, 100)));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliCaptureSaveFailed);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"outside the virtual desktop"));
}

TEST(app_controller, cli_padded_capture_rejects_output_size_overflow) {
    ControllerFixture fixture;

    CliOptions options{};
    options.capture_mode = CliCaptureMode::Region;
    options.region_px = RectPx::From_ltrb(0, 0, 0x7fffffff, 1);
    options.padding_px = InsetsPx{1, 0, 0, 0};

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliCaptureSaveFailed);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"invalid or too large"));
}

TEST(app_controller, cli_output_path_detects_format_conflict) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\capture.png";
    options.output_format = CliOutputFormat::Jpeg;
    options.overwrite_output = true;

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliOutputPathFailure);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"conflicts with --format"));
}

TEST(app_controller, cli_default_output_path_uses_timestamp_and_num_scan) {
    ControllerFixture fixture;
    fixture.config.default_save_dir = L"C:\\shots";
    fixture.config.filename_pattern_desktop = L"shot_${num}";

    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    SaveTimestamp const timestamp{27, 2, 2026, 10, 11, 12};
    std::wstring const expected_unreserved = L"C:\\shots\\shot_000002.png";

    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(fixture.file_system,
                Resolve_save_directory(Eq(fixture.config.default_save_dir)))
        .WillOnce(Return(L"C:\\shots"));
    EXPECT_CALL(fixture.file_system, Get_current_timestamp())
        .WillOnce(Return(timestamp));
    EXPECT_CALL(fixture.file_system,
                List_directory_filenames(Eq(std::wstring_view{L"C:\\shots"})))
        .WillOnce(Return(std::vector<std::wstring>{L"shot_000001.png"}));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{expected_unreserved})))
        .WillOnce(Return(expected_unreserved));
    EXPECT_CALL(fixture.file_system,
                Reserve_unique_file_path(Eq(std::wstring_view{expected_unreserved})))
        .WillOnce(Return(expected_unreserved));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(Make_save_request(desktop),
                                  Eq(std::wstring_view{expected_unreserved}),
                                  ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
    EXPECT_THAT(result.stdout_message, HasSubstr(expected_unreserved));
}

TEST(app_controller, cli_monitor_mode_reports_out_of_range) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Monitor;
    options.monitor_id = 2;

    MonitorWithBounds monitor{};
    monitor.bounds = RectPx::From_ltrb(0, 0, 100, 100);
    EXPECT_CALL(fixture.display, Get_monitors_with_bounds())
        .WillOnce(Return(std::vector<MonitorWithBounds>{monitor}));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliMonitorOutOfRange);
}

TEST(app_controller, cli_desktop_mode_saves_virtual_bounds) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\desktop.png";
    options.overwrite_output = true;

    RectPx const desktop = RectPx::From_ltrb(-100, 0, 1820, 1080);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\desktop.png"})))
        .WillOnce(Return(L"C:\\shots\\desktop.png"));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(Make_save_request(desktop),
                                  Eq(std::wstring_view{L"C:\\shots\\desktop.png"}),
                                  ImageSaveFormat::Png))
        .WillOnce(Return(true));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
}

TEST(app_controller, cli_explicit_output_without_overwrite_checks_reservation) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\exists.png";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 100, 100);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\exists.png"})))
        .WillOnce(Return(L"C:\\shots\\exists.png"));
    EXPECT_CALL(
        fixture.file_system,
        Try_reserve_exact_file_path(Eq(std::wstring_view{L"C:\\shots\\exists.png"}), _))
        .WillOnce(DoAll(SetArgReferee<1>(true), Return(false)));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliOutputPathFailure);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"already exists"));
}

TEST(app_controller,
     cli_save_failure_deletes_reserved_output_path_when_reservation_requested) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\fail.png";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 100, 100);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\fail.png"})))
        .WillOnce(Return(L"C:\\shots\\fail.png"));
    EXPECT_CALL(
        fixture.file_system,
        Try_reserve_exact_file_path(Eq(std::wstring_view{L"C:\\shots\\fail.png"}), _))
        .WillOnce(Return(true));
    EXPECT_CALL(fixture.capture,
                Save_rect_to_file(Make_save_request(desktop),
                                  Eq(std::wstring_view{L"C:\\shots\\fail.png"}),
                                  ImageSaveFormat::Png))
        .WillOnce(Return(false));
    EXPECT_CALL(fixture.file_system,
                Delete_file_if_exists(Eq(std::wstring_view{L"C:\\shots\\fail.png"})));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliCaptureSaveFailed);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"Failed to encode or write image"));
}

TEST(app_controller,
     cli_annotate_inline_json_prepares_before_output_resolution_and_saves_annotations) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\desktop-annotated.png";
    options.overwrite_output = true;
    options.annotate_value =
        L"{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":1,\"y\":2},"
        L"\"end\":{\"x\":10,\"y\":20},\"size\":3}]}";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    core::Annotation prepared_annotation{};
    prepared_annotation.id = 1;
    prepared_annotation.data = core::LineAnnotation{
        .start = PointPx{1, 2},
        .end = PointPx{10, 20},
        .style = StrokeStyle{.width_px = 3,
                             .color = Make_colorref(0x00, 0x00, 0x00),
                             .opacity_percent = StrokeStyle::kDefaultOpacityPercent},
        .arrow_head = false,
    };

    {
        ::testing::InSequence sequence;
        EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
            .Times(2)
            .WillRepeatedly(Return(desktop));
        EXPECT_CALL(fixture.annotation_preparation, Prepare_annotations(_))
            .WillOnce([&](core::AnnotationPreparationRequest const &request) {
                EXPECT_EQ(request.annotations.size(), 1u);
                core::AnnotationPreparationResult result{};
                if (!request.annotations.empty() &&
                    std::holds_alternative<core::LineAnnotation>(
                        request.annotations[0].data)) {
                    core::LineAnnotation const &line =
                        std::get<core::LineAnnotation>(request.annotations[0].data);
                    EXPECT_EQ(line.start, (PointPx{1, 2}));
                    EXPECT_EQ(line.end, (PointPx{10, 20}));
                    EXPECT_EQ(line.style.width_px, 3);
                    result.status = core::AnnotationPreparationStatus::Success;
                    result.annotations = {prepared_annotation};
                } else {
                    result.status = core::AnnotationPreparationStatus::InputInvalid;
                    result.error_message =
                        L"--annotate: expected a parsed line annotation.";
                }
                return result;
            });
        EXPECT_CALL(fixture.file_system, Resolve_absolute_path(Eq(std::wstring_view{
                                             L"C:\\shots\\desktop-annotated.png"})))
            .WillOnce(Return(L"C:\\shots\\desktop-annotated.png"));
        EXPECT_CALL(fixture.capture,
                    Save_rect_to_file(
                        _, Eq(std::wstring_view{L"C:\\shots\\desktop-annotated.png"}),
                        ImageSaveFormat::Png))
            .WillOnce([&](core::CaptureSaveRequest const &request, std::wstring_view,
                          ImageSaveFormat) {
                EXPECT_EQ(request.source_rect_screen, desktop);
                EXPECT_EQ(request.annotations.size(), 1u);
                EXPECT_EQ(request.annotations[0], prepared_annotation);
                return true;
            });
    }

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
}

TEST(app_controller, cli_annotate_parse_failure_does_not_resolve_output_path) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\out.png";
    options.annotate_value = L"{\"annotations\":[{\"bogus\":1}]}";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    // No EXPECT_CALL for Prepare_annotations or Resolve_absolute_path(output_path):
    // StrictMock will fail the test if either is unexpectedly invoked.

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliAnnotationInputInvalid);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"--annotate:"));
}

TEST(app_controller, cli_annotate_file_read_success_parses_and_saves_annotations) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\out.png";
    options.overwrite_output = true;
    options.annotate_value = L"annotations.json";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    core::Annotation prepared_annotation{};
    prepared_annotation.id = 7;
    prepared_annotation.data = core::LineAnnotation{
        .start = PointPx{1, 2},
        .end = PointPx{10, 20},
        .style = StrokeStyle{.width_px = 2,
                             .color = Make_colorref(0x00, 0x00, 0x00),
                             .opacity_percent = StrokeStyle::kDefaultOpacityPercent},
        .arrow_head = false,
    };

    {
        ::testing::InSequence sequence;
        EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
            .Times(2)
            .WillRepeatedly(Return(desktop));
        EXPECT_CALL(fixture.file_system,
                    Resolve_absolute_path(Eq(std::wstring_view{L"annotations.json"})))
            .WillOnce(Return(L"C:\\shots\\annotations.json"));
        EXPECT_CALL(fixture.file_system,
                    Try_read_text_file_utf8(
                        Eq(std::wstring_view{L"C:\\shots\\annotations.json"}), _, _))
            .WillOnce([](std::wstring_view, std::string &out, std::wstring &) {
                out =
                    R"({"annotations":[{"type":"line","start":{"x":1,"y":2},"end":{"x":10,"y":20},"size":2}]})";
                return true;
            });
        EXPECT_CALL(fixture.annotation_preparation, Prepare_annotations(_))
            .WillOnce([&](core::AnnotationPreparationRequest const &request) {
                core::AnnotationPreparationResult result{};
                result.status = core::AnnotationPreparationStatus::Success;
                result.annotations = {prepared_annotation};
                EXPECT_EQ(request.annotations.size(), 1u);
                return result;
            });
        EXPECT_CALL(fixture.file_system,
                    Resolve_absolute_path(Eq(std::wstring_view{L"C:\\shots\\out.png"})))
            .WillOnce(Return(L"C:\\shots\\out.png"));
        EXPECT_CALL(fixture.capture,
                    Save_rect_to_file(_, Eq(std::wstring_view{L"C:\\shots\\out.png"}),
                                      ImageSaveFormat::Png))
            .WillOnce([&](core::CaptureSaveRequest const &request, std::wstring_view,
                          ImageSaveFormat) {
                EXPECT_EQ(request.annotations.size(), 1u);
                EXPECT_EQ(request.annotations[0], prepared_annotation);
                return true;
            });
    }

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::Success);
}

TEST(app_controller, cli_annotate_file_read_failure_returns_exit_14) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\desktop-annotated.png";
    options.overwrite_output = true;
    options.annotate_value = L"annotations.json";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(fixture.file_system,
                Resolve_absolute_path(Eq(std::wstring_view{L"annotations.json"})))
        .WillOnce(Return(L"C:\\shots\\annotations.json"));
    EXPECT_CALL(fixture.file_system, Try_read_text_file_utf8(_, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(L"access denied"), Return(false)));

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliAnnotationInputInvalid);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"unable to read annotation file"));
}

TEST(app_controller, cli_annotate_preparation_input_invalid_returns_exit_14) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\desktop-annotated.png";
    options.overwrite_output = true;
    options.annotate_value =
        L"{\"annotations\":[{\"type\":\"line\",\"start\":{\"x\":0,\"y\":0},"
        L"\"end\":{\"x\":1,\"y\":1},\"size\":2}]}";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(fixture.annotation_preparation, Prepare_annotations(_))
        .WillOnce([](core::AnnotationPreparationRequest const &) {
            core::AnnotationPreparationResult result{};
            result.status = core::AnnotationPreparationStatus::InputInvalid;
            result.error_message =
                L"--annotate: font family \"Nope\" is not installed.";
            return result;
        });

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliAnnotationInputInvalid);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"font family"));
}

TEST(app_controller, cli_annotate_preparation_render_failed_returns_exit_11) {
    ControllerFixture fixture;
    CliOptions options{};
    options.capture_mode = CliCaptureMode::Desktop;
    options.output_path = L"C:\\shots\\desktop-annotated.png";
    options.overwrite_output = true;
    options.annotate_value =
        L"{\"annotations\":[{\"type\":\"text\",\"origin\":{\"x\":0,\"y\":0},"
        L"\"text\":\"hello\",\"size\":10}]}";

    RectPx const desktop = RectPx::From_ltrb(0, 0, 1920, 1080);
    EXPECT_CALL(fixture.display, Get_virtual_desktop_bounds_px())
        .Times(2)
        .WillRepeatedly(Return(desktop));
    EXPECT_CALL(fixture.annotation_preparation, Prepare_annotations(_))
        .WillOnce([](core::AnnotationPreparationRequest const &) {
            core::AnnotationPreparationResult result{};
            result.status = core::AnnotationPreparationStatus::RenderFailed;
            result.error_message =
                L"Error: Failed to rasterize a text annotation for --annotate.";
            return result;
        });

    CliResult const result = fixture.controller.Run_cli_capture_mode(options);
    EXPECT_EQ(result.exit_code, ProcessExitCode::CliCaptureSaveFailed);
    EXPECT_THAT(result.stderr_message, HasSubstr(L"Failed to rasterize"));
}
