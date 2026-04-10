#include "console_output.h"

namespace greenflame {

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
            std::wstring const text_copy(text);
            int const utf8_bytes = WideCharToMultiByte(
                CP_UTF8, 0, text_copy.c_str(), source_chars, nullptr, 0, nullptr,
                nullptr);
            if (utf8_bytes > 0) {
                std::string utf8(static_cast<size_t>(utf8_bytes), '\0');
                int const converted =
                    WideCharToMultiByte(CP_UTF8, 0, text_copy.c_str(), source_chars,
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

void Write_console_block(std::wstring_view text, bool to_stderr) {
    if (text.empty()) {
        return;
    }
    size_t line_start = 0;
    while (line_start <= text.size()) {
        size_t const line_end = text.find(L'\n', line_start);
        if (line_end == std::wstring_view::npos) {
            Write_console_line(text.substr(line_start), to_stderr);
            break;
        }
        Write_console_line(text.substr(line_start, line_end - line_start), to_stderr);
        line_start = line_end + 1;
        if (line_start == text.size()) {
            break;
        }
    }
}

} // namespace greenflame
