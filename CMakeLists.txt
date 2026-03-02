cmake_minimum_required(VERSION 3.26)
project(greenflame VERSION 0.2.0 LANGUAGES CXX)

# So the IDE (and clangd) can find includes for all targets, including tests.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# cmake-scripts (pulled by Catch2/CPM) sets CMAKE_COMPILE_WARNING_AS_ERROR=ON
# which adds /WX globally, breaking builds on warnings in third-party headers.
# We control warnings per-target already, so disable it.
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

# Keep the app as a GUI subsystem executable on Windows.
set(CMAKE_WIN32_EXECUTABLE ON)

# -----------------------------
# Version and metadata (single source of truth)
# -----------------------------
set(GREENFLAME_VERSION_TWEAK 0 CACHE STRING "Fourth version component for Windows resources")
set(GREENFLAME_FILE_VERSION_COMMA
    "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${GREENFLAME_VERSION_TWEAK}"
)
set(GREENFLAME_FILE_VERSION_DOTTED
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${GREENFLAME_VERSION_TWEAK}"
)

set(GREENFLAME_COMPANY_NAME "Jocelyn Legault" CACHE STRING "Company name in VERSIONINFO")
set(GREENFLAME_PRODUCT_NAME "Greenflame" CACHE STRING "Product name in VERSIONINFO")
set(GREENFLAME_FILE_DESCRIPTION "Greenflame Screenshot Tool" CACHE STRING "File description in VERSIONINFO")
set(GREENFLAME_COPYRIGHT "Copyright (C) 2026 Jocelyn Legault" CACHE STRING "Copyright in VERSIONINFO")
set(GREENFLAME_INTERNAL_NAME "greenflame.exe" CACHE STRING "InternalName in VERSIONINFO")
set(GREENFLAME_ORIGINAL_FILENAME "greenflame.exe" CACHE STRING "OriginalFilename in VERSIONINFO")
set(GREENFLAME_MANIFEST_NAME "greenflame" CACHE STRING "Manifest assemblyIdentity name")
set(GREENFLAME_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/greenflame.ico")
file(TO_CMAKE_PATH "${GREENFLAME_ICON_PATH}" GREENFLAME_ICON_PATH)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app.manifest"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/greenflame.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/greenflame.rc"
    @ONLY
)

# -----------------------------
# greenflame_core (testable library)
# -----------------------------
add_library(greenflame_core STATIC
    src/greenflame_core/rect_px.cpp
    src/greenflame_core/rect_px.h
    src/greenflame_core/cli_options.cpp
    src/greenflame_core/cli_options.h
    src/greenflame_core/app_controller.cpp
    src/greenflame_core/app_controller.h
    src/greenflame_core/app_config.cpp
    src/greenflame_core/app_config.h
    src/greenflame_core/app_services.h
    src/greenflame_core/process_exit_code.h
    src/greenflame_core/monitor_rules.cpp
    src/greenflame_core/monitor_rules.h
    src/greenflame_core/pixel_ops.cpp
    src/greenflame_core/pixel_ops.h
    src/greenflame_core/bmp.cpp
    src/greenflame_core/bmp.h
    src/greenflame_core/selection_handles.cpp
    src/greenflame_core/selection_handles.h
    src/greenflame_core/snap_to_edges.cpp
    src/greenflame_core/snap_to_edges.h
    src/greenflame_core/snap_edge_builder.cpp
    src/greenflame_core/snap_edge_builder.h
    src/greenflame_core/save_image_policy.cpp
    src/greenflame_core/save_image_policy.h
    src/greenflame_core/string_utils.cpp
    src/greenflame_core/string_utils.h
    src/greenflame_core/window_filter.cpp
    src/greenflame_core/window_filter.h
    src/greenflame_core/window_query.h
    src/greenflame_core/output_path.cpp
    src/greenflame_core/output_path.h
    src/greenflame_core/overlay_controller.cpp
    src/greenflame_core/overlay_controller.h
)

# Allow includes like: #include "greenflame_core/rect_px.h"
target_include_directories(greenflame_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(greenflame_core PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_precompile_headers(greenflame_core PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/greenflame_core/pch.h>"
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(greenflame_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-pre-c++14-compat
        -Wno-pre-c++17-compat
        -Wno-pre-c++20-compat
        -Wno-implicit-int-float-conversion
        -Wno-switch-default
        -Wno-language-extension-token
        -ftime-trace
    )
elseif(MSVC)
    target_compile_options(greenflame_core PRIVATE
        /Wall
        /wd4355 # 'this': used in base member initializer list
        /wd4514 # 'function' : unreferenced inline function has been removed
        /wd4710 # 'function' : function not inlined
        /wd4711 # 'function' : function selected for automatic inline expansion
        /wd4820 # 'bytes' bytes padding added after construct 'member_name'
        /wd5045 # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
        /external:W0
        /external:anglebrackets
        /permissive-
        /Zc:__cplusplus
    )
else()
    target_compile_options(greenflame_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -----------------------------
# Tests
# -----------------------------
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

# -----------------------------
# greenflame (Win32 GUI app)
# -----------------------------
add_executable(greenflame WIN32
    src/greenflame/main.cpp
    src/greenflame/greenflame_app.cpp
    src/greenflame/greenflame_app.h
    src/greenflame/app_config_store.cpp
    src/greenflame/app_config_store.h
    src/greenflame/win/display_queries.cpp
    src/greenflame/win/display_queries.h
    src/greenflame/win/gdi_capture.cpp
    src/greenflame/win/gdi_capture.h
    src/greenflame/win/save_image.cpp
    src/greenflame/win/overlay_paint.cpp
    src/greenflame/win/overlay_paint.h
    src/greenflame/win/overlay_window.cpp
    src/greenflame/win/overlay_window.h
    src/greenflame/win/tray_window.cpp
    src/greenflame/win/tray_window.h
    src/greenflame/win/window_query.cpp
    src/greenflame/win/window_query.h
    src/greenflame/win/win32_services.cpp
    src/greenflame/win/win32_services.h
    ${CMAKE_CURRENT_BINARY_DIR}/app.manifest
    ${CMAKE_CURRENT_BINARY_DIR}/greenflame.rc
)

target_include_directories(greenflame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/greenflame
)

target_compile_definitions(greenflame PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
)

target_precompile_headers(greenflame PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/greenflame/pch.h>"
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(greenflame PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-pre-c++14-compat
        -Wno-pre-c++17-compat
        -Wno-pre-c++20-compat
        -Wno-implicit-int-float-conversion
        -Wno-switch-default
        -Wno-language-extension-token
        -ftime-trace
    )
elseif(MSVC)
    target_compile_options(greenflame PRIVATE
        /Wall
        /wd4355 # 'this': used in base member initializer list
        /wd4514 # 'function' : unreferenced inline function has been removed
        /wd4710 # 'function' : function not inlined
        /wd4711 # 'function' : function selected for automatic inline expansion
        /wd4820 # 'bytes' bytes padding added after construct 'member_name'
        /wd5045 # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
        /external:W0
        /external:anglebrackets
        /permissive-
        /Zc:__cplusplus
    )
else()
    target_compile_options(greenflame PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(greenflame PRIVATE
    greenflame_core

        # Win32 basics
        user32
        gdi32
        shcore
        shell32
        dwmapi
        comdlg32
        Windowscodecs
        ole32
        gdiplus
)

# -----------------------------
# Link-Time Optimization (release builds only)
# -----------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Full LTO: TUs emit LLVM bitcode; lld-link merges and optimises at link time.
    foreach(_lto_tgt greenflame_core greenflame)
        target_compile_options(${_lto_tgt} PRIVATE $<$<CONFIG:Release>:-flto=full>)
    endforeach()
    target_link_options(greenflame PRIVATE $<$<CONFIG:Release>:-flto=full>)
elseif(MSVC)
    # Whole Program Optimisation: /GL at compile time, /LTCG at link time.
    set_property(TARGET greenflame_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET greenflame      PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
