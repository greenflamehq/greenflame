#include "win/startup_launch.h"

namespace {

constexpr wchar_t kStartupRunKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupValueName[] = L"Greenflame";
constexpr DWORD kMaxExecutablePathChars = 32767;

struct RegistryKeyGuard {
    HKEY key = nullptr;

    RegistryKeyGuard() = default;
    RegistryKeyGuard(RegistryKeyGuard const &) = delete;
    RegistryKeyGuard &operator=(RegistryKeyGuard const &) = delete;
    ~RegistryKeyGuard() {
        if (key != nullptr) {
            RegCloseKey(key);
        }
    }
};

[[nodiscard]] std::wstring Get_current_executable_path() {
    std::wstring executable_path(MAX_PATH, L'\0');
    for (;;) {
        DWORD const capacity = static_cast<DWORD>(executable_path.size());
        DWORD const copied =
            GetModuleFileNameW(nullptr, executable_path.data(), capacity);
        if (copied == 0) {
            return {};
        }
        if (copied < capacity - 1) {
            executable_path.resize(static_cast<size_t>(copied));
            return executable_path;
        }
        if (capacity >= kMaxExecutablePathChars) {
            return {};
        }
        DWORD const next_capacity = (std::min)(capacity * 2, kMaxExecutablePathChars);
        executable_path.resize(next_capacity);
    }
}

[[nodiscard]] std::wstring Build_startup_command() {
    std::wstring const executable_path = Get_current_executable_path();
    if (executable_path.empty()) {
        return {};
    }
    return L"\"" + executable_path + L"\"";
}

[[nodiscard]] bool Read_startup_value(std::wstring &value) {
    value.clear();
    DWORD value_bytes = 0;
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, kStartupRunKeyPath, kStartupValueName,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &value_bytes);
    if (status != ERROR_SUCCESS || value_bytes < sizeof(wchar_t)) {
        return false;
    }

    size_t const character_count = value_bytes / sizeof(wchar_t);
    value.resize(character_count);
    status = RegGetValueW(HKEY_CURRENT_USER, kStartupRunKeyPath, kStartupValueName,
                          RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value.data(),
                          &value_bytes);
    if (status != ERROR_SUCCESS || value_bytes < sizeof(wchar_t)) {
        value.clear();
        return false;
    }

    size_t const copied_character_count = value_bytes / sizeof(wchar_t);
    if (copied_character_count == 0) {
        value.clear();
        return false;
    }
    value.resize(copied_character_count);
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return !value.empty();
}

[[nodiscard]] std::wstring
Parse_startup_command_executable(std::wstring_view command_line) {
    if (command_line.empty()) {
        return {};
    }
    std::wstring command_text(command_line);
    int argument_count = 0;
    LPWSTR *arguments = CommandLineToArgvW(command_text.c_str(), &argument_count);
    if (arguments == nullptr || argument_count <= 0) {
        if (arguments != nullptr) {
            LocalFree(arguments);
        }
        return {};
    }

    std::wstring executable_path(arguments[0]);
    LocalFree(arguments);
    return executable_path;
}

[[nodiscard]] bool Equals_case_insensitive(std::wstring_view lhs,
                                           std::wstring_view rhs) {
    std::wstring lhs_text(lhs);
    std::wstring rhs_text(rhs);
    return CompareStringOrdinal(lhs_text.c_str(), -1, rhs_text.c_str(), -1, TRUE) ==
           CSTR_EQUAL;
}

} // namespace

namespace greenflame {

bool Is_startup_launch_enabled() {
    std::wstring command_line;
    if (!Read_startup_value(command_line)) {
        return false;
    }
    std::wstring const configured_path = Parse_startup_command_executable(command_line);
    if (configured_path.empty()) {
        return false;
    }
    std::wstring const current_path = Get_current_executable_path();
    if (current_path.empty()) {
        return false;
    }
    return Equals_case_insensitive(configured_path, current_path);
}

bool Set_startup_launch_enabled(bool enabled) {
    if (enabled) {
        std::wstring const startup_command = Build_startup_command();
        if (startup_command.empty()) {
            return false;
        }
        RegistryKeyGuard run_key{};
        DWORD disposition = 0;
        LSTATUS const create_status = RegCreateKeyExW(
            HKEY_CURRENT_USER, kStartupRunKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &run_key.key, &disposition);
        (void)disposition;
        if (create_status != ERROR_SUCCESS || run_key.key == nullptr) {
            return false;
        }
        DWORD const startup_command_bytes =
            static_cast<DWORD>((startup_command.size() + 1) * sizeof(wchar_t));
        LSTATUS const set_status =
            RegSetValueExW(run_key.key, kStartupValueName, 0, REG_SZ,
                           reinterpret_cast<BYTE const *>(startup_command.c_str()),
                           startup_command_bytes);
        return set_status == ERROR_SUCCESS;
    }

    RegistryKeyGuard run_key{};
    LSTATUS const open_status = RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRunKeyPath, 0,
                                              KEY_SET_VALUE, &run_key.key);
    if (open_status == ERROR_FILE_NOT_FOUND || open_status == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    if (open_status != ERROR_SUCCESS || run_key.key == nullptr) {
        return false;
    }
    LSTATUS const delete_status = RegDeleteValueW(run_key.key, kStartupValueName);
    return delete_status == ERROR_SUCCESS || delete_status == ERROR_FILE_NOT_FOUND;
}

} // namespace greenflame
