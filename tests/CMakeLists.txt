cmake_minimum_required(VERSION 3.26)

# GoogleTest: fetch at configure time (requires network on first run or when cache is missing)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# Windows: prevent overriding parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(greenflame_tests
    geometry_tests.cpp
    monitor_policy_tests.cpp
    rect_from_points_tests.cpp
    virtual_screen_rect_tests.cpp
    pixel_ops_tests.cpp
    bmp_tests.cpp
    dpi_scale_tests.cpp
    selection_handles_tests.cpp
    snap_to_edges_tests.cpp
    snap_edge_builder_tests.cpp
    save_image_policy_tests.cpp
    string_utils_tests.cpp
    window_filter_tests.cpp
    output_path_tests.cpp
    cli_options_tests.cpp
    app_config_tests.cpp
    annotation_hit_test_tests.cpp
    bubble_annotation_tests.cpp
    annotation_tool_tests.cpp
    annotation_edit_interaction_tests.cpp
    annotation_controller_tests.cpp
    text_draft_buffer_tests.cpp
    color_wheel_tests.cpp
    overlay_controller_tests.cpp
    app_controller_tests.cpp
    undo_stack_tests.cpp
    toolbar_placement_tests.cpp
)
target_compile_definitions(greenflame_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:DEBUG>
)
set_target_properties(greenflame_tests PROPERTIES
    WIN32_EXECUTABLE FALSE  # console app (main), not GUI (WinMain)
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_precompile_headers(greenflame_tests PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(greenflame_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-pre-c++14-compat
        -Wno-pre-c++17-compat
        -Wno-pre-c++20-compat
        -Wno-global-constructors
        -Wno-implicit-int-float-conversion
        -Wno-switch-default
        -Wno-language-extension-token
    )
    if(GREENFLAME_ENABLE_TIME_TRACE)
        target_compile_options(greenflame_tests PRIVATE -ftime-trace)
    endif()
elseif(MSVC)
    target_compile_options(greenflame_tests PRIVATE
        /Wall
        /WX
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
    target_compile_options(greenflame_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_link_libraries(greenflame_tests PRIVATE
    greenflame_core
    GTest::gtest_main
    GTest::gmock
)
include(GoogleTest)
gtest_discover_tests(greenflame_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_append:$<TARGET_RUNTIME_DLL_DIRS:greenflame_tests>"
)
