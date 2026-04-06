#pragma once

namespace greenflame {

#if defined(GREENFLAME_LOG)
[[nodiscard]] std::wstring const &Debug_log_path() noexcept;
void Debug_log_write(std::wstring_view category, std::wstring_view message) noexcept;
void Install_debug_crash_logging() noexcept;
namespace core {
struct RectPx;
}
[[nodiscard]] std::wstring Format_rect_for_debug_log(core::RectPx rect);
[[nodiscard]] std::wstring Format_hwnd_for_debug_log(HWND hwnd);
#define GREENFLAME_LOG_WRITE(category, message_expression)                             \
    do {                                                                               \
        ::greenflame::Debug_log_write((category), (message_expression));               \
    } while (false)
#define GREENFLAME_INSTALL_DEBUG_CRASH_LOGGING()                                       \
    do {                                                                               \
        ::greenflame::Install_debug_crash_logging();                                   \
    } while (false)
#else
[[nodiscard]] inline std::wstring const &Debug_log_path() noexcept {
    static std::wstring const *const kPath = new std::wstring();
    return *kPath;
}
inline void Debug_log_write(std::wstring_view, std::wstring_view) noexcept {}
inline void Install_debug_crash_logging() noexcept {}
#define GREENFLAME_LOG_WRITE(category, message_expression)                             \
    do {                                                                               \
    } while (false)
#define GREENFLAME_INSTALL_DEBUG_CRASH_LOGGING()                                       \
    do {                                                                               \
    } while (false)
#endif

} // namespace greenflame
