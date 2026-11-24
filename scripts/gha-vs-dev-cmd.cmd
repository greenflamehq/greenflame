@echo off
set "GREENFLAME_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%GREENFLAME_VSWHERE%" (
    echo Could not find vswhere.exe at "%GREENFLAME_VSWHERE%".
    exit /b 1
)

set "GREENFLAME_VS_INSTALL="
for /f "usebackq tokens=*" %%i in (`"%GREENFLAME_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "GREENFLAME_VS_INSTALL=%%i"
)

if not defined GREENFLAME_VS_INSTALL (
    echo Could not find Visual Studio with the C++ x64/x86 tools.
    exit /b 1
)

if not exist "%GREENFLAME_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" (
    echo Could not find VsDevCmd.bat under "%GREENFLAME_VS_INSTALL%".
    exit /b 1
)

call "%GREENFLAME_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

rem Pass "llvm" to put the VS-bundled LLVM (clang-cl, clang-format, clang-tidy)
rem ahead of the runner's standalone LLVM, which is older than the Clang the MSVC
rem STL requires. Not done by default: with LLVM first on PATH, CMake picks clang++
rem for presets that name no compiler (the MSVC ones).
if /i not "%~1"=="llvm" exit /b 0
set "GREENFLAME_VS_LLVM=%GREENFLAME_VS_INSTALL%\VC\Tools\Llvm\x64\bin"
if not exist "%GREENFLAME_VS_LLVM%\clang-cl.exe" (
    echo Could not find the VS-bundled clang-cl under "%GREENFLAME_VS_LLVM%".
    exit /b 1
)
set "PATH=%GREENFLAME_VS_LLVM%;%PATH%"
exit /b 0
