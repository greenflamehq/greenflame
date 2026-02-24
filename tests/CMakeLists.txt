cmake_minimum_required(VERSION 3.26)

# Catch2: fetch at configure time (requires network on first run or when cache is missing)
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

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
)
set_target_properties(greenflame_tests PROPERTIES WIN32_EXECUTABLE FALSE)  # console app (main), not GUI (WinMain)
target_precompile_headers(greenflame_tests PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)
target_link_libraries(greenflame_tests PRIVATE
    greenflame_core
    Catch2::Catch2WithMain
)
add_test(NAME greenflame_tests COMMAND greenflame_tests)
set_tests_properties(greenflame_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_append:$<TARGET_RUNTIME_DLL_DIRS:greenflame_tests>"
)
