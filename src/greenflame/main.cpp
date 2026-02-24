// Entry point: optional console-detach relaunch, then run GreenflameApp.

#include "greenflame_app.h"

namespace {

#ifdef DEBUG
constexpr bool kDebugBuild = true;
#else
constexpr bool kDebugBuild = false;
#endif
constexpr wchar_t kTrayInstanceMutexName[] = L"Local\\greenflame.tray.single_instance";

[[nodiscard]] std::vector<std::wstring> Command_line_args_without_program() {
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }
    args.reserve(static_cast<size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

void Write_console_text(std::wstring_view text, bool to_stderr) {
    DWORD const stream_id = to_stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
    HANDLE stream = GetStdHandle(stream_id);
    bool close_stream = false;
    if (stream == nullptr || stream == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) != 0 ||
            GetLastError() == ERROR_ACCESS_DENIED) {
            stream = GetStdHandle(stream_id);
        }
    }
    if (stream == nullptr || stream == INVALID_HANDLE_VALUE) {
        stream =
            CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stream != INVALID_HANDLE_VALUE) {
            close_stream = true;
        }
    }
    if (stream != nullptr && stream != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(stream, &mode) != 0) {
            DWORD written = 0;
            (void)WriteConsoleW(stream, text.data(), static_cast<DWORD>(text.size()),
                                &written, nullptr);
            if (close_stream) {
                CloseHandle(stream);
            }
            return;
        }
        if (!text.empty() && text.size() <= static_cast<size_t>(INT_MAX)) {
            int const source_chars = static_cast<int>(text.size());
            int const utf8_bytes = WideCharToMultiByte(
                CP_UTF8, 0, text.data(), source_chars, nullptr, 0, nullptr, nullptr);
            if (utf8_bytes > 0) {
                std::string utf8(static_cast<size_t>(utf8_bytes), '\0');
                int const converted =
                    WideCharToMultiByte(CP_UTF8, 0, text.data(), source_chars,
                                        utf8.data(), utf8_bytes, nullptr, nullptr);
                if (converted > 0) {
                    DWORD written = 0;
                    if (WriteFile(stream, utf8.data(), static_cast<DWORD>(converted),
                                  &written, nullptr) != 0) {
                        if (close_stream) {
                            CloseHandle(stream);
                        }
                        return;
                    }
                }
            }
        }
        if (close_stream) {
            CloseHandle(stream);
        }
    }
    std::wstring message(text);
    OutputDebugStringW(message.c_str());
}

void Write_console_line(std::wstring_view text, bool to_stderr) {
    Write_console_text(text, to_stderr);
    Write_console_text(L"\n", to_stderr);
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

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }

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
    std::vector<std::wstring> const args = Command_line_args_without_program();
    greenflame::core::CliParseResult const parse_result =
        greenflame::core::Parse_cli_arguments(args, kDebugBuild);
    if (!parse_result.ok) {
        std::wstring error_line = L"Error: ";
        error_line += parse_result.error_message;
        Write_console_line(error_line, true);
        return 2;
    }

    if (parse_result.options.capture_mode == greenflame::core::CliCaptureMode::Help) {
        Write_console_text(greenflame::core::Build_cli_help_text(kDebugBuild), false);
        return 0;
    }

    if (parse_result.options.capture_mode == greenflame::core::CliCaptureMode::None &&
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
                ExitProcess(0);
            }
        }
    }

    ScopedHandle tray_single_instance_lock;
    if (parse_result.options.capture_mode == greenflame::core::CliCaptureMode::None) {
        SingleInstanceResult const lock_result =
            Acquire_tray_single_instance_lock(tray_single_instance_lock);
        if (lock_result == SingleInstanceResult::AlreadyRunning) {
            return 0;
        }
        if (lock_result == SingleInstanceResult::Error) {
            Write_console_line(L"Error: Failed to enforce single-instance tray mode.",
                               true);
            return 3;
        }
    }

    greenflame::GreenflameApp app(h_instance, parse_result.options);
    return app.Run();
}
