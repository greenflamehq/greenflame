# greenflame_render_bench: offscreen overlay render benchmark and present-floor
# probe. See docs/testing.md "Overlay performance".
add_executable(greenflame_render_bench
    pch.h
    render_bench.cpp
)
set_target_properties(greenflame_render_bench PROPERTIES
    WIN32_EXECUTABLE FALSE  # console app (main), not GUI (WinMain)
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_compile_definitions(greenflame_render_bench PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:DEBUG>
)
target_precompile_headers(greenflame_render_bench PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)
greenflame_set_warnings(greenflame_render_bench)
target_link_libraries(greenflame_render_bench PRIVATE
    greenflame_render
    user32
    gdi32
    ole32
    winmm
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(greenflame_render_bench PRIVATE $<$<CONFIG:Release>:-flto=full>)
    target_link_options(greenflame_render_bench PRIVATE $<$<CONFIG:Release>:-flto=full>)
elseif(MSVC)
    set_property(TARGET greenflame_render_bench
                 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(BUILD_TESTING)
    # WARP smoke run: every scenario renders, the readback path writes a BMP, and
    # --diff reads it back. No timing assertions; WARP runs headless on CI runners.
    set(greenflame_bench_smoke_bmp "${CMAKE_CURRENT_BINARY_DIR}/smoke_select.bmp")
    add_test(
        NAME greenflame_render_bench_smoke
        COMMAND greenflame_render_bench
            --adapter warp --layout single-1080 --scenario all --frames 10
            --dump-frame 5 "${greenflame_bench_smoke_bmp}"
    )
    set_tests_properties(greenflame_render_bench_smoke PROPERTIES
        FIXTURES_SETUP greenflame_bench_smoke
    )
    add_test(
        NAME greenflame_render_bench_diff_smoke
        COMMAND greenflame_render_bench
            --diff "${greenflame_bench_smoke_bmp}" "${greenflame_bench_smoke_bmp}"
    )
    set_tests_properties(greenflame_render_bench_diff_smoke PROPERTIES
        FIXTURES_REQUIRED greenflame_bench_smoke
    )
endif()
