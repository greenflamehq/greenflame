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
    cli_options_tests.cpp
    overlay_controller_tests.cpp
)
target_compile_definitions(greenflame_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)
set_target_properties(greenflame_tests PROPERTIES
    WIN32_EXECUTABLE FALSE  # console app (main), not GUI (WinMain)
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_precompile_headers(greenflame_tests PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)
target_link_libraries(greenflame_tests PRIVATE
    greenflame_core
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(greenflame_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_append:$<TARGET_RUNTIME_DLL_DIRS:greenflame_tests>"
)
