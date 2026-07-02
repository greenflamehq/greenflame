if(NOT DEFINED GREENFLAME_CMAKELISTS)
    message(FATAL_ERROR "GREENFLAME_CMAKELISTS is required")
endif()
if(NOT DEFINED GREENFLAME_INSTALLER_SCRIPT)
    message(FATAL_ERROR "GREENFLAME_INSTALLER_SCRIPT is required")
endif()
if(NOT DEFINED GREENFLAME_PATH_HELPER_SCRIPT)
    message(FATAL_ERROR "GREENFLAME_PATH_HELPER_SCRIPT is required")
endif()

function(require_file path description)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing ${description}: ${path}")
    endif()
endfunction()

function(require_contains text needle description)
    string(FIND "${text}" "${needle}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Expected ${description}: ${needle}")
    endif()
endfunction()

function(require_not_contains text needle description)
    string(FIND "${text}" "${needle}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR "Unexpected ${description}: ${needle}")
    endif()
endfunction()

require_file("${GREENFLAME_CMAKELISTS}" "CMakeLists.txt")
require_file("${GREENFLAME_INSTALLER_SCRIPT}" "NSIS installer script")
require_file("${GREENFLAME_PATH_HELPER_SCRIPT}" "PATH helper script")

file(READ "${GREENFLAME_CMAKELISTS}" cmakelists_text)
file(READ "${GREENFLAME_INSTALLER_SCRIPT}" installer_text)
file(READ "${GREENFLAME_PATH_HELPER_SCRIPT}" path_helper_text)

require_contains("${cmakelists_text}" "find_program(MAKENSIS_EXECUTABLE"
                 "makensis discovery")
require_contains("${cmakelists_text}" "if(MAKENSIS_EXECUTABLE)"
                 "installer target guarded by makensis discovery")
require_contains("${cmakelists_text}"
                 "NSIS makensis.exe is required to build the installer target."
                 "installer-only makensis failure")
require_contains("${cmakelists_text}"
                 [=[greenflame-${GREENFLAME_PRODUCT_VERSION}-win-x64.exe]=]
                 "versioned Windows x64 installer filename")
require_contains("${cmakelists_text}" "add_custom_target(installer"
                 "installer build target")
require_not_contains("${cmakelists_text}" "greenflame_setup"
                     "custom MSI bootstrapper target")
require_not_contains("${cmakelists_text}" ".wixproj" "WiX project packaging")
require_not_contains("${cmakelists_text}" ".msi" "MSI packaging")

require_contains("${installer_text}" "Name \"greenflame\"" "lowercase product name")
require_contains("${installer_text}" "RequestExecutionLevel highest"
                 "global-install capable execution level")
require_contains("${installer_text}" "!insertmacro MUI_PAGE_LICENSE"
                 "license approval page")
require_contains("${installer_text}" "Page custom InstallModePageCreate InstallModePageLeave"
                 "global-or-user install choice page")
require_contains("${installer_text}" "!insertmacro MUI_PAGE_DIRECTORY"
                 "install path page")
require_contains("${installer_text}" "Page custom StartupPageCreate StartupPageLeave"
                 "startup preference page")
require_contains("${installer_text}" "!define MUI_FINISHPAGE_RUN"
                 "start greenflame now finish option")
require_contains("${installer_text}" "Add greenflame to PATH for command-line use"
                 "PATH opt-in checkbox")
require_contains("${installer_text}" "New terminal windows will use the updated PATH. Already-open terminals must be reopened."
                 "PATH terminal restart note")
require_contains("${path_helper_text}" "PathAddedByInstaller"
                 "PATH ownership tracking")
require_contains("${path_helper_text}" "Set-PathOwnership $true $Entry"
                 "installed PATH entry tracking")
require_contains("${installer_text}" "Function un.RemoveInstallerPathEntries"
                 "PATH cleanup during uninstall")
require_contains("${installer_text}"
                 [=[SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment"]=]
                 "environment change broadcast")
require_contains("${installer_text}" "greenflame-path.ps1"
                 "bundled PATH helper")
require_contains("${path_helper_text}" "HKCU:\\Environment"
                 "current-user PATH key")
require_contains("${path_helper_text}" "HKLM:\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
                 "machine PATH key")
require_contains("${path_helper_text}" "New-ItemProperty -Path $env_key -Name Path"
                 "PATH registry update")
require_contains("${path_helper_text}" "Remove-ItemProperty -Path $uninstall_key -Name PathEntry"
                 "PATH ownership cleanup")
require_contains("${installer_text}" "WriteUninstaller"
                 "uninstaller generation")
require_contains("${installer_text}" "WriteRegStr HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Run\" \"Greenflame\""
                 "current-user startup Run value")
require_contains("${installer_text}" "DeleteRegValue HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Run\" \"Greenflame\""
                 "current-user startup cleanup")
require_contains("${installer_text}" "DeleteRegValue HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Run\" \"Greenflame\""
                 "machine startup cleanup")
require_contains("${installer_text}" "Function un.CloseRunningGreenflame"
                 "running app close before uninstall file removal")
require_contains("${installer_text}" "FindWindow $0 \"GreenflameTray\""
                 "tray window close request during uninstall")
require_contains("${installer_text}" "taskkill.exe"
                 "forced running app termination fallback during uninstall")
require_contains("${installer_text}" "Delete /REBOOTOK \"$INSTDIR\\greenflame.exe\""
                 "reboot-safe executable deletion during uninstall")
