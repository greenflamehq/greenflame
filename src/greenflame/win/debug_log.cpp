#include "win/debug_log.h"

#if defined(GREENFLAME_LOG)

#include "greenflame_core/rect_px.h"

namespace {

constexpr wchar_t kDebugLogFileName[] = L"greenflame-debug.log";
constexpr DWORD kAppendFileShareMask =
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr std::array<wchar_t, 16> kHexChars = {{L'0', L'1', L'2', L'3', L'4', L'5',
                                                L'6', L'7', L'8', L'9', L'A', L'B',
                                                L'C', L'D', L'E', L'F'}};

[[nodiscard]] std::mutex &Debug_log_mutex() noexcept {
    static std::mutex mutex = {};
    return mutex;
}

[[nodiscard]] std::wstring Zero_pad_uint(uint32_t value, size_t width) {
    std::wstring text = std::to_wstring(value);
    if (text.size() < width) {
        text.insert(text.begin(), width - text.size(), L'0');
    }
    return text;
}

[[nodiscard]] std::wstring Build_debug_log_path() {
    std::array<wchar_t, MAX_PATH + 1> temp_path = {};
    DWORD const length =
        GetTempPathW(static_cast<DWORD>(temp_path.size()), temp_path.data());

    std::wstring path = {};
    if (length > 0 && length < temp_path.size()) {
        path.assign(temp_path.data(), static_cast<size_t>(length));
    } else {
        path = L".\\";
    }

    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path += kDebugLogFileName;
    return path;
}

[[nodiscard]] std::wstring Build_log_line(std::wstring_view category,
                                          std::wstring_view message) {
    SYSTEMTIME local_time = {};
    GetLocalTime(&local_time);

    std::wstring line = {};
    line.reserve(96 + category.size() + message.size());
    line += std::to_wstring(local_time.wYear);
    line.push_back(L'-');
    line += Zero_pad_uint(local_time.wMonth, 2);
    line.push_back(L'-');
    line += Zero_pad_uint(local_time.wDay, 2);
    line.push_back(L' ');
    line += Zero_pad_uint(local_time.wHour, 2);
    line.push_back(L':');
    line += Zero_pad_uint(local_time.wMinute, 2);
    line.push_back(L':');
    line += Zero_pad_uint(local_time.wSecond, 2);
    line.push_back(L'.');
    line += Zero_pad_uint(local_time.wMilliseconds, 3);
    line += L" [";
    line += std::to_wstring(GetCurrentProcessId());
    line.push_back(L':');
    line += std::to_wstring(GetCurrentThreadId());
    line += L"] [";
    line.append(category);
    line += L"] ";
    line.append(message);
    line += L"\r\n";
    return line;
}

[[nodiscard]] std::string Utf8_from_wide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    int const required_bytes =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            nullptr, 0, nullptr, nullptr);
    if (required_bytes <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(required_bytes), '\0');
    int const converted_bytes =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            utf8.data(), required_bytes, nullptr, nullptr);
    if (converted_bytes != required_bytes) {
        return {};
    }

    return utf8;
}

[[nodiscard]] std::wstring Format_hex_uint64(uint64_t value, size_t width) {
    std::wstring text(width, L'0');
    for (size_t index = 0; index < width; ++index) {
        size_t const nibble_index = width - 1u - index;
        text[nibble_index] = kHexChars[value & 0xFu];
        value >>= 4u;
    }
    return text;
}

LONG WINAPI
Debug_unhandled_exception_filter(EXCEPTION_POINTERS *exception_pointers) noexcept {
    try {
        if (exception_pointers == nullptr ||
            exception_pointers->ExceptionRecord == nullptr) {
            greenflame::Debug_log_write(
                L"crash", L"Unhandled exception with null exception pointers");
            return EXCEPTION_CONTINUE_SEARCH;
        }

        EXCEPTION_RECORD const *const exception_record =
            exception_pointers->ExceptionRecord;
        std::wstring message = L"Unhandled exception code=0x";
        message += Format_hex_uint64(
            static_cast<uint32_t>(exception_record->ExceptionCode), 8);
        message += L" address=0x";
        message += Format_hex_uint64(
            reinterpret_cast<uint64_t>(exception_record->ExceptionAddress), 16);

        HMODULE const module_base = GetModuleHandleW(nullptr);
        if (module_base != nullptr && exception_record->ExceptionAddress != nullptr) {
            uintptr_t const module_address = reinterpret_cast<uintptr_t>(module_base);
            uintptr_t const exception_address =
                reinterpret_cast<uintptr_t>(exception_record->ExceptionAddress);
            if (exception_address >= module_address) {
                message += L" exe_offset=0x";
                message += Format_hex_uint64(
                    static_cast<uint64_t>(exception_address - module_address), 16);
            }
        }

        CONTEXT const *const context = exception_pointers->ContextRecord;
        if (context != nullptr) {
#if defined(_M_X64)
            message += L" rip=0x";
            message += Format_hex_uint64(context->Rip, 16);
            message += L" rsp=0x";
            message += Format_hex_uint64(context->Rsp, 16);
#elif defined(_M_IX86)
            message += L" eip=0x";
            message += Format_hex_uint64(context->Eip, 8);
            message += L" esp=0x";
            message += Format_hex_uint64(context->Esp, 8);
#endif
        }

        if (exception_record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            exception_record->NumberParameters >= 2) {
            message += L" access=";
            ULONG_PTR const operation = exception_record->ExceptionInformation[0];
            if (operation == 0u) {
                message += L"read";
            } else if (operation == 1u) {
                message += L"write";
            } else if (operation == 8u) {
                message += L"execute";
            } else {
                message += L"unknown";
            }
            message += L" target=0x";
            message += Format_hex_uint64(
                static_cast<uint64_t>(exception_record->ExceptionInformation[1]), 16);
        }

        greenflame::Debug_log_write(L"crash", message);
    } catch (...) {
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

namespace greenflame {

std::wstring const &Debug_log_path() noexcept {
    static std::wstring *const kPath = new std::wstring(Build_debug_log_path());
    return *kPath;
}

void Debug_log_write(std::wstring_view category, std::wstring_view message) noexcept {
    try {
        std::lock_guard<std::mutex> const lock(Debug_log_mutex());
        std::wstring const line = Build_log_line(category, message);
        std::string const utf8_line = Utf8_from_wide(line);
        if (utf8_line.empty() || utf8_line.size() > std::numeric_limits<DWORD>::max()) {
            return;
        }

        HANDLE const file = CreateFileW(Debug_log_path().c_str(), FILE_APPEND_DATA,
                                        kAppendFileShareMask, nullptr, OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD written = 0;
        (void)WriteFile(file, utf8_line.data(), static_cast<DWORD>(utf8_line.size()),
                        &written, nullptr);
        CloseHandle(file);
    } catch (...) {
    }
}

void Install_debug_crash_logging() noexcept {
    try {
        SetUnhandledExceptionFilter(Debug_unhandled_exception_filter);
        Debug_log_write(L"crash", L"Installed unhandled exception logger");
    } catch (...) {
    }
}

std::wstring Format_rect_for_debug_log(core::RectPx rect) {
    rect = rect.Normalized();
    std::wstring text = L"[";
    text += std::to_wstring(rect.left);
    text += L",";
    text += std::to_wstring(rect.top);
    text += L" -> ";
    text += std::to_wstring(rect.right);
    text += L",";
    text += std::to_wstring(rect.bottom);
    text += L"]";
    return text;
}

std::wstring Format_hwnd_for_debug_log(HWND hwnd) {
    return std::to_wstring(reinterpret_cast<uintptr_t>(hwnd));
}

} // namespace greenflame

#endif
