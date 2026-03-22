#include "greenflame_core/app_controller.h"

#include "greenflame_core/app_config.h"
#include "greenflame_core/cli_annotation_import.h"
#include "greenflame_core/output_path.h"
#include "greenflame_core/string_utils.h"
#include "greenflame_core/window_filter.h"

namespace {

constexpr wchar_t kClipboardCopiedBalloonMessage[] = L"Selection copied to clipboard.";
constexpr wchar_t kNoLastRegionMessage[] = L"No previously captured region.";
constexpr wchar_t kNoLastWindowMessage[] = L"No previously captured window.";
constexpr wchar_t kLastWindowClosedMessage[] =
    L"Previously captured window is no longer available.";
constexpr wchar_t kLastWindowMinimizedMessage[] =
    L"Previously captured window is minimized.";

[[nodiscard]] std::wstring_view
Pattern_for_source(greenflame::core::AppConfig const &config,
                   greenflame::core::SaveSelectionSource source) {
    switch (source) {
    case greenflame::core::SaveSelectionSource::Region:
        return config.filename_pattern_region;
    case greenflame::core::SaveSelectionSource::Window:
        return config.filename_pattern_window;
    case greenflame::core::SaveSelectionSource::Monitor:
        return config.filename_pattern_monitor;
    case greenflame::core::SaveSelectionSource::Desktop:
        return config.filename_pattern_desktop;
    }
    return {};
}

[[nodiscard]] greenflame::core::ImageSaveFormat Default_image_save_format_from_config(
    greenflame::core::AppConfig const &config) noexcept {
    if (config.default_save_format == L"jpg" || config.default_save_format == L"jpeg") {
        return greenflame::core::ImageSaveFormat::Jpeg;
    }
    if (config.default_save_format == L"bmp") {
        return greenflame::core::ImageSaveFormat::Bmp;
    }
    return greenflame::core::ImageSaveFormat::Png;
}

[[nodiscard]] COLORREF
Resolve_padding_color(greenflame::core::AppConfig const &config,
                      greenflame::core::CliOptions const &cli_options) noexcept {
    if (cli_options.padding_color_override.has_value()) {
        return *cli_options.padding_color_override;
    }
    return config.padding_color;
}

void Update_default_save_dir_from_path(greenflame::core::AppConfig &config,
                                       std::wstring_view full_path) {
    size_t const slash = full_path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return;
    }
    config.default_save_dir = std::wstring(full_path.substr(0, slash));
    config.Normalize();
}

void Append_line(std::wstring &text, std::wstring_view line) {
    if (!text.empty()) {
        text += L'\n';
    }
    text += line;
}

[[nodiscard]] std::wstring_view Trim_wspace(std::wstring_view text) noexcept {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(text[begin]) != 0) {
        ++begin;
    }
    while (end > begin && std::iswspace(text[end - 1]) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

[[nodiscard]] bool Try_encode_utf8(std::wstring_view value,
                                   std::string &utf8) noexcept {
    utf8.clear();
    if (value.empty()) {
        return true;
    }

    int const required_chars =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            nullptr, 0, nullptr, nullptr);
    if (required_chars <= 0) {
        return false;
    }

    utf8.resize(static_cast<size_t>(required_chars));
    int const converted_chars =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            utf8.data(), required_chars, nullptr, nullptr);
    return converted_chars == required_chars;
}

[[nodiscard]] greenflame::CliResult Make_cli_error(greenflame::ProcessExitCode code,
                                                   std::wstring_view message) {
    greenflame::CliResult result{};
    result.exit_code = code;
    result.stderr_message = std::wstring(message);
    return result;
}

[[nodiscard]] bool
Try_compute_padded_output_size(greenflame::core::RectPx const &source_rect,
                               greenflame::core::InsetsPx padding, int32_t &width,
                               int32_t &height) noexcept {
    int32_t source_width = 0;
    int32_t source_height = 0;
    if (!source_rect.Try_get_size(source_width, source_height)) {
        return false;
    }
    return padding.Try_expand_size(source_width, source_height, width, height);
}

[[nodiscard]] std::optional<size_t>
Find_unique_exact_title_match(std::vector<greenflame::WindowMatch> const &matches,
                              std::wstring_view query) noexcept {
    std::optional<size_t> exact_match_index = std::nullopt;
    for (size_t i = 0; i < matches.size(); ++i) {
        if (!greenflame::core::Equals_no_case(matches[i].info.title, query)) {
            continue;
        }
        if (exact_match_index.has_value()) {
            return std::nullopt;
        }
        exact_match_index = i;
    }
    return exact_match_index;
}

} // namespace

namespace greenflame {

AppController::AppController(
    core::AppConfig &config, IDisplayQueries &display_queries,
    IWindowInspector &window_inspector, ICaptureService &capture_service,
    IAnnotationPreparationService &annotation_preparation_service,
    IFileSystemService &file_system_service)
    : config_(config), display_queries_(display_queries),
      window_inspector_(window_inspector), capture_service_(capture_service),
      annotation_preparation_service_(annotation_preparation_service),
      file_system_service_(file_system_service) {}

ClipboardCopyResult
AppController::On_copy_window_to_clipboard_requested(HWND target_window) {
    if (target_window != nullptr) {
        std::optional<core::RectPx> const target_rect =
            window_inspector_.Get_window_rect(target_window);
        if (target_rect.has_value() &&
            capture_service_.Copy_rect_to_clipboard(*target_rect)) {
            Store_last_capture(*target_rect, target_window);
            return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
        }
    }

    std::optional<core::RectPx> const foreground_rect =
        window_inspector_.Get_foreground_window_rect(target_window);
    if (foreground_rect.has_value() &&
        capture_service_.Copy_rect_to_clipboard(*foreground_rect)) {
        Store_last_capture(*foreground_rect, std::nullopt);
        return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
    }

    core::PointPx const cursor = display_queries_.Get_cursor_pos_px();
    POINT const cursor_point{cursor.x, cursor.y};
    std::optional<core::RectPx> const fallback_rect =
        window_inspector_.Get_window_rect_under_cursor(cursor_point, nullptr);
    if (fallback_rect.has_value() &&
        capture_service_.Copy_rect_to_clipboard(*fallback_rect)) {
        Store_last_capture(*fallback_rect, std::nullopt);
        return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
    }

    return {};
}

ClipboardCopyResult AppController::On_copy_monitor_to_clipboard_requested() {
    std::vector<core::MonitorWithBounds> const monitors =
        display_queries_.Get_monitors_with_bounds();
    if (monitors.empty()) {
        return {};
    }

    core::PointPx const cursor = display_queries_.Get_cursor_pos_px();
    std::optional<size_t> const index =
        core::Index_of_monitor_containing(cursor, monitors);
    if (!index.has_value()) {
        return {};
    }

    core::RectPx const rect = monitors[*index].bounds;
    if (capture_service_.Copy_rect_to_clipboard(rect)) {
        Store_last_capture(rect, std::nullopt);
        return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
    }
    return {};
}

ClipboardCopyResult AppController::On_copy_desktop_to_clipboard_requested() {
    core::RectPx const rect = display_queries_.Get_virtual_desktop_bounds_px();
    if (capture_service_.Copy_rect_to_clipboard(rect)) {
        Store_last_capture(rect, std::nullopt);
        return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
    }
    return {};
}

ClipboardCopyResult AppController::On_copy_last_region_to_clipboard_requested() {
    if (!last_capture_screen_rect_.has_value()) {
        return ClipboardCopyResult{kNoLastRegionMessage, false};
    }
    if (capture_service_.Copy_rect_to_clipboard(*last_capture_screen_rect_)) {
        return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
    }
    return ClipboardCopyResult{kNoLastRegionMessage, false};
}

ClipboardCopyResult AppController::On_copy_last_window_to_clipboard_requested() {
    if (!last_capture_window_.has_value()) {
        return ClipboardCopyResult{kNoLastWindowMessage, false};
    }
    HWND const hwnd = *last_capture_window_;
    if (!window_inspector_.Is_window_valid(hwnd)) {
        last_capture_window_ = std::nullopt;
        return ClipboardCopyResult{kLastWindowClosedMessage, false};
    }
    if (window_inspector_.Is_window_minimized(hwnd)) {
        return ClipboardCopyResult{kLastWindowMinimizedMessage, false};
    }
    std::optional<core::RectPx> const rect = window_inspector_.Get_window_rect(hwnd);
    if (!rect.has_value()) {
        last_capture_window_ = std::nullopt;
        return ClipboardCopyResult{kLastWindowClosedMessage, false};
    }
    if (capture_service_.Copy_rect_to_clipboard(*rect)) {
        Store_last_capture(*rect, hwnd);
        return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
    }
    return {};
}

ClipboardCopyResult
AppController::On_selection_copied_to_clipboard(core::RectPx screen_rect,
                                                std::optional<HWND> window) {
    Store_last_capture(screen_rect, window);
    return ClipboardCopyResult{kClipboardCopiedBalloonMessage, true};
}

SelectionSavedResult AppController::On_selection_saved_to_file(
    core::RectPx screen_rect, std::optional<HWND> window, std::wstring_view saved_path,
    bool file_copied) {
    Store_last_capture(screen_rect, window);
    return SelectionSavedResult{
        core::Build_saved_selection_balloon_message(saved_path, file_copied),
        std::wstring(saved_path)};
}

core::OverlayHelpContent AppController::Build_overlay_help_content() const {
    core::OverlayHelpContent content{};
    content.title = L"Keyboard Shortcuts";
    content.close_hint = L"Ctrl+H or Esc to close";

    core::OverlayHelpSection copy_and_save{};
    copy_and_save.title = L"Copy and Save";
    copy_and_save.entries = {
        {L"Ctrl + C", L"Copy selection to clipboard"},
        {L"Ctrl + S", L"Save selection to default location"},
        {L"Ctrl + Alt + S", L"Save selection and copy saved file path"},
        {L"Ctrl + Shift + S", L"Save selection as..."},
        {L"Ctrl + Shift + Alt + S", L"Save selection as... and copy saved file path"},
    };
    content.sections.push_back(std::move(copy_and_save));

    core::OverlayHelpSection edit{};
    edit.title = L"Edit";
    edit.gap_before = true;
    edit.entries = {
        {L"Delete", L"Delete selected annotation"},
        {L"Ctrl + Z", L"Undo last region or annotation change"},
        {L"Ctrl + Shift + Z", L"Redo last undone change"},
    };
    content.sections.push_back(std::move(edit));

    core::OverlayHelpSection tools{};
    tools.title = L"Annotation Tools";
    tools.new_column = true;
    tools.entries = {
        {L"B", L"Toggle Brush tool"},
        {L"H", L"Toggle Highlighter tool"},
        {L"L", L"Toggle Line tool"},
        {L"A", L"Toggle Arrow tool"},
        {L"R", L"Toggle Rectangle tool"},
        {L"Shift+R", L"Toggle Filled rectangle tool"},
        {L"E", L"Toggle Ellipse tool"},
        {L"Shift+E", L"Toggle Filled ellipse tool"},
        {L"T", L"Toggle Text tool"},
        {L"N", L"Toggle Bubble tool"},
        {L"Right Click", L"Open the active tool's color wheel at cursor"},
        {L"Wheel Up / Ctrl + =",
         L"Increase Brush/Highlighter/Line/Arrow/Rectangle/Ellipse width"},
        {L"Wheel Down / Ctrl + -",
         L"Decrease Brush/Highlighter/Line/Arrow/Rectangle/Ellipse width"},
    };
    content.sections.push_back(std::move(tools));

    return content;
}

CliResult AppController::Run_cli_capture_mode(core::CliOptions const &cli_options) {
    core::RectPx target_rect = {};
    core::SaveSelectionSource source = core::SaveSelectionSource::Region;
    std::optional<size_t> monitor_index_zero_based = std::nullopt;
    std::wstring window_title = {};
    std::optional<HWND> captured_window = std::nullopt;
    WindowObscuration window_obscuration = WindowObscuration::None;
    bool window_partially_out_of_bounds = false;
    bool const has_padding = cli_options.padding_px.has_value();
    core::InsetsPx const padding_px = cli_options.padding_px.value_or(core::InsetsPx{});
    COLORREF const padding_color = Resolve_padding_color(config_, cli_options);

    switch (cli_options.capture_mode) {
    case core::CliCaptureMode::Region:
        if (!cli_options.region_px.has_value()) {
            return Make_cli_error(ProcessExitCode::CliRegionMissing,
                                  L"Error: --region is required.");
        }
        target_rect = *cli_options.region_px;
        source = core::SaveSelectionSource::Region;
        break;
    case core::CliCaptureMode::Window: {
        if (cli_options.window_hwnd.has_value()) {
            HWND const requested_hwnd =
                reinterpret_cast<HWND>(*cli_options.window_hwnd);
            if (!window_inspector_.Is_window_valid(requested_hwnd)) {
                return Make_cli_error(
                    ProcessExitCode::CliWindowUnavailable,
                    L"Error: Specified window handle is not available. Try again.");
            }
            if (window_inspector_.Is_window_minimized(requested_hwnd)) {
                return Make_cli_error(
                    ProcessExitCode::CliWindowMinimized,
                    L"Error: Specified window handle is minimized. Restore it and "
                    L"try again.");
            }

            std::optional<core::WindowCandidateInfo> const window_info =
                window_inspector_.Get_window_info(requested_hwnd);
            if (!window_info.has_value()) {
                return Make_cli_error(
                    ProcessExitCode::CliWindowUnavailable,
                    L"Error: Specified window handle is not a visible top-level "
                    L"window. Try again.");
            }

            captured_window = requested_hwnd;
            target_rect = window_info->rect;
            window_title = window_info->title;
            source = core::SaveSelectionSource::Window;
            window_obscuration =
                window_inspector_.Get_window_obscuration(*captured_window);
            break;
        }

        std::vector<WindowMatch> const raw_matches =
            window_inspector_.Find_windows_by_title(cli_options.window_name);

        std::vector<WindowMatch> matches = {};
        matches.reserve(raw_matches.size());
        for (WindowMatch const &match : raw_matches) {
            if (!core::Is_cli_invocation_window(match.info, cli_options.window_name)) {
                matches.push_back(match);
            }
        }

        if (matches.empty()) {
            std::wstring message = L"Error: No visible window matches: ";
            message += cli_options.window_name;
            return Make_cli_error(ProcessExitCode::CliWindowNotFound, message);
        }

        size_t selected_match_index = 0;
        if (matches.size() > 1) {
            std::optional<size_t> const exact_match_index =
                Find_unique_exact_title_match(matches, cli_options.window_name);
            if (exact_match_index.has_value()) {
                selected_match_index = *exact_match_index;
            } else {
                std::wstring stderr_text = L"Error: Window name is ambiguous (";
                stderr_text += std::to_wstring(matches.size());
                stderr_text += L" matches): ";
                stderr_text += cli_options.window_name;
                Append_line(stderr_text, L"Matching windows:");
                for (size_t i = 0; i < matches.size(); ++i) {
                    Append_line(stderr_text,
                                core::Format_window_candidate_line(matches[i].info, i));
                }
                return Make_cli_error(ProcessExitCode::CliWindowAmbiguous, stderr_text);
            }
        }

        captured_window = matches[selected_match_index].hwnd;
        if (!window_inspector_.Is_window_valid(*captured_window)) {
            return Make_cli_error(
                ProcessExitCode::CliWindowUnavailable,
                L"Error: Matched window is no longer available. Try again.");
        }
        if (window_inspector_.Is_window_minimized(*captured_window)) {
            return Make_cli_error(
                ProcessExitCode::CliWindowMinimized,
                L"Error: Matched window is minimized. Restore it and try again.");
        }

        std::optional<core::RectPx> const current_rect =
            window_inspector_.Get_window_rect(*captured_window);
        if (!current_rect.has_value()) {
            return Make_cli_error(
                ProcessExitCode::CliWindowUnavailable,
                L"Error: Matched window is no longer capturable. Try again.");
        }

        target_rect = *current_rect;
        window_title = matches[selected_match_index].info.title;
        source = core::SaveSelectionSource::Window;
        window_obscuration = window_inspector_.Get_window_obscuration(*captured_window);
        break;
    }
    case core::CliCaptureMode::Monitor: {
        std::vector<core::MonitorWithBounds> const monitors =
            display_queries_.Get_monitors_with_bounds();
        if (monitors.empty()) {
            return Make_cli_error(ProcessExitCode::CliNoMonitorsAvailable,
                                  L"Error: No monitors are available.");
        }
        if (cli_options.monitor_id < 1 ||
            static_cast<size_t>(cli_options.monitor_id) > monitors.size()) {
            std::wstring message = L"Error: --monitor id is out of range (1..";
            message += std::to_wstring(monitors.size());
            message += L").";
            return Make_cli_error(ProcessExitCode::CliMonitorOutOfRange, message);
        }
        monitor_index_zero_based = static_cast<size_t>(cli_options.monitor_id - 1);
        target_rect = monitors[*monitor_index_zero_based].bounds;
        source = core::SaveSelectionSource::Monitor;
        break;
    }
    case core::CliCaptureMode::Desktop:
        target_rect = display_queries_.Get_virtual_desktop_bounds_px();
        source = core::SaveSelectionSource::Desktop;
        break;
    case core::CliCaptureMode::None:
        return {};
    }

    [[maybe_unused]] int32_t padded_output_width = 0;
    [[maybe_unused]] int32_t padded_output_height = 0;
    if (has_padding &&
        !Try_compute_padded_output_size(target_rect, padding_px, padded_output_width,
                                        padded_output_height)) {
        return Make_cli_error(
            ProcessExitCode::CliCaptureSaveFailed,
            L"Error: Requested padded output dimensions are invalid or too large.");
    }

    core::RectPx const virtual_bounds =
        display_queries_.Get_virtual_desktop_bounds_px();
    std::wstring stderr_text = {};
    std::optional<core::RectPx> const clipped =
        core::RectPx::Clip(target_rect, virtual_bounds);
    window_partially_out_of_bounds = clipped.has_value() && *clipped != target_rect;
    if (!clipped.has_value()) {
        if (cli_options.capture_mode == core::CliCaptureMode::Window) {
            Append_line(stderr_text,
                        L"Error: Matched window is completely outside the virtual "
                        L"desktop. Nothing to capture.");
        } else {
            Append_line(stderr_text,
                        L"Error: Requested capture area is outside the virtual "
                        L"desktop.");
        }
        return CliResult{{}, stderr_text, ProcessExitCode::CliCaptureSaveFailed};
    }

    if (window_obscuration == WindowObscuration::Full) {
        Append_line(stderr_text,
                    L"Warning: Matched window is fully obscured by other windows. "
                    L"The saved image may not show that window.");
    } else if (!has_padding && window_obscuration == WindowObscuration::Partial &&
               window_partially_out_of_bounds) {
        Append_line(stderr_text,
                    L"Warning: Matched window is partially obscured and partially "
                    L"outside visible desktop bounds. The saved image may include "
                    L"other windows and may clip the target window.");
    } else if (window_obscuration == WindowObscuration::Partial) {
        Append_line(stderr_text,
                    L"Warning: Matched window is partially obscured by other "
                    L"windows. The saved image may include those windows.");
    } else if (!has_padding && window_partially_out_of_bounds) {
        Append_line(stderr_text,
                    L"Warning: Matched window is partially outside visible desktop "
                    L"bounds. The saved image may clip the target window.");
    }
    if (has_padding && window_partially_out_of_bounds) {
        Append_line(stderr_text,
                    L"Warning: Requested capture area extends outside the virtual "
                    L"desktop. Uncovered areas were filled with the padding color.");
    }

    std::vector<core::Annotation> prepared_annotations = {};
    if (cli_options.annotate_value.has_value()) {
        std::wstring_view const annotate_value =
            Trim_wspace(*cli_options.annotate_value);
        std::string annotation_json = {};
        if (core::Classify_cli_annotation_input(annotate_value) ==
            core::CliAnnotationInputKind::InlineJson) {
            if (!Try_encode_utf8(annotate_value, annotation_json)) {
                return Make_cli_error(
                    ProcessExitCode::CliAnnotationInputInvalid,
                    L"Error: --annotate inline JSON could not be encoded as UTF-8.");
            }
        } else {
            std::wstring const annotation_path =
                file_system_service_.Resolve_absolute_path(annotate_value);
            std::wstring read_error = {};
            if (!file_system_service_.Try_read_text_file_utf8(
                    annotation_path, annotation_json, read_error)) {
                std::wstring message = L"--annotate: unable to read annotation file \"";
                message += annotation_path;
                message += L"\"";
                if (!read_error.empty()) {
                    message += L": ";
                    message += read_error;
                }
                return Make_cli_error(ProcessExitCode::CliAnnotationInputInvalid,
                                      message);
            }
        }

        core::CliAnnotationParseContext const parse_context{
            .capture_rect_screen = target_rect,
            .virtual_desktop_bounds = virtual_bounds,
            .config = &config_,
        };
        core::CliAnnotationParseResult const parsed_annotations =
            core::Parse_cli_annotations_json(annotation_json, parse_context);
        if (!parsed_annotations.ok) {
            return Make_cli_error(ProcessExitCode::CliAnnotationInputInvalid,
                                  parsed_annotations.error_message);
        }

        core::AnnotationPreparationRequest const prepare_request{
            .annotations = parsed_annotations.annotations,
            .preset_font_families = core::Resolve_text_font_families(config_),
        };
        core::AnnotationPreparationResult const prepared_result =
            annotation_preparation_service_.Prepare_annotations(prepare_request);
        switch (prepared_result.status) {
        case core::AnnotationPreparationStatus::Success:
            prepared_annotations = prepared_result.annotations;
            break;
        case core::AnnotationPreparationStatus::InputInvalid:
            return Make_cli_error(ProcessExitCode::CliAnnotationInputInvalid,
                                  prepared_result.error_message);
        case core::AnnotationPreparationStatus::RenderFailed:
            return Make_cli_error(ProcessExitCode::CliCaptureSaveFailed,
                                  prepared_result.error_message);
        }
    }

    core::ImageSaveFormat const default_format =
        cli_options.output_format.has_value()
            ? core::Image_save_format_from_cli_format(*cli_options.output_format)
            : Default_image_save_format_from_config(config_);
    core::ImageSaveFormat output_format = default_format;

    bool const has_explicit_output_path = !cli_options.output_path.empty();
    std::wstring output_path = {};
    if (has_explicit_output_path) {
        core::ResolveExplicitPathResult const resolved =
            core::Resolve_explicit_output_path(cli_options.output_path, default_format,
                                               cli_options.output_format);
        if (!resolved.ok || resolved.path.empty()) {
            if (!resolved.error_message.empty()) {
                return Make_cli_error(ProcessExitCode::CliOutputPathFailure,
                                      resolved.error_message);
            }
            return Make_cli_error(ProcessExitCode::CliOutputPathFailure,
                                  L"Error: Unable to resolve output path.");
        }
        output_path = resolved.path;
        output_format = resolved.format;
    } else {
        output_path = Build_default_output_path(source, monitor_index_zero_based,
                                                window_title, default_format);
    }

    output_path = file_system_service_.Resolve_absolute_path(output_path);

    bool delete_output_path_on_failure = false;
    if (!has_explicit_output_path) {
        std::wstring const reserved =
            file_system_service_.Reserve_unique_file_path(output_path);
        if (reserved.empty()) {
            return Make_cli_error(ProcessExitCode::CliOutputPathFailure,
                                  L"Error: Unable to reserve an output path.");
        }
        output_path = reserved;
        delete_output_path_on_failure = true;
    } else if (!cli_options.overwrite_output) {
        bool already_exists = false;
        if (!file_system_service_.Try_reserve_exact_file_path(output_path,
                                                              already_exists)) {
            if (already_exists) {
                std::wstring message = L"Error: Output file already exists: ";
                message += output_path;
                message += L". Use --overwrite (or -f) to replace it.";
                return Make_cli_error(ProcessExitCode::CliOutputPathFailure, message);
            }
            return Make_cli_error(ProcessExitCode::CliOutputPathFailure,
                                  L"Error: Unable to reserve the output path.");
        }
        delete_output_path_on_failure = true;
    }

    core::CaptureSaveRequest save_request{};
    save_request.source_rect_screen = target_rect;
    save_request.padding_px = padding_px;
    save_request.fill_color = padding_color;
    save_request.preserve_source_extent = has_padding;
    save_request.annotations = prepared_annotations;

    if (!capture_service_.Save_rect_to_file(save_request, output_path, output_format)) {
        if (delete_output_path_on_failure) {
            file_system_service_.Delete_file_if_exists(output_path);
        }
        std::wstring error_message = L"Error: Failed to encode or write image file: ";
        error_message += output_path;
        Append_line(stderr_text, error_message);
        return CliResult{{}, stderr_text, ProcessExitCode::CliCaptureSaveFailed};
    }

    Update_default_save_dir_from_path(config_, output_path);
    Store_last_capture(target_rect, captured_window);
    config_.Normalize();

    std::wstring stdout_text = L"Saved: ";
    stdout_text += output_path;
    return CliResult{stdout_text, stderr_text, ProcessExitCode::Success};
}

std::wstring AppController::Build_default_output_path(
    core::SaveSelectionSource source, std::optional<size_t> monitor_index_zero_based,
    std::wstring_view window_title, core::ImageSaveFormat format) const {
    std::wstring const save_dir =
        file_system_service_.Resolve_save_directory(config_.default_save_dir);

    core::FilenamePatternContext context{};
    context.timestamp = file_system_service_.Get_current_timestamp();
    context.monitor_index_zero_based = monitor_index_zero_based;
    context.window_title = window_title;

    std::wstring_view const configured_pattern = Pattern_for_source(config_, source);
    std::wstring_view const effective_pattern =
        configured_pattern.empty() ? core::Default_filename_pattern(source)
                                   : configured_pattern;
    if (core::Pattern_uses_num(effective_pattern)) {
        std::vector<std::wstring> const files =
            file_system_service_.List_directory_filenames(save_dir);
        context.incrementing_number =
            core::Find_next_num_for_pattern(effective_pattern, context, files);
    }

    std::wstring const base_name =
        core::Build_default_save_name(source, context, configured_pattern);
    std::wstring output_path = save_dir;
    if (!output_path.empty() && output_path.back() != L'\\') {
        output_path += L'\\';
    }
    output_path += base_name;
    output_path += core::Extension_for_image_save_format(format);
    return output_path;
}

void AppController::Store_last_capture(core::RectPx screen_rect,
                                       std::optional<HWND> window) {
    last_capture_screen_rect_ = screen_rect.Normalized();
    if (window.has_value()) {
        last_capture_window_ = *window;
    }
}

} // namespace greenflame
