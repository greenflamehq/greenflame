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

    # Regression: a short stroke drawn and committed first must leave nothing behind
    # in the next stroke's draft. The next stroke's frame must match the same frame
    # when the short stroke was committed without ever being drawn. The short strokes
    # are long enough to feed the smoother but too short for a final point, so no body
    # is drawn (a drawn body's commit reuse may round its own pixels differently).
    # The brush case runs with --annotated: a committed highlighter rebuilds the
    # annotations through the highlighter path.
    set(greenflame_dab_points_highlighter 7)
    set(greenflame_dab_points_brush 5)
    foreach(greenflame_dab_scenario highlighter brush)
        set(greenflame_dab_args
            --adapter warp --layout single-1080 --scenario ${greenflame_dab_scenario}
            --annotated --points-per-frame 16 --frames 10
            --dab ${greenflame_dab_points_${greenflame_dab_scenario}})
        set(greenflame_dab_bmp
            "${CMAKE_CURRENT_BINARY_DIR}/dab_${greenflame_dab_scenario}.bmp")
        set(greenflame_dab_fresh_bmp
            "${CMAKE_CURRENT_BINARY_DIR}/dab_${greenflame_dab_scenario}_fresh.bmp")
        add_test(
            NAME greenflame_render_bench_dab_${greenflame_dab_scenario}_drawn
            COMMAND greenflame_render_bench ${greenflame_dab_args}
                --dump-frame 3 "${greenflame_dab_bmp}"
        )
        add_test(
            NAME greenflame_render_bench_dab_${greenflame_dab_scenario}_fresh
            COMMAND greenflame_render_bench ${greenflame_dab_args} --dab-no-draft
                --dump-frame 3 "${greenflame_dab_fresh_bmp}"
        )
        set_tests_properties(
            greenflame_render_bench_dab_${greenflame_dab_scenario}_drawn
            greenflame_render_bench_dab_${greenflame_dab_scenario}_fresh
            PROPERTIES FIXTURES_SETUP greenflame_bench_dab_${greenflame_dab_scenario}
        )
        add_test(
            NAME greenflame_render_bench_dab_${greenflame_dab_scenario}_diff
            COMMAND greenflame_render_bench
                --diff "${greenflame_dab_bmp}" "${greenflame_dab_fresh_bmp}"
        )
        set_tests_properties(greenflame_render_bench_dab_${greenflame_dab_scenario}_diff
            PROPERTIES FIXTURES_REQUIRED greenflame_bench_dab_${greenflame_dab_scenario}
        )
    endforeach()

    # A committed highlighter rebuilt from scratch (undo, redo) draws its mask in
    # pieces, as the live draft does. Its frame must match the release frame, which
    # reuses the draft's body, but for a few edge pixels at piece joins (2-3 now). One
    # geometry for the whole stroke (the old way) differs on about 10,000 edge pixels.
    set(greenflame_rebuild_max_pixels 100)
    foreach(greenflame_rebuild_smooth on off)
        set(greenflame_rebuild_bmp
            "${CMAKE_CURRENT_BINARY_DIR}/rebuild_${greenflame_rebuild_smooth}.bmp")
        add_test(
            NAME greenflame_render_bench_rebuild_${greenflame_rebuild_smooth}
            COMMAND greenflame_render_bench
                --adapter warp --layout single-1080 --scenario highlighter --annotated
                --smooth ${greenflame_rebuild_smooth} --points-per-frame 16 --frames 10
                --dump-frame 10 "${greenflame_rebuild_bmp}"
        )
        set_tests_properties(greenflame_render_bench_rebuild_${greenflame_rebuild_smooth}
            PROPERTIES FIXTURES_SETUP greenflame_bench_rebuild_${greenflame_rebuild_smooth}
        )
        add_test(
            NAME greenflame_render_bench_rebuild_diff_${greenflame_rebuild_smooth}
            COMMAND greenflame_render_bench
                --diff "${greenflame_rebuild_bmp}" "${greenflame_rebuild_bmp}.rebuild.bmp"
                --max-pixels ${greenflame_rebuild_max_pixels}
        )
        set_tests_properties(
            greenflame_render_bench_rebuild_diff_${greenflame_rebuild_smooth}
            PROPERTIES
                FIXTURES_REQUIRED greenflame_bench_rebuild_${greenflame_rebuild_smooth}
        )
    endforeach()

    # A line or arrow draft over the frozen frame recomposites only around itself. The
    # frame must match the same frame drawn through the dynamic path. The flat arrow
    # gets one tight rect, so its head shows any reach that falls short.
    foreach(greenflame_line_shape diagonal flat)
        set(greenflame_line_args
            --adapter warp --layout single-1080 --scenario line --annotated --arrow
            --frames 10)
        if(greenflame_line_shape STREQUAL "flat")
            list(APPEND greenflame_line_args --flat)
        endif()
        list(APPEND greenflame_line_args --dump-frame 9)
        set(greenflame_line_bmp
            "${CMAKE_CURRENT_BINARY_DIR}/line_${greenflame_line_shape}_fast.bmp")
        set(greenflame_line_dynamic_bmp
            "${CMAKE_CURRENT_BINARY_DIR}/line_${greenflame_line_shape}_dynamic.bmp")
        add_test(
            NAME greenflame_render_bench_line_${greenflame_line_shape}_fast
            COMMAND greenflame_render_bench ${greenflame_line_args}
                "${greenflame_line_bmp}"
        )
        add_test(
            NAME greenflame_render_bench_line_${greenflame_line_shape}_dynamic
            COMMAND greenflame_render_bench ${greenflame_line_args}
                "${greenflame_line_dynamic_bmp}" --line-dynamic
        )
        set_tests_properties(
            greenflame_render_bench_line_${greenflame_line_shape}_fast
            greenflame_render_bench_line_${greenflame_line_shape}_dynamic
            PROPERTIES FIXTURES_SETUP greenflame_bench_line_${greenflame_line_shape}
        )
        add_test(
            NAME greenflame_render_bench_line_${greenflame_line_shape}_diff
            COMMAND greenflame_render_bench
                --diff "${greenflame_line_bmp}" "${greenflame_line_dynamic_bmp}"
        )
        set_tests_properties(greenflame_render_bench_line_${greenflame_line_shape}_diff
            PROPERTIES FIXTURES_REQUIRED greenflame_bench_line_${greenflame_line_shape}
        )
    endforeach()

    # A highlighter over a lifted window capture tints the capture, not the screenshot
    # under it: frames must match those where the screenshot itself has the capture's
    # pixels. Frame 5 is the live stroke; frame 10 the committed one.
    foreach(greenflame_lifted_frame 5 10)
        set(greenflame_lifted_args
            --adapter warp --layout single-1080 --scenario highlighter --annotated
            --points-per-frame 16 --frames 10 --dump-frame ${greenflame_lifted_frame})
        set(greenflame_lifted_bmp
            "${CMAKE_CURRENT_BINARY_DIR}/lifted_${greenflame_lifted_frame}.bmp")
        set(greenflame_lifted_ref_bmp
            "${CMAKE_CURRENT_BINARY_DIR}/lifted_ref_${greenflame_lifted_frame}.bmp")
        add_test(
            NAME greenflame_render_bench_lifted_${greenflame_lifted_frame}
            COMMAND greenflame_render_bench ${greenflame_lifted_args}
                "${greenflame_lifted_bmp}"
                --lifted
        )
        add_test(
            NAME greenflame_render_bench_lifted_ref_${greenflame_lifted_frame}
            COMMAND greenflame_render_bench ${greenflame_lifted_args}
                "${greenflame_lifted_ref_bmp}" --lifted-as-screenshot
        )
        set_tests_properties(
            greenflame_render_bench_lifted_${greenflame_lifted_frame}
            greenflame_render_bench_lifted_ref_${greenflame_lifted_frame}
            PROPERTIES FIXTURES_SETUP greenflame_bench_lifted_${greenflame_lifted_frame}
        )
        add_test(
            NAME greenflame_render_bench_lifted_diff_${greenflame_lifted_frame}
            COMMAND greenflame_render_bench
                --diff "${greenflame_lifted_bmp}" "${greenflame_lifted_ref_bmp}"
        )
        set_tests_properties(greenflame_render_bench_lifted_diff_${greenflame_lifted_frame}
            PROPERTIES FIXTURES_REQUIRED greenflame_bench_lifted_${greenflame_lifted_frame}
        )
    endforeach()

    # A straightened highlighter bar recomposites in pieces along a long diagonal.
    # The last live frame must show the whole bar: it matches the release frame
    # (the bar committed and drawn as an annotation) up to rounding.
    set(greenflame_straight_args
        --adapter warp --layout single-1080 --scenario highlighter --annotated
        --diagonal --frames 10 --straighten-at 2)
    set(greenflame_straight_live_bmp "${CMAKE_CURRENT_BINARY_DIR}/straight_live.bmp")
    set(greenflame_straight_release_bmp
        "${CMAKE_CURRENT_BINARY_DIR}/straight_release.bmp")
    add_test(
        NAME greenflame_render_bench_straight_live
        COMMAND greenflame_render_bench ${greenflame_straight_args}
            --dump-frame 9 "${greenflame_straight_live_bmp}"
    )
    add_test(
        NAME greenflame_render_bench_straight_release
        COMMAND greenflame_render_bench ${greenflame_straight_args}
            --dump-frame 10 "${greenflame_straight_release_bmp}"
    )
    set_tests_properties(
        greenflame_render_bench_straight_live greenflame_render_bench_straight_release
        PROPERTIES FIXTURES_SETUP greenflame_bench_straight
    )
    add_test(
        NAME greenflame_render_bench_straight_diff
        COMMAND greenflame_render_bench
            --diff "${greenflame_straight_live_bmp}" "${greenflame_straight_release_bmp}"
            --tolerance 2
    )
    set_tests_properties(greenflame_render_bench_straight_diff
        PROPERTIES FIXTURES_REQUIRED greenflame_bench_straight
    )
endif()
