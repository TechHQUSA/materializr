# Coverage-guided libFuzzer harnesses over the file-format parsers and
# Settings. Only reached when the root CMakeLists' BUILD_FUZZERS option is on
# (Clang-only — see the guard there). Each target defines LLVMFuzzerTestOneInput
# (and, where noted, LLVMFuzzerInitialize) with no main() — the
# -fsanitize=fuzzer link flag supplies libFuzzer's own driver.

# Same OCCT 7.8 STEP/IGES toolkit rename as the STL toolkit gate above
# (tests/CMakeLists.txt) — TKSTEP/TKIGES pre-7.8, TKDESTEP/TKDEIGES from 7.8 on.
if(OpenCASCADE_MAJOR_VERSION EQUAL 7 AND OpenCASCADE_MINOR_VERSION LESS 8)
    set(FUZZ_STEP_LIB TKSTEP)
    set(FUZZ_IGES_LIB TKIGES)
    set(FUZZ_STL_LIB TKSTL)
else()
    set(FUZZ_STEP_LIB TKDESTEP)
    set(FUZZ_IGES_LIB TKDEIGES)
    set(FUZZ_STL_LIB TKDESTL)
endif()

# Shared sanitizer/driver flags for every fuzz target. -fno-sanitize-recover=all
# makes UBSan abort on first UB instead of logging and continuing — under
# libFuzzer a non-aborting UB hit produces no saved crash input.
function(add_fuzz_target NAME)
    cmake_parse_arguments(FZ "" "" "EXTRA_SOURCES;EXTRA_LIBS;DEFS" ${ARGN})
    add_executable(${NAME} ${NAME}.cpp ${FZ_EXTRA_SOURCES})
    target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(${NAME} PRIVATE
        -g -O1 -fno-omit-frame-pointer
        -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(${NAME} PRIVATE
        -fsanitize=fuzzer,address,undefined)
    target_link_libraries(${NAME} PRIVATE materializr_core ${FZ_EXTRA_LIBS})
    if(FZ_DEFS)
        target_compile_definitions(${NAME} PRIVATE ${FZ_DEFS})
    endif()
endfunction()

add_fuzz_target(fuzz_projectio_load)
add_fuzz_target(fuzz_projectio_sketchbody)

add_fuzz_target(fuzz_svgimport
    EXTRA_SOURCES ${CMAKE_SOURCE_DIR}/src/modeling/SvgImport.cpp
                  ${CMAKE_SOURCE_DIR}/src/io/SvgExport.cpp
    EXTRA_LIBS TKV3d TKService)

# OCC_CONVERT_SIGNALS must be passed explicitly — materializr_core doesn't
# carry it (only the app target does, see root CMakeLists), and without it
# OCC_CATCH_SIGNALS in StepIO.cpp/IgesIO.cpp expands to nothing, making the
# entire point of these two harnesses (validating the signal-conversion path)
# moot. Also see each harness's ASAN_OPTIONS note for the other half of that.
add_fuzz_target(fuzz_stepio
    EXTRA_SOURCES ${CMAKE_SOURCE_DIR}/src/io/StepIO.cpp
    EXTRA_LIBS TKXSBase ${FUZZ_STEP_LIB}
    DEFS OCC_CONVERT_SIGNALS)

add_fuzz_target(fuzz_igesio
    EXTRA_SOURCES ${CMAKE_SOURCE_DIR}/src/io/IgesIO.cpp
    EXTRA_LIBS TKXSBase ${FUZZ_IGES_LIB}
    DEFS OCC_CONVERT_SIGNALS)

add_fuzz_target(fuzz_settings_roundtrip
    EXTRA_SOURCES ${CMAKE_SOURCE_DIR}/src/io/Settings.cpp)

# DxfImport is already compiled into materializr_core, so no EXTRA_SOURCES —
# but it pulls the OCCT geometry it samples (spline/ellipse) from there too.
add_fuzz_target(fuzz_dxfimport)

# BrepIO / StlIO: same OCC_CONVERT_SIGNALS treatment as StepIO/IgesIO — the .cpp
# is recompiled into the target with the def so OCC_CATCH_SIGNALS actually
# converts a kernel fault (materializr_core's copy is built without it). The
# static-lib copy of each object is simply not extracted once the target
# defines the symbols itself, so there's no duplicate-symbol clash.
add_fuzz_target(fuzz_brepio
    EXTRA_SOURCES ${CMAKE_SOURCE_DIR}/src/io/BrepIO.cpp
    EXTRA_LIBS TKBRep
    DEFS OCC_CONVERT_SIGNALS)

add_fuzz_target(fuzz_stlio
    EXTRA_SOURCES ${CMAKE_SOURCE_DIR}/src/io/StlIO.cpp
    EXTRA_LIBS ${FUZZ_STL_LIB}
    DEFS OCC_CONVERT_SIGNALS)
