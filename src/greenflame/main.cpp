// Entry point: optional console-detach relaunch, then run GreenflameApp.

#include "console_output.h"
#include "greenflame_app.h"
#include "version_string.h"
#include "win/debug_log.h"

namespace {

#ifdef DEBUG
constexpr bool kDebugBuild = true;
#else
constexpr bool kDebugBuild = false;
#endif
constexpr wchar_t kTrayInstanceMutexName[] = L"Local\\greenflame.tray.single_instance";

[[nodiscard]] std::wstring Build_semantic_version_text() {
    return L"Greenflame v" GREENFLAME_PRODUCT_VERSION_WIDE;
}

[[nodiscard]] std::vector<std::wstring> Command_line_args_without_program() {
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }
    std::vector<LPWSTR> argv_vec;
    argv_vec.reserve(static_cast<size_t>(argc));

    CLANG_WARN_IGNORE_PUSH("-Wunsafe-buffer-usage")
    for (int i = 0; i < argc; ++i) {
        argv_vec.push_back(argv[i]);
    }
    CLANG_WARN_IGNORE_POP()

    std::span<LPWSTR> const argv_span(argv_vec);
    args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0u);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv_span[static_cast<size_t>(i)]);
    }
    LocalFree(argv);
    return args;
}

class ScopedHandle final {
  public:
    ScopedHandle() = default;
    ~ScopedHandle() { Reset(); }

    ScopedHandle(ScopedHandle const &) = delete;
    ScopedHandle &operator=(ScopedHandle const &) = delete;

    void Reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_ = nullptr;
};

enum class SingleInstanceResult : uint8_t {
    Acquired = 0,
    AlreadyRunning = 1,
    Error = 2,
};

[[nodiscard]] SingleInstanceResult
Acquire_tray_single_instance_lock(ScopedHandle &lock) {
    SetLastError(ERROR_SUCCESS);
    HANDLE const mutex_handle = CreateMutexW(nullptr, FALSE, kTrayInstanceMutexName);
    if (mutex_handle == nullptr) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            return SingleInstanceResult::AlreadyRunning;
        }
        return SingleInstanceResult::Error;
    }
    lock.Reset(mutex_handle);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return SingleInstanceResult::AlreadyRunning;
    }
    return SingleInstanceResult::Acquired;
}

} // namespace

int WINAPI wWinMain(HINSTANCE h_instance, HINSTANCE, PWSTR, int) {
    GREENFLAME_INSTALL_DEBUG_CRASH_LOGGING();

    std::vector<std::wstring> const args = Command_line_args_without_program();
    greenflame::core::CliParseResult const parse_result =
        greenflame::core::Parse_cli_arguments(args, kDebugBuild);
    if (!parse_result.ok) {
        std::wstring error_line = L"Error: ";
        error_line += parse_result.error_message;
        greenflame::Write_console_line(error_line, true);
        return greenflame::To_exit_code(
            greenflame::ProcessExitCode::CliArgumentParseFailed);
    }

    if (parse_result.options.action == greenflame::core::CliAction::Help) {
        greenflame::Write_console_text(
            greenflame::core::Build_cli_help_text(kDebugBuild), false);
        return greenflame::To_exit_code(greenflame::ProcessExitCode::Success);
    }
    if (parse_result.options.action == greenflame::core::CliAction::Version) {
        greenflame::Write_console_line(Build_semantic_version_text(), false);
        return greenflame::To_exit_code(greenflame::ProcessExitCode::Success);
    }

    if (!greenflame::core::Has_cli_render_source(parse_result.options) &&
        GetConsoleWindow() != nullptr) {
        std::wstring command_line = GetCommandLineW();
        if (!command_line.empty()) {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                               DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                ExitProcess(
                    greenflame::To_exit_code(greenflame::ProcessExitCode::Success));
            }
        }
    }

    ScopedHandle tray_single_instance_lock;
    if (!greenflame::core::Has_cli_render_source(parse_result.options)) {
        SingleInstanceResult const lock_result =
            Acquire_tray_single_instance_lock(tray_single_instance_lock);
        if (lock_result == SingleInstanceResult::AlreadyRunning) {
            return greenflame::To_exit_code(greenflame::ProcessExitCode::Success);
        }
        if (lock_result == SingleInstanceResult::Error) {
            greenflame::Write_console_line(
                L"Error: Failed to enforce single-instance tray mode.", true);
            return greenflame::To_exit_code(
                greenflame::ProcessExitCode::TraySingleInstanceEnforcementFailed);
        }
    }

    greenflame::GreenflameApp app(h_instance, parse_result.options);
    return app.Run();
}
