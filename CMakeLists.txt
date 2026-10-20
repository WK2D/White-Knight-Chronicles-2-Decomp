cmake_minimum_required(VERSION 3.20)
project(WKC2Native
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "White Knight Chronicles II native C/C++ recompilation research"
)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer Ninja if available
if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{CMAKE_GENERATOR})
    message(STATUS "Tip: use -G Ninja for faster builds")
endif()

# ---------------------------------------------------------------------------
# The two directories this project is built from
# ---------------------------------------------------------------------------
# PS3RECOMP_DIR is the toolkit checkout: include/, runtime/, libs/, tools/.
# RECOMP_DIR is where the lifter's output for THIS game lives. They are
# separate on purpose -- a port keeps its generated code in its own tree and
# points at whatever toolkit checkout it is building against:
#
#   cmake -B build -G Ninja \
#         -DPS3RECOMP_DIR=/path/to/ps3recomp \
#         -DRECOMP_DIR=/path/to/wkc2-generated/ppu
#
if(NOT DEFINED PS3RECOMP_DIR)
    # Legacy template fallback. tools/build.ps1 passes the pinned SDK path.
    get_filename_component(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
endif()
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)

set(RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recompiled" CACHE PATH
    "Directory holding the lifter's output: ppu_recomp.h and the lifted sources")
get_filename_component(RECOMP_DIR "${RECOMP_DIR}" ABSOLUTE)

message(STATUS "ps3recomp SDK:   ${PS3RECOMP_DIR}")
message(STATUS "Lifted output:   ${RECOMP_DIR}")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR
        "No ppu_recomp.h in ${RECOMP_DIR}.\n"
        "That header and the lifted sources beside it are what "
        "tools/ppu_lifter.py writes. Run the lifter, drop its output there, "
        "or point -DRECOMP_DIR= at wherever it already is.")
endif()

# The lifted translation units. A game's lift is hundreds of generated files,
# so they are globbed rather than listed; re-run CMake after a fresh lift.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_DIR}/*.c"
    "${RECOMP_DIR}/*.cpp"
)
if(NOT RECOMP_SOURCES)
    message(FATAL_ERROR
        "${RECOMP_DIR} has a ppu_recomp.h but no .c or .cpp beside it. The "
        "lifted functions and the function table live in those; without them "
        "there is nothing to run.")
endif()
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")

# ---------------------------------------------------------------------------
# ps3recomp runtime library
# ---------------------------------------------------------------------------
# Built from source as a subdirectory rather than imported as a prebuilt
# archive. A static library carries none of its own link requirements, so an
# imported one arrives without SDL2, pthreads, the Metal frameworks or the
# Winsock/XInput imports -- the link then fails on every platform for a
# different reason. add_subdirectory hands all of that over with the target.
if(WIN32 AND DEFINED DIRECTX_HEADERS_DIR)
    include_directories(BEFORE SYSTEM "${DIRECTX_HEADERS_DIR}/include/directx")
endif()
option(PS3RECOMP_RSX_VULKAN "Use the shared Vulkan renderer" ${WIN32})
if(WIN32 AND PS3RECOMP_RSX_VULKAN)
    find_package(glslang CONFIG REQUIRED)
endif()
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)
if(WIN32 AND PS3RECOMP_RSX_VULKAN)
    if(NOT TARGET glslang::glslang OR NOT TARGET glslang::SPIRV)
        message(FATAL_ERROR "WKC Vulkan requires glslang with SPIR-V support. Run tools/prepare-vulkan.ps1 first.")
    endif()
endif()

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses.
if(EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    file(GLOB_RECURSE HLE_MODULE_SOURCES CONFIGURE_DEPENDS
        "${PS3RECOMP_DIR}/libs/*.c")
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
                ${HLE_MODULE_SOURCES}
        COMMENT "Generating the HLE NID -> handler table")
    message(STATUS "HLE NID table:   generated (every module)")
endif()

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
# The PPU boot scaffold is compiled here rather than taken from the runtime
# library: every one of these files #includes the lifter's per-game
# ppu_recomp.h, so they can only be built against a particular lift.
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

# ---------------------------------------------------------------------------
# Lifted SPU images (optional)
# ---------------------------------------------------------------------------
# A title that runs SPU work gets its images lifted into <SPU_DIR>/<image>/ by
# tools/spu_lifter.py, with a registration TU generated by
# tools/build_spu_workloads.py. cellSpurs dispatches them by content
# fingerprint, so the generated table is keyed to the images THIS build ships:
# a table generated from another release registers signatures that can never
# match, which is worse than shipping none. Both the table and the job-module
# registry self-register from constructors and are therefore optional -- a tree
# that has not lifted its SPU images yet configures and links without them.
set(SPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spu" CACHE PATH
    "Directory holding the lifted SPU images (<SPU_DIR>/<image>/spu_recomp.c)")
option(PORT_ENABLE_SPU "Compile and register the title's lifted SPU images" ON)

set(SPU_SOURCES "")
if(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(SPU_LIFTED)
        list(LENGTH SPU_LIFTED SPU_IMAGE_COUNT)
        message(STATUS "Lifted SPU:      ${SPU_IMAGE_COUNT} image(s)")
        # The musttail indirect-branch fast path. Excluded from the runtime
        # library on purpose (see the SDK root CMakeLists): it must be built
        # here, by clang, or the tail call degrades to a plain call and a guest
        # loop that branches indirectly walks the thread stack off its guard
        # page a few thousand iterations in.
        list(APPEND SPU_SOURCES ${SPU_LIFTED}
             "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
        # Generated beside the lift; each is present only once its generator
        # has been run, so each is included only if it is actually here.
        foreach(gen IN ITEMS spu_workloads.c jobmods_register.c taskset_policy_bytes.c)
            if(EXISTS "${SPU_DIR}/${gen}")
                list(APPEND SPU_SOURCES "${SPU_DIR}/${gen}")
            else()
                message(STATUS "  (no ${gen} -- not registered)")
            endif()
        endforeach()
    else()
        message(STATUS "Lifted SPU:      none found in ${SPU_DIR}")
    endif()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE PLAY_ENV_SDK_SOURCES CONFIGURE_DEPENDS
    "${PS3RECOMP_DIR}/runtime/*.c" "${PS3RECOMP_DIR}/runtime/*.cpp"
    "${PS3RECOMP_DIR}/runtime/*.h" "${PS3RECOMP_DIR}/runtime/*.hpp"
    "${PS3RECOMP_DIR}/libs/*.c" "${PS3RECOMP_DIR}/libs/*.cpp"
    "${PS3RECOMP_DIR}/libs/*.h" "${PS3RECOMP_DIR}/libs/*.hpp")
file(GLOB PLAY_ENV_PROJECT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
set(PLAY_ENV_NAMES "${CMAKE_CURRENT_BINARY_DIR}/play_environment_names.h")
add_custom_command(OUTPUT "${PLAY_ENV_NAMES}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_play_environment.py"
        --sdk "${PS3RECOMP_DIR}" --project "${CMAKE_CURRENT_SOURCE_DIR}" --out "${PLAY_ENV_NAMES}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_play_environment.py" ${PLAY_ENV_SDK_SOURCES} ${PLAY_ENV_PROJECT_SOURCES}
    COMMENT "Generating clean-play environment guard")
set_source_files_properties(play_environment.cpp PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}")
add_executable(${PROJECT_NAME}
    main.cpp
    graphics_settings.cpp
    graphics_menu.cpp
    graphics_timing.cpp
    graphics_readback.cpp
    stubs.cpp
    lwcond.cpp
    lfqueue.cpp
    pamf.cpp
    dmux.cpp
    vpost.cpp
    vdec.cpp
    adec.cpp
    osk.cpp
    l10n.cpp
    thread_diagnostics.cpp
    graphics_diagnostics.cpp
    spu_job_diagnostics.cpp
    native_skinning_bridge.cpp
    native_skinning_stream_bridge.cpp
    play_environment.cpp
    ${PLAY_ENV_NAMES}
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE osk_win32.cpp)
    target_link_libraries(${PROJECT_NAME} PRIVATE user32 gdi32)
endif()

# The lifted SPU sources use bare #include "spu_context.h". Scope that include
# directory (and their flags) to those TUs alone: adding it to the whole target
# changes the compile command of every lifted PPU TU, and those are large enough
# that the needless rebuild is measured in tens of minutes.
#   SPU_USE_DISPATCH_MT -- route SPU_IB_DISPATCH through the musttail path.
#   -O2                 -- the target-wide -O1 exists for the huge PPU TUs; the
#                          SPU sources are small and decode-hot, so they get
#                          full optimization. The last -O on the line wins.
if(SPU_SOURCES)
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu"
        COMPILE_DEFINITIONS SPU_USE_DISPATCH_MT)
    if(NOT MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
endif()

set_source_files_properties(spu_job_diagnostics.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu")

# Embed the project lettermark in Windows executables. SDL uses the first
# icon resource for the game window; Explorer uses it for the executable.
if(WIN32)
    enable_language(RC)
    set_source_files_properties(app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/wkcii.ico")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${RECOMP_DIR}"                            # the generated ppu_recomp.h
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"        # win32_compat.h and friends
    "${PS3RECOMP_DIR}/libs/video"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ps3recomp_runtime
)

# The runtime is C++, and on Apple also Objective-C. Letting CMake infer the
# link language from the generated C would leave the C++ standard library out.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Windows system import libraries. dbghelp symbolizes crash-handler addresses
# against the PDB, user32 and gdi32 are what the backend's window and its GDI
# present are made of, and winmm is timeBeginPeriod. None of the four names
# anything a POSIX linker can find; the D3D12 backend's own imports come in
# through the #pragma comment(lib) in libs/video/rsx_d3d12_backend.c.
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE windows_guids.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dbghelp
        user32
        gdi32
        winmm
    )
endif()

# ---------------------------------------------------------------------------
# Compiler-specific flags
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fno-strict-aliasing
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3 /wd4100 /wd4244 /wd4267
        /Zc:__cplusplus         # report correct __cplusplus value
        # ppu_recomp.h defines PPU_FENCE, the lowering of the guest's
        # sync/lwsync/eieio/isync, and in C that is <stdatomic.h>. The lifter
        # writes C++ by default, but --single-file writes ppu_recomp.c, and a
        # port that took it would not compile without this. The runtime's own
        # build already carries the same flag for the same reason.
        /experimental:c11atomics
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# The lifted translation units are machine-generated and can run to hundreds of
# thousands of lines each, so they get their own flags:
#
#   - warnings off. They are about generated code and nobody will act on them,
#     and at this volume they bury the diagnostics that matter.
#   - no floating-point contraction. arm64 fuses a multiply and an add by
#     default; a PPC fmadd is spelt as one where the lifter means one.
#   - /bigobj on MSVC. A single lifted file routinely passes the 65,536-section
#     limit of the default object format.
set_source_files_properties(vdec.cpp PROPERTIES INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/codec")
set_source_files_properties(adec.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/codec;${PS3RECOMP_FFMPEG_INCLUDE_DIR}"
    COMPILE_DEFINITIONS PS3RECOMP_FFMPEG_RUNTIME)

if(MSVC)
    set_source_files_properties(dmux.cpp PROPERTIES COMPILE_OPTIONS "/EHsc")
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off")
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

option(WKC2_BUILD_TESTS "Build port-specific regression tests" ON)
if(WKC2_BUILD_TESTS)
    enable_testing()
    # Exercise the linked channel implementation without loading game files.
    target_sources(${PROJECT_NAME} PRIVATE tests/play_environment_test.cpp tests/native_skinning_test.cpp tests/spu_title_test.cpp tests/spu_dma_poll_test.c tests/pad_file_test.c tests/spu_microstep_profile_test.c)
    target_sources(${PROJECT_NAME} PRIVATE tests/native_skinning_stream_test.cpp tests/native_vertex_decode_test.cpp tests/native_index_decode_test.cpp)
    target_sources(${PROJECT_NAME} PRIVATE tests/native_vertex_pack_test.cpp tests/native_geometry_filter_test.cpp)
    # Decode and filtering distinguish rounded operations from explicit FMA.
    # Keep those boundaries in their host-policy regressions, including ARM.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_property(SOURCE tests/native_vertex_decode_test.cpp tests/native_geometry_filter_test.cpp APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
    elseif(MSVC)
        set_property(SOURCE tests/native_vertex_decode_test.cpp tests/native_geometry_filter_test.cpp APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
    endif()
    set_source_files_properties(tests/play_environment_test.cpp tests/native_skinning_test.cpp tests/spu_title_test.cpp tests/spu_dma_poll_test.c tests/pad_file_test.c tests/spu_microstep_profile_test.c PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu")
    set_source_files_properties(tests/pad_file_test.c PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${PS3RECOMP_DIR}/libs/input")
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS WKC2_BUILD_SELF_TESTS)
    add_executable(wkc2_pad_stick_test tests/pad_stick_test.c)
    target_include_directories(wkc2_pad_stick_test PRIVATE "${PS3RECOMP_DIR}/libs/input")
    if(NOT MSVC)
        target_link_libraries(wkc2_pad_stick_test PRIVATE m)
    endif()
    add_test(NAME wkc2_pad_stick COMMAND wkc2_pad_stick_test)
    set_tests_properties(wkc2_pad_stick PROPERTIES TIMEOUT 10)
    add_executable(wkc2_graphics_menu_test tests/graphics_menu_test.cpp graphics_menu.cpp graphics_settings.cpp)
    target_include_directories(wkc2_graphics_menu_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/ppu")
    add_test(NAME wkc2_graphics_menu COMMAND wkc2_graphics_menu_test)
    set_tests_properties(wkc2_graphics_menu PROPERTIES TIMEOUT 10)
    add_executable(wkc2_graphics_timing_test tests/graphics_timing_test.cpp graphics_timing.cpp graphics_settings.cpp)
    target_include_directories(wkc2_graphics_timing_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/ppu")
    add_test(NAME wkc2_graphics_timing COMMAND wkc2_graphics_timing_test)
    set_tests_properties(wkc2_graphics_timing PROPERTIES TIMEOUT 10)
    add_executable(wkc2_graphics_readback_test tests/graphics_readback_test.cpp graphics_readback.cpp)
    target_include_directories(wkc2_graphics_readback_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/ppu")
    add_test(NAME wkc2_graphics_readback COMMAND wkc2_graphics_readback_test)
    set_tests_properties(wkc2_graphics_readback PROPERTIES TIMEOUT 10)
    add_executable(wkc2_graphics_settings_test tests/graphics_settings_test.cpp graphics_settings.cpp)
    target_include_directories(wkc2_graphics_settings_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME wkc2_graphics_settings COMMAND wkc2_graphics_settings_test)
    set_tests_properties(wkc2_graphics_settings PROPERTIES TIMEOUT 10)
    foreach(cri_id IN ITEMS -1 22 23)
        add_test(NAME wkc2_spu_title_${cri_id}
            COMMAND ${PROJECT_NAME} --test-spu-title ${cri_id})
        set_tests_properties(wkc2_spu_title_${cri_id} PROPERTIES
            ENVIRONMENT "SPU_CRI_IMAGE=${cri_id}" TIMEOUT 10)
    endforeach()
    add_test(NAME wkc2_play_environment COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_play_environment.py" $<TARGET_FILE:${PROJECT_NAME}>)
    set_tests_properties(wkc2_play_environment PROPERTIES TIMEOUT 15)
    add_test(NAME wkc2_native_skinning COMMAND ${PROJECT_NAME} --test-native-skinning)
    set_tests_properties(wkc2_native_skinning PROPERTIES TIMEOUT 20)
    add_test(NAME wkc2_native_geometry_hook COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_native_geometry_hook.py")
    set_tests_properties(wkc2_native_geometry_hook PROPERTIES TIMEOUT 10)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64|i.86")
        if(MSVC)
            set_property(SOURCE tests/native_skinning_test.cpp tests/native_skinning_stream_test.cpp tests/native_vertex_decode_test.cpp tests/native_vertex_pack_test.cpp APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
        else()
            set_property(SOURCE tests/native_skinning_test.cpp tests/native_skinning_stream_test.cpp tests/native_vertex_decode_test.cpp tests/native_vertex_pack_test.cpp APPEND PROPERTY COMPILE_OPTIONS -mavx2 -mfma)
        endif()
    endif()
    add_test(NAME wkc2_spu_entry_manifest
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_spu_entries.py")
    set_tests_properties(wkc2_spu_entry_manifest PROPERTIES TIMEOUT 10)
    add_test(NAME wkc2_spu_dma_poll COMMAND ${PROJECT_NAME} --test-spu-dma-poll 0)
    set_tests_properties(wkc2_spu_dma_poll PROPERTIES ENVIRONMENT "SPU_DMA_REPEAT_LIMIT=0" TIMEOUT 10)
    add_test(NAME wkc2_spu_dma_poll_default
        COMMAND ${CMAKE_COMMAND} -E env --unset=SPU_DMA_REPEAT_LIMIT $<TARGET_FILE:${PROJECT_NAME}> --test-spu-dma-poll 0)
    set_tests_properties(wkc2_spu_dma_poll_default PROPERTIES TIMEOUT 10)
    add_test(NAME wkc2_spu_dma_repeat_diagnostic COMMAND ${PROJECT_NAME} --test-spu-dma-poll 1)
    set_tests_properties(wkc2_spu_dma_repeat_diagnostic PROPERTIES ENVIRONMENT "SPU_DMA_REPEAT_LIMIT=8" TIMEOUT 10)
    add_test(NAME wkc2_pad_file
        COMMAND ${PROJECT_NAME} --test-pad-file "${CMAKE_BINARY_DIR}/pad-input-fixture.txt")
    set_tests_properties(wkc2_pad_file PROPERTIES TIMEOUT 10)
    find_package(Threads REQUIRED)
    add_executable(wkc2_osk_test tests/osk_test.cpp osk.cpp)
    target_include_directories(wkc2_osk_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    target_compile_definitions(wkc2_osk_test PRIVATE WKC2_OSK_TEST_BACKEND)
    target_link_libraries(wkc2_osk_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_osk COMMAND wkc2_osk_test "${CMAKE_BINARY_DIR}/osk-input-fixture.txt")
    set_tests_properties(wkc2_osk PROPERTIES TIMEOUT 10)
    add_executable(wkc2_l10n_test tests/l10n_test.cpp l10n.cpp)
    target_include_directories(wkc2_l10n_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    add_test(NAME wkc2_l10n COMMAND wkc2_l10n_test)
    add_executable(wkc2_vertex_fetch_test tests/vertex_fetch_test.c
        "${PS3RECOMP_DIR}/libs/video/rsx_vertex_fetch.c")
    target_include_directories(wkc2_vertex_fetch_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_vertex_fetch COMMAND wkc2_vertex_fetch_test)
    set_tests_properties(wkc2_vertex_fetch PROPERTIES TIMEOUT 10)
    if(WIN32)
        add_executable(wkc2_frame_timeline_test tests/frame_timeline_test.c)
        target_include_directories(wkc2_frame_timeline_test PRIVATE "${PS3RECOMP_DIR}/include")
        add_test(NAME wkc2_frame_timeline COMMAND wkc2_frame_timeline_test
            "${CMAKE_BINARY_DIR}/timeline-fixture.gate" "${CMAKE_BINARY_DIR}/timeline-fixture.trace")
        set_tests_properties(wkc2_frame_timeline PROPERTIES TIMEOUT 15)
    endif()
    set(SPU_FP_FIXTURE "${CMAKE_BINARY_DIR}/spu-fp-fixture/spu_fp_vectors.h")
    add_custom_command(OUTPUT "${SPU_FP_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_spu_fp_fixture.py" "${SPU_FP_FIXTURE}"
        DEPENDS tools/prepare_spu_fp_fixture.py)
    add_executable(wkc2_spu_fp_test tests/spu_fp_test.c "${SPU_FP_FIXTURE}")
    target_include_directories(wkc2_spu_fp_test PRIVATE
        "${CMAKE_BINARY_DIR}/spu-fp-fixture" "${PS3RECOMP_DIR}/runtime/spu")
    add_test(NAME wkc2_spu_fp COMMAND wkc2_spu_fp_test)
    set_tests_properties(wkc2_spu_fp PROPERTIES TIMEOUT 10)
    add_executable(wkc2_spu_fnms_vector_test tests/spu_fnms_vector_test.c)
    target_include_directories(wkc2_spu_fnms_vector_test PRIVATE "${PS3RECOMP_DIR}/runtime/spu")
    add_test(NAME wkc2_spu_fnms_vector COMMAND wkc2_spu_fnms_vector_test)
    set_tests_properties(wkc2_spu_fnms_vector PROPERTIES TIMEOUT 15)
    # Compile the SDK's exact guard body, with isolated diagnostic/halt capture.
    set(SPU_DEPTH_FIXTURE "${CMAKE_BINARY_DIR}/spu-depth-fixture/spu_depth_guard_fixture.inc")
    add_custom_command(OUTPUT "${SPU_DEPTH_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_spu_depth_fixture.py"
            "${PS3RECOMP_DIR}" "${SPU_DEPTH_FIXTURE}"
        DEPENDS tools/prepare_spu_depth_fixture.py "${PS3RECOMP_DIR}/runtime/spu/spu_drain.c")
    add_executable(wkc2_spu_depth_guard_test tests/spu_depth_guard_test.c "${SPU_DEPTH_FIXTURE}")
    target_include_directories(wkc2_spu_depth_guard_test PRIVATE
        "${CMAKE_BINARY_DIR}/spu-depth-fixture" "${PS3RECOMP_DIR}/runtime/spu"
        "${PS3RECOMP_DIR}/runtime/platform")
    if(NOT WIN32)
        target_sources(wkc2_spu_depth_guard_test PRIVATE
            "${PS3RECOMP_DIR}/runtime/platform/win32_compat.c")
        target_link_libraries(wkc2_spu_depth_guard_test PRIVATE Threads::Threads m)
    endif()
    add_test(NAME wkc2_spu_depth_guard
        COMMAND "${CMAKE_COMMAND}" -E env --unset=SPU_HOST_DEPTH_MAX
            $<TARGET_FILE:wkc2_spu_depth_guard_test> 2000)
    add_test(NAME wkc2_spu_depth_guard_configured COMMAND wkc2_spu_depth_guard_test 4)
    set_tests_properties(wkc2_spu_depth_guard_configured PROPERTIES
        ENVIRONMENT "SPU_HOST_DEPTH_MAX=0x4")
    add_test(NAME wkc2_spu_depth_guard_zero COMMAND wkc2_spu_depth_guard_test 0)
    set_tests_properties(wkc2_spu_depth_guard_zero PROPERTIES
        ENVIRONMENT "SPU_HOST_DEPTH_MAX=0")
    add_test(NAME wkc2_spu_depth_guard_invalid COMMAND wkc2_spu_depth_guard_test 0)
    set_tests_properties(wkc2_spu_depth_guard_invalid PROPERTIES
        ENVIRONMENT "SPU_HOST_DEPTH_MAX=invalid")
    set_tests_properties(wkc2_spu_depth_guard wkc2_spu_depth_guard_configured
        wkc2_spu_depth_guard_zero wkc2_spu_depth_guard_invalid PROPERTIES TIMEOUT 15)

    add_executable(wkc2_spu_cntb_test tests/spu_cntb_test.c)
    target_include_directories(wkc2_spu_cntb_test PRIVATE "${PS3RECOMP_DIR}/runtime/spu")
    add_test(NAME wkc2_spu_cntb COMMAND wkc2_spu_cntb_test)
    set_tests_properties(wkc2_spu_cntb PROPERTIES TIMEOUT 10)

    foreach(shufb_path IN ITEMS default scalar)
        add_executable(wkc2_spu_shufb_${shufb_path}_test tests/spu_shufb_test.c)
        target_include_directories(wkc2_spu_shufb_${shufb_path}_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/spu")
        if(shufb_path STREQUAL "scalar")
            target_compile_definitions(wkc2_spu_shufb_${shufb_path}_test PRIVATE
                PS3RECOMP_SPU_SHUFB_SCALAR=1 WKC2_SHUFB_EXPECT_SCALAR=1)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64|i[3-6]86)$"
               AND CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$")
            # The normal Windows clang-cl build uses AVX2 already. Explicitly
            # select an ISA here so this test cannot silently run only scalar.
            target_compile_definitions(wkc2_spu_shufb_${shufb_path}_test PRIVATE
                WKC2_SHUFB_EXPECT_SIMD=1)
            if(MSVC)
                target_compile_options(wkc2_spu_shufb_${shufb_path}_test PRIVATE /arch:AVX2)
            else()
                target_compile_options(wkc2_spu_shufb_${shufb_path}_test PRIVATE -msse4.1)
            endif()
        endif()
        add_test(NAME wkc2_spu_shufb_${shufb_path}
            COMMAND wkc2_spu_shufb_${shufb_path}_test)
        set_tests_properties(wkc2_spu_shufb_${shufb_path} PROPERTIES TIMEOUT 15)
    endforeach()

    foreach(byte_variant IN ITEMS vector scalar)
        add_executable(wkc2_spu_byte_${byte_variant}_test tests/spu_byte_ops_test.c)
        target_include_directories(wkc2_spu_byte_${byte_variant}_test PRIVATE
            "${PS3RECOMP_DIR}/runtime/spu")
        if(byte_variant STREQUAL "scalar")
            target_compile_definitions(wkc2_spu_byte_${byte_variant}_test PRIVATE
                PS3RECOMP_SPU_BYTE_SCALAR=1)
        endif()
        add_test(NAME wkc2_spu_byte_${byte_variant} COMMAND wkc2_spu_byte_${byte_variant}_test)
        set_tests_properties(wkc2_spu_byte_${byte_variant} PROPERTIES TIMEOUT 20)
    endforeach()

    set(SPURS_ABI_FIXTURE "${CMAKE_BINARY_DIR}/spurs-abi-fixture")
    set(PPU_VSUB_FIXTURE "${CMAKE_BINARY_DIR}/ppu-vsub-fixture.h")
    add_custom_command(OUTPUT "${PPU_VSUB_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_ppu_vsub_fixture.py"
            "${PS3RECOMP_DIR}" "${PPU_VSUB_FIXTURE}"
        DEPENDS tools/prepare_ppu_vsub_fixture.py "${PS3RECOMP_DIR}/tools/ppu_lifter.py"
            "${PS3RECOMP_DIR}/tools/ppu_disasm.py")
    add_executable(wkc2_ppu_vsub_test tests/ppu_vsub_test.cpp "${PPU_VSUB_FIXTURE}")
    target_include_directories(wkc2_ppu_vsub_test PRIVATE "${CMAKE_BINARY_DIR}")
    add_test(NAME wkc2_ppu_vsub COMMAND wkc2_ppu_vsub_test)
    set_tests_properties(wkc2_ppu_vsub PROPERTIES TIMEOUT 10)
    add_test(NAME wkc2_pad_exports COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_pad_exports.py" "${HLE_NID_TABLE}")
    set_tests_properties(wkc2_pad_exports PROPERTIES TIMEOUT 10)
    add_custom_command(OUTPUT "${SPURS_ABI_FIXTURE}/fixture.c" "${SPURS_ABI_FIXTURE}/fixture.h"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_spurs_abi_fixture.py"
            "${PS3RECOMP_DIR}" "${SPURS_ABI_FIXTURE}"
        DEPENDS tools/prepare_spurs_abi_fixture.py "${PS3RECOMP_DIR}/tools/spu_lifter.py")
    add_executable(wkc2_spurs_abi_test tests/spurs_abi_test.c
        "${SPURS_ABI_FIXTURE}/fixture.c" "${PS3RECOMP_DIR}/runtime/spu/spurs_job.c")
    target_include_directories(wkc2_spurs_abi_test PRIVATE
        "${SPURS_ABI_FIXTURE}" "${PS3RECOMP_DIR}/runtime/spu")
    add_test(NAME wkc2_spurs_abi COMMAND wkc2_spurs_abi_test)
    set_tests_properties(wkc2_spurs_abi PROPERTIES TIMEOUT 10)
    add_executable(wkc2_spurs_job_queue_test tests/spurs_job_queue_test.c
        "${PS3RECOMP_DIR}/libs/spurs/cellSpursJq.c")
    target_include_directories(wkc2_spurs_job_queue_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/spurs"
        "${PS3RECOMP_DIR}/runtime/ppu")
    add_test(NAME wkc2_spurs_job_queue COMMAND wkc2_spurs_job_queue_test)
    set_tests_properties(wkc2_spurs_job_queue PROPERTIES TIMEOUT 10)
    add_executable(wkc2_spu_interp_profile_test tests/spu_interp_profile_test.c
        "${PS3RECOMP_DIR}/runtime/spu/spu_interp.c" "${PS3RECOMP_DIR}/runtime/spu/spu_interp_profile.c"
        "${PS3RECOMP_DIR}/runtime/spu/spu_float_mode.c")
    add_executable(wkc2_spu_interp_ops_test tests/spu_interp_ops_test.c
        "${PS3RECOMP_DIR}/runtime/spu/spu_interp.c" "${PS3RECOMP_DIR}/runtime/spu/spu_interp_profile.c"
        "${PS3RECOMP_DIR}/runtime/spu/spu_float_mode.c")
    target_include_directories(wkc2_spu_interp_ops_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/spu")
    target_link_libraries(wkc2_spu_interp_ops_test PRIVATE Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(wkc2_spu_interp_ops_test PRIVATE m)
    endif()
    add_test(NAME wkc2_spu_interp_ops COMMAND wkc2_spu_interp_ops_test)
    set_tests_properties(wkc2_spu_interp_ops PROPERTIES TIMEOUT 10)
    target_include_directories(wkc2_spu_interp_profile_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/spu")
    target_link_libraries(wkc2_spu_interp_profile_test PRIVATE Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(wkc2_spu_interp_profile_test PRIVATE m)
    endif()
    set(SPU_INTERP_PROFILE_FIXTURE "${CMAKE_BINARY_DIR}/spu-interp-profile.tsv")
    add_test(NAME wkc2_spu_interp_profile
        COMMAND wkc2_spu_interp_profile_test "${SPU_INTERP_PROFILE_FIXTURE}" 1)
    set_tests_properties(wkc2_spu_interp_profile PROPERTIES
        ENVIRONMENT "SPU_INTERP_PROFILE=${SPU_INTERP_PROFILE_FIXTURE}" TIMEOUT 10)
    add_test(NAME wkc2_spu_interp_profile_off
        COMMAND ${CMAKE_COMMAND} -E env --unset=SPU_INTERP_PROFILE
            $<TARGET_FILE:wkc2_spu_interp_profile_test> "${SPU_INTERP_PROFILE_FIXTURE}.off" 0)
    set_tests_properties(wkc2_spu_interp_profile_off PROPERTIES TIMEOUT 10)
    add_test(NAME wkc2_spu_interp_forbidden COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_spu_fallback_guard.py"
        --program $<TARGET_FILE:wkc2_spu_interp_profile_test> --path interp)
    add_test(NAME wkc2_spu_microstep_forbidden COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_spu_fallback_guard.py"
        --program $<TARGET_FILE:${PROJECT_NAME}> --path microstep)
    set_tests_properties(wkc2_spu_interp_forbidden wkc2_spu_microstep_forbidden PROPERTIES TIMEOUT 15)
    add_executable(wkc2_spu_float_mode_test tests/spu_float_mode_test.c
        "${PS3RECOMP_DIR}/runtime/spu/spu_float_mode.c")
    target_include_directories(wkc2_spu_float_mode_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/spu")
    target_link_libraries(wkc2_spu_float_mode_test PRIVATE Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(wkc2_spu_float_mode_test PRIVATE m)
    endif()
    add_test(NAME wkc2_spu_float_host
        COMMAND ${CMAKE_COMMAND} -E env --unset=SPU_FLOAT_MODE
            $<TARGET_FILE:wkc2_spu_float_mode_test> host)
    add_test(NAME wkc2_spu_float_exact COMMAND wkc2_spu_float_mode_test exact)
    add_test(NAME wkc2_spu_float_audit COMMAND wkc2_spu_float_mode_test audit)
    set_tests_properties(wkc2_spu_float_exact PROPERTIES ENVIRONMENT "SPU_FLOAT_MODE=exact")
    set_tests_properties(wkc2_spu_float_audit PROPERTIES ENVIRONMENT "SPU_FLOAT_MODE=audit")
    set_tests_properties(wkc2_spu_float_host wkc2_spu_float_exact wkc2_spu_float_audit
        PROPERTIES TIMEOUT 60)
    add_test(NAME wkc2_spu_microstep_profile COMMAND ${PROJECT_NAME}
        --test-spu-microstep-profile "${CMAKE_BINARY_DIR}/spu-microstep-profile.tsv")
    set_tests_properties(wkc2_spu_microstep_profile PROPERTIES
        ENVIRONMENT "SPU_INTERP_PROFILE=${CMAKE_BINARY_DIR}/spu-microstep-profile.tsv" TIMEOUT 10)
    if(WIN32)
        add_executable(wkc2_pad_report_test tests/pad_report_test.c)
        target_include_directories(wkc2_pad_report_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/input")
        target_link_libraries(wkc2_pad_report_test PRIVATE xinput)
        add_test(NAME wkc2_pad_report COMMAND wkc2_pad_report_test)
        set_tests_properties(wkc2_pad_report PROPERTIES TIMEOUT 15)
        add_executable(wkc2_ppu_join_test tests/ppu_join_test.c
            "${PS3RECOMP_DIR}/runtime/syscalls/sys_ppu_thread.c")
        target_include_directories(wkc2_ppu_join_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/syscalls")
        add_test(NAME wkc2_ppu_join COMMAND wkc2_ppu_join_test)
        set_tests_properties(wkc2_ppu_join PROPERTIES TIMEOUT 20)
        add_executable(wkc2_osk_window_test tests/osk_window_test.cpp)
        target_link_libraries(wkc2_osk_window_test PRIVATE user32 gdi32 Threads::Threads)
        if(MSVC)
            target_compile_options(wkc2_osk_window_test PRIVATE /EHsc)
        endif()
        add_test(NAME wkc2_osk_window COMMAND wkc2_osk_window_test)
        set_tests_properties(wkc2_osk_window PROPERTIES TIMEOUT 10)
        add_executable(wkc2_fp_precision_test tests/fp_precision_test.cpp
            "${PS3RECOMP_DIR}/libs/video/rsx_fp_decompiler.c")
        target_include_directories(wkc2_fp_precision_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_fp_precision_test PRIVATE d3d11 d3dcompiler)
        add_test(NAME wkc2_fp_precision COMMAND wkc2_fp_precision_test)
        set_tests_properties(wkc2_fp_precision PROPERTIES TIMEOUT 60)
        add_executable(wkc2_spurs_job_test tests/spurs_job_test.c
            "${PS3RECOMP_DIR}/libs/spurs/cellSpurs.c")
        target_include_directories(wkc2_spurs_job_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/spurs"
            "${PS3RECOMP_DIR}/runtime/spu" "${PS3RECOMP_DIR}/runtime/ppu")
        add_test(NAME wkc2_spurs_job COMMAND wkc2_spurs_job_test)
        set_tests_properties(wkc2_spurs_job PROPERTIES TIMEOUT 20)
    endif()
    add_executable(wkc2_indexed_geometry_test tests/indexed_geometry_test.cpp
        "${PS3RECOMP_DIR}/libs/video/rsx_commands.c")
    target_include_directories(wkc2_indexed_geometry_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_indexed_geometry COMMAND wkc2_indexed_geometry_test)
    set_tests_properties(wkc2_indexed_geometry PROPERTIES TIMEOUT 10)
    add_executable(wkc2_depth_clip_test tests/depth_clip_test.c
        "${PS3RECOMP_DIR}/libs/video/rsx_commands.c")
    target_include_directories(wkc2_depth_clip_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_depth_clip COMMAND wkc2_depth_clip_test)
    set_tests_properties(wkc2_depth_clip PROPERTIES TIMEOUT 10)
    add_executable(wkc2_depth_state_test tests/depth_state_test.c)
    target_include_directories(wkc2_depth_state_test PRIVATE
        "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_depth_state COMMAND wkc2_depth_state_test)
    set_tests_properties(wkc2_depth_state PROPERTIES TIMEOUT 10)
    add_executable(wkc2_present_metrics_test tests/present_metrics_test.c)
    target_include_directories(wkc2_present_metrics_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_present_metrics COMMAND wkc2_present_metrics_test)
    set_tests_properties(wkc2_present_metrics PROPERTIES TIMEOUT 10)
    if(WIN32)
        add_executable(wkc2_fragment_inputs_test tests/fragment_inputs_test.c)
        target_include_directories(wkc2_fragment_inputs_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_fragment_inputs_test PRIVATE ps3recomp_runtime d3dcompiler)
        add_test(NAME wkc2_fragment_inputs COMMAND wkc2_fragment_inputs_test)
        set_tests_properties(wkc2_fragment_inputs PROPERTIES TIMEOUT 20)
        set(PRESENT_MEASURE_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/present_measure_fixture.h")
        add_custom_command(OUTPUT "${PRESENT_MEASURE_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_present_measure_fixture.py"
                "${PS3RECOMP_DIR}/libs/video/rsx_live_draw.c" "${PRESENT_MEASURE_FIXTURE}"
            DEPENDS tools/prepare_present_measure_fixture.py "${PS3RECOMP_DIR}/libs/video/rsx_live_draw.c")
        add_executable(wkc2_present_measure_test tests/present_measure_test.c "${PRESENT_MEASURE_FIXTURE}")
        target_include_directories(wkc2_present_measure_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PS3RECOMP_DIR}/libs/video")
        add_test(NAME wkc2_present_measure COMMAND wkc2_present_measure_test)
        set_tests_properties(wkc2_present_measure PROPERTIES TIMEOUT 10)
        add_executable(wkc2_d3d12_raster_clip_test tests/d3d12_raster_clip_test.c)
        target_include_directories(wkc2_d3d12_raster_clip_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_d3d12_raster_clip_test PRIVATE d3d12 dxgi dxguid d3dcompiler)
        add_test(NAME wkc2_d3d12_raster_clip COMMAND wkc2_d3d12_raster_clip_test)
        set_tests_properties(wkc2_d3d12_raster_clip PROPERTIES TIMEOUT 20)
        add_executable(wkc2_d3d12_depth_bias_test tests/d3d12_depth_bias_test.c)
        target_include_directories(wkc2_d3d12_depth_bias_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_d3d12_depth_bias_test PRIVATE d3d12 dxgi dxguid d3dcompiler)
        add_test(NAME wkc2_d3d12_depth_bias COMMAND wkc2_d3d12_depth_bias_test)
        set_tests_properties(wkc2_d3d12_depth_bias PROPERTIES TIMEOUT 20)
        add_executable(wkc2_d3d12_shadow_test tests/d3d12_shadow_test.c)
        target_include_directories(wkc2_d3d12_shadow_test PRIVATE "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_d3d12_shadow_test PRIVATE d3d12 dxgi dxguid d3dcompiler)
        add_test(NAME wkc2_d3d12_shadow COMMAND wkc2_d3d12_shadow_test)
        set_tests_properties(wkc2_d3d12_shadow PROPERTIES TIMEOUT 20)
        add_executable(wkc2_d3d12_masked_clear_test tests/d3d12_masked_clear_test.c)
        target_include_directories(wkc2_d3d12_masked_clear_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_d3d12_masked_clear_test PRIVATE d3d12 dxgi dxguid d3dcompiler)
        add_test(NAME wkc2_d3d12_masked_clear COMMAND wkc2_d3d12_masked_clear_test)
        set_tests_properties(wkc2_d3d12_masked_clear PROPERTIES TIMEOUT 20)
        add_executable(wkc2_d3d12_surface_readback_test tests/d3d12_surface_readback_test.c)
        target_include_directories(wkc2_d3d12_surface_readback_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_d3d12_surface_readback_test PRIVATE d3d12 dxgi dxguid)
        add_test(NAME wkc2_d3d12_surface_readback COMMAND wkc2_d3d12_surface_readback_test)
        set_tests_properties(wkc2_d3d12_surface_readback PROPERTIES TIMEOUT 20)
        add_executable(wkc2_d3d12_fxaa_gpu_test tests/d3d12_fxaa_gpu_test.c)
        target_include_directories(wkc2_d3d12_fxaa_gpu_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_d3d12_fxaa_gpu_test PRIVATE d3d12 dxgi dxguid d3dcompiler)
        add_test(NAME wkc2_d3d12_fxaa_gpu COMMAND wkc2_d3d12_fxaa_gpu_test)
        set_tests_properties(wkc2_d3d12_fxaa_gpu PROPERTIES TIMEOUT 20)
        add_executable(wkc2_d3d12_counter_gpu_test tests/d3d12_counter_gpu_test.c)
        target_include_directories(wkc2_d3d12_counter_gpu_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_d3d12_counter_gpu_test PRIVATE d3d12 dxgi dxguid)
        add_test(NAME wkc2_d3d12_counter_gpu COMMAND wkc2_d3d12_counter_gpu_test)
        set_tests_properties(wkc2_d3d12_counter_gpu PROPERTIES TIMEOUT 20)
        add_executable(wkc2_surface_clear_gpu_test tests/surface_clear_gpu_test.c)
        target_include_directories(wkc2_surface_clear_gpu_test PRIVATE
            "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_surface_clear_gpu_test PRIVATE d3d12 dxgi dxguid)
        add_test(NAME wkc2_surface_clear_gpu COMMAND wkc2_surface_clear_gpu_test)
        set_tests_properties(wkc2_surface_clear_gpu PROPERTIES TIMEOUT 20)
        add_executable(wkc2_d3d12_graphics_test tests/d3d12_graphics_test.c)
        target_include_directories(wkc2_d3d12_graphics_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
        add_test(NAME wkc2_d3d12_graphics COMMAND wkc2_d3d12_graphics_test)
        set_tests_properties(wkc2_d3d12_graphics PROPERTIES TIMEOUT 10)
    endif()
    add_executable(wkc2_vp_cache_test tests/vp_cache_test.c
        "${PS3RECOMP_DIR}/libs/video/rsx_vp_decompiler.c")
    target_include_directories(wkc2_vp_cache_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_vp_cache COMMAND wkc2_vp_cache_test)
    set_tests_properties(wkc2_vp_cache PROPERTIES TIMEOUT 10)
    add_executable(wkc2_vp_coissue_test tests/vp_coissue_test.c
        "${PS3RECOMP_DIR}/libs/video/rsx_vp_decompiler.c")
    target_include_directories(wkc2_vp_coissue_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_vp_coissue COMMAND wkc2_vp_coissue_test)
    set_tests_properties(wkc2_vp_coissue PROPERTIES TIMEOUT 10)
    add_executable(wkc2_spirv_cache_test tests/spirv_cache_test.cpp)
    target_include_directories(wkc2_spirv_cache_test PRIVATE "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_spirv_cache COMMAND wkc2_spirv_cache_test)
    if(PS3RECOMP_RSX_VULKAN AND TARGET glslang::SPIRV)
        add_executable(wkc2_spirv_translation_cache_test tests/spirv_translation_cache_test.cpp)
        target_include_directories(wkc2_spirv_translation_cache_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
        target_link_libraries(wkc2_spirv_translation_cache_test PRIVATE ps3recomp_runtime)
        if(MSVC)
            target_compile_options(wkc2_spirv_translation_cache_test PRIVATE /EHsc)
        endif()
        add_test(NAME wkc2_spirv_translation_cache COMMAND wkc2_spirv_translation_cache_test 1)
        add_test(NAME wkc2_spirv_translation_cache_off COMMAND wkc2_spirv_translation_cache_test 0)
        set_tests_properties(wkc2_spirv_translation_cache PROPERTIES
            ENVIRONMENT "RSX_SPIRV_CACHE=1" TIMEOUT 20)
        set_tests_properties(wkc2_spirv_translation_cache_off PROPERTIES
            ENVIRONMENT "RSX_SPIRV_CACHE=0" TIMEOUT 20)
    endif()
    add_executable(wkc2_fp_identity_test tests/fp_identity_test.c
        "${PS3RECOMP_DIR}/libs/video/rsx_fp_decompiler.c")
    target_include_directories(wkc2_fp_identity_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    if(NOT WIN32)
        target_link_libraries(wkc2_fp_identity_test PRIVATE m)
    endif()
    add_test(NAME wkc2_fp_identity COMMAND wkc2_fp_identity_test)
    set_tests_properties(wkc2_fp_identity PROPERTIES TIMEOUT 10)
    add_executable(wkc2_alpha_reference_test tests/alpha_reference_test.c
        "${PS3RECOMP_DIR}/libs/video/rsx_fp_decompiler.c")
    target_include_directories(wkc2_alpha_reference_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    if(NOT WIN32)
        target_link_libraries(wkc2_alpha_reference_test PRIVATE m)
    endif()
    add_test(NAME wkc2_alpha_reference COMMAND wkc2_alpha_reference_test)
    set_tests_properties(wkc2_alpha_reference PROPERTIES TIMEOUT 10)
    add_executable(wkc2_rsx_request_audit_test tests/rsx_request_audit_test.c
        "${PS3RECOMP_DIR}/libs/video/rsx_dispatch.c")
    target_include_directories(wkc2_rsx_request_audit_test PRIVATE
        "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/include")
    add_test(NAME wkc2_rsx_request_audit COMMAND wkc2_rsx_request_audit_test
        "${CMAKE_CURRENT_BINARY_DIR}/rsx-audit-test.tsv")
    set_tests_properties(wkc2_rsx_request_audit PROPERTIES TIMEOUT 10)
    add_executable(wkc2_vertex_batch_test tests/vertex_batch_test.cpp
        "${PS3RECOMP_DIR}/libs/video/rsx_vertex_compact.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_dispatch.c")
    target_include_directories(wkc2_vertex_batch_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    if(NOT WIN32)
        target_link_libraries(wkc2_vertex_batch_test PRIVATE m)
    endif()
    add_test(NAME wkc2_vertex_batch COMMAND wkc2_vertex_batch_test)
    set_tests_properties(wkc2_vertex_batch PROPERTIES TIMEOUT 20)
    add_executable(wkc2_draw_engine_test
        "${PS3RECOMP_DIR}/libs/video/tests/test_rsx_draw_engine.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_draw_engine.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_dispatch.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_vertex_fetch.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_vertex_compact.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_texture_layout.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_vp_decompiler.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_fp_decompiler.c")
    target_include_directories(wkc2_draw_engine_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    if(NOT WIN32)
        target_link_libraries(wkc2_draw_engine_test PRIVATE m)
    endif()
    add_test(NAME wkc2_draw_engine COMMAND wkc2_draw_engine_test)
    set_tests_properties(wkc2_draw_engine PROPERTIES TIMEOUT 15)
    # Includes cellGcmSys.c to drive the FIFO walker's own 2D-engine methods.
    add_executable(wkc2_nv3089_blit_test tests/nv3089_blit_test.c)
    target_include_directories(wkc2_nv3089_blit_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/runtime/ppu")
    if(NOT WIN32)
        target_sources(wkc2_nv3089_blit_test PRIVATE
            "${PS3RECOMP_DIR}/runtime/platform/win32_compat.c")
        target_link_libraries(wkc2_nv3089_blit_test PRIVATE Threads::Threads m)
    endif()
    add_test(NAME wkc2_nv3089_blit COMMAND wkc2_nv3089_blit_test)
    set_tests_properties(wkc2_nv3089_blit PROPERTIES TIMEOUT 10)
    # Synthetic producer handoffs through the actual SDK FIFO walker.
    add_executable(wkc2_gcm_producer_wake_test tests/gcm_producer_wake_test.c)
    if(WIN32 AND PS3RECOMP_RSX_VULKAN)
        add_executable(wkc2_rsx_shader_requests_gpu_test tests/rsx_shader_requests_gpu_test.cpp)
        target_include_directories(wkc2_rsx_shader_requests_gpu_test PRIVATE
            "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/include" "${PS3RECOMP_VULKAN_INCLUDE_DIR}")
        target_link_libraries(wkc2_rsx_shader_requests_gpu_test PRIVATE ps3recomp_runtime)
        add_test(NAME wkc2_rsx_shader_requests_gpu COMMAND wkc2_rsx_shader_requests_gpu_test)
        set_tests_properties(wkc2_rsx_shader_requests_gpu PROPERTIES TIMEOUT 30)
        add_executable(wkc2_vk_polygon_offset_gpu_test tests/vk_polygon_offset_gpu_test.cpp)
        target_include_directories(wkc2_vk_polygon_offset_gpu_test PRIVATE
            "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/include" "${PS3RECOMP_VULKAN_INCLUDE_DIR}")
        target_link_libraries(wkc2_vk_polygon_offset_gpu_test PRIVATE ps3recomp_runtime)
        add_test(NAME wkc2_vk_polygon_offset_gpu COMMAND wkc2_vk_polygon_offset_gpu_test)
        set_tests_properties(wkc2_vk_polygon_offset_gpu PROPERTIES TIMEOUT 30)
        add_executable(wkc2_vk_depth_range_gpu_test tests/vk_depth_range_gpu_test.cpp)
        target_include_directories(wkc2_vk_depth_range_gpu_test PRIVATE
            "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/include" "${PS3RECOMP_VULKAN_INCLUDE_DIR}")
        target_link_libraries(wkc2_vk_depth_range_gpu_test PRIVATE ps3recomp_runtime)
        add_test(NAME wkc2_vk_depth_range_gpu COMMAND wkc2_vk_depth_range_gpu_test)
        set_tests_properties(wkc2_vk_depth_range_gpu PROPERTIES TIMEOUT 30)
        set(SAMPLER_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/sampler-fixture.inc")
        add_custom_command(OUTPUT "${SAMPLER_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_sampler_fixture.py"
                --source "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                --output "${SAMPLER_FIXTURE}"
            DEPENDS "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_sampler_fixture.py"
            VERBATIM)
        add_executable(wkc2_vk_sampler_lod_test tests/vk_sampler_lod_test.c "${SAMPLER_FIXTURE}")
        target_include_directories(wkc2_vk_sampler_lod_test PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}" "${PS3RECOMP_DIR}/libs/video"
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_VULKAN_INCLUDE_DIR}")
        add_test(NAME wkc2_vk_sampler_lod COMMAND wkc2_vk_sampler_lod_test)
        set_tests_properties(wkc2_vk_sampler_lod PROPERTIES TIMEOUT 10)
        set(PRESENT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/present-fixture.inc")
        add_custom_command(OUTPUT "${PRESENT_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_present_fixture.py"
                --source "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                --output "${PRESENT_FIXTURE}"
            DEPENDS "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_present_fixture.py"
            VERBATIM)
        add_executable(wkc2_vk_present_source_test tests/vk_present_source_test.c "${PRESENT_FIXTURE}")
        target_include_directories(wkc2_vk_present_source_test PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}" "${PS3RECOMP_VULKAN_INCLUDE_DIR}")
        add_test(NAME wkc2_vk_present_source COMMAND wkc2_vk_present_source_test)
        set_tests_properties(wkc2_vk_present_source PROPERTIES TIMEOUT 10)
        set(GRAPHICS_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/graphics-fixture.inc")
        add_custom_command(OUTPUT "${GRAPHICS_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_graphics_fixture.py"
                --source "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c" --output "${GRAPHICS_FIXTURE}"
            DEPENDS "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_graphics_fixture.py" VERBATIM)
        add_executable(wkc2_vk_graphics_test tests/vk_graphics_test.c "${GRAPHICS_FIXTURE}")
        target_include_directories(wkc2_vk_graphics_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_VULKAN_INCLUDE_DIR}")
        add_test(NAME wkc2_vk_graphics COMMAND wkc2_vk_graphics_test)
        set_tests_properties(wkc2_vk_graphics PROPERTIES TIMEOUT 10)
        set(VK_LIFETIME_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/vk-lifetime-fixture.inc")
        add_custom_command(OUTPUT "${VK_LIFETIME_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_vk_lifetime_fixture.py"
                --source "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                --output "${VK_LIFETIME_FIXTURE}"
            DEPENDS "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_vk_lifetime_fixture.py"
            VERBATIM)
        add_executable(wkc2_vk_batch_lifetime_test tests/vk_batch_lifetime_test.c "${VK_LIFETIME_FIXTURE}")
        target_include_directories(wkc2_vk_batch_lifetime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME wkc2_vk_batch_lifetime COMMAND wkc2_vk_batch_lifetime_test)
        set_tests_properties(wkc2_vk_batch_lifetime PROPERTIES TIMEOUT 10)
        set(VK_CONSTANT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/vk-constant-fixture.inc")
        add_custom_command(OUTPUT "${VK_CONSTANT_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_vk_constant_fixture.py"
                --source "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                --output "${VK_CONSTANT_FIXTURE}"
            DEPENDS "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_vk_constant_fixture.py"
            VERBATIM)
        add_executable(wkc2_vk_constant_reuse_test tests/vk_constant_reuse_test.c "${VK_CONSTANT_FIXTURE}")
        target_include_directories(wkc2_vk_constant_reuse_test PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}" "${PS3RECOMP_DIR}/libs/video")
        add_test(NAME wkc2_vk_constant_reuse COMMAND wkc2_vk_constant_reuse_test)
        set_tests_properties(wkc2_vk_constant_reuse PROPERTIES TIMEOUT 10)
        set(VK_ARENA_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/vk-arena-fixture.inc")
        add_custom_command(OUTPUT "${VK_ARENA_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_vk_arena_fixture.py"
                --source "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                --output "${VK_ARENA_FIXTURE}"
            DEPENDS "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_vk_arena_fixture.py"
            VERBATIM)
        add_executable(wkc2_vk_arena_reserve_test tests/vk_arena_reserve_test.c "${VK_ARENA_FIXTURE}")
        target_include_directories(wkc2_vk_arena_reserve_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME wkc2_vk_arena_reserve COMMAND wkc2_vk_arena_reserve_test)
        set_tests_properties(wkc2_vk_arena_reserve PROPERTIES TIMEOUT 10)
        set(DEPTH_SHADER_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/depth-shader-fixture.inc")
        add_custom_command(OUTPUT "${DEPTH_SHADER_FIXTURE}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_depth_shader_fixture.py"
                --source "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                --output "${DEPTH_SHADER_FIXTURE}"
            DEPENDS "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_depth_shader_fixture.py"
            VERBATIM)
        add_executable(wkc2_depth_shader_binding_test tests/depth_shader_binding_test.cpp "${DEPTH_SHADER_FIXTURE}")
        target_include_directories(wkc2_depth_shader_binding_test PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}" "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/include")
        target_link_libraries(wkc2_depth_shader_binding_test PRIVATE ps3recomp_runtime)
        add_test(NAME wkc2_depth_shader_binding COMMAND wkc2_depth_shader_binding_test)
        set_tests_properties(wkc2_depth_shader_binding PROPERTIES TIMEOUT 15)
        add_executable(wkc2_shadow_compare_test tests/shadow_compare_test.cpp)
        target_include_directories(wkc2_shadow_compare_test PRIVATE
            "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/include")
        target_link_libraries(wkc2_shadow_compare_test PRIVATE ps3recomp_runtime)
        add_test(NAME wkc2_shadow_compare COMMAND wkc2_shadow_compare_test)
        set_tests_properties(wkc2_shadow_compare PROPERTIES TIMEOUT 20)
    endif()
    if(WIN32)
        add_executable(wkc2_gcm_flip_completion_test tests/gcm_flip_completion_test.c)
        target_include_directories(wkc2_gcm_flip_completion_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/runtime/ppu")
        if(MSVC)
            target_compile_options(wkc2_gcm_flip_completion_test PRIVATE /experimental:c11atomics)
        endif()
        add_test(NAME wkc2_gcm_flip_completion COMMAND wkc2_gcm_flip_completion_test)
        set_tests_properties(wkc2_gcm_flip_completion PROPERTIES TIMEOUT 10)
    endif()
    if(MSVC)
        target_compile_options(wkc2_gcm_producer_wake_test PRIVATE /experimental:c11atomics)
    endif()
    target_include_directories(wkc2_gcm_producer_wake_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/runtime/ppu")
    if(NOT WIN32)
        target_sources(wkc2_gcm_producer_wake_test PRIVATE
            "${PS3RECOMP_DIR}/runtime/platform/win32_compat.c")
        target_link_libraries(wkc2_gcm_producer_wake_test PRIVATE Threads::Threads m)
    endif()
    add_test(NAME wkc2_gcm_producer_wake COMMAND wkc2_gcm_producer_wake_test)
    set_tests_properties(wkc2_gcm_producer_wake PROPERTIES TIMEOUT 15
        ENVIRONMENT "GCM_FIFO_KICK=1;GCM_PRODUCER_PROFILE=1")
    add_test(NAME wkc2_gcm_producer_wake_disabled COMMAND wkc2_gcm_producer_wake_test disabled)
    set_tests_properties(wkc2_gcm_producer_wake_disabled PROPERTIES TIMEOUT 15
        ENVIRONMENT "GCM_FIFO_KICK=0;GCM_PRODUCER_PROFILE=1")
    add_test(NAME wkc2_gcm_producer_wake_profile_off
        COMMAND "${CMAKE_COMMAND}" -E env --unset=GCM_PRODUCER_PROFILE
            GCM_FIFO_KICK=1 $<TARGET_FILE:wkc2_gcm_producer_wake_test>)
    set_tests_properties(wkc2_gcm_producer_wake_profile_off PROPERTIES TIMEOUT 15)

    add_executable(wkc2_nv0039_copy_test tests/nv0039_copy_test.c)
    target_include_directories(wkc2_nv0039_copy_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video" "${PS3RECOMP_DIR}/runtime/ppu")
    if(NOT WIN32)
        target_sources(wkc2_nv0039_copy_test PRIVATE
            "${PS3RECOMP_DIR}/runtime/platform/win32_compat.c")
        target_link_libraries(wkc2_nv0039_copy_test PRIVATE Threads::Threads m)
    endif()
    add_test(NAME wkc2_nv0039_copy COMMAND wkc2_nv0039_copy_test)
    set_tests_properties(wkc2_nv0039_copy PROPERTIES TIMEOUT 15
        ENVIRONMENT "GCM_SUBCH1_2D=1;GCM_M2MF_VERIFY=1")
    add_test(NAME wkc2_nv0039_routing_3d COMMAND wkc2_nv0039_copy_test 3d)
    set_tests_properties(wkc2_nv0039_routing_3d PROPERTIES TIMEOUT 10
        ENVIRONMENT "GCM_SUBCH1_2D=0")
    add_executable(wkc2_texture_layout_test
        "${PS3RECOMP_DIR}/libs/video/tests/test_texture_layout.c"
        "${PS3RECOMP_DIR}/libs/video/rsx_texture_layout.c")
    target_include_directories(wkc2_texture_layout_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
    add_test(NAME wkc2_texture_layout COMMAND wkc2_texture_layout_test)
    set_tests_properties(wkc2_texture_layout PROPERTIES TIMEOUT 10)
    if(PS3RECOMP_FFMPEG_RUNTIME)
        set(CODEC_FIXTURE "${CMAKE_BINARY_DIR}/codec-fixture")
        add_custom_command(OUTPUT "${CODEC_FIXTURE}.m2v" "${CODEC_FIXTURE}.yuv"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_codec_fixture.py" "${CODEC_FIXTURE}"
            DEPENDS tools/prepare_codec_fixture.py)
        add_custom_target(wkc2_codec_fixture DEPENDS "${CODEC_FIXTURE}.m2v" "${CODEC_FIXTURE}.yuv")
        add_executable(wkc2_vdec_decode_test "${PS3RECOMP_DIR}/libs/codec/tests/test_vdec_decode.c")
        add_dependencies(wkc2_vdec_decode_test wkc2_codec_fixture)
        target_include_directories(wkc2_vdec_decode_test PRIVATE "${PS3RECOMP_DIR}/include" "${PS3RECOMP_FFMPEG_INCLUDE_DIR}")
        target_compile_definitions(wkc2_vdec_decode_test PRIVATE PS3RECOMP_FFMPEG_RUNTIME)
        target_link_libraries(wkc2_vdec_decode_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        add_test(NAME wkc2_vdec_decode COMMAND wkc2_vdec_decode_test decode mpeg2 "${CODEC_FIXTURE}.m2v" "${CODEC_FIXTURE}.yuv" 32 16)
        set_tests_properties(wkc2_vdec_decode PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
        add_test(NAME wkc2_vdec_timing COMMAND wkc2_vdec_decode_test timing "${CODEC_FIXTURE}.m2v" 30 1)
        set_tests_properties(wkc2_vdec_timing PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
        add_executable(wkc2_async_vdec_test tests/vdec_test.cpp vdec.cpp
            "${PS3RECOMP_DIR}/libs/codec/vdec_ffmpeg.c"
            "${PS3RECOMP_DIR}/libs/codec/ffmpeg_runtime.c")
        add_dependencies(wkc2_async_vdec_test wkc2_codec_fixture)
        target_include_directories(wkc2_async_vdec_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include"
            "${PS3RECOMP_DIR}/libs/codec" "${PS3RECOMP_FFMPEG_INCLUDE_DIR}")
        target_compile_definitions(wkc2_async_vdec_test PRIVATE PS3RECOMP_FFMPEG_RUNTIME)
        target_link_libraries(wkc2_async_vdec_test PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        add_test(NAME wkc2_async_vdec COMMAND wkc2_async_vdec_test "${CODEC_FIXTURE}.m2v" "${CODEC_FIXTURE}.yuv" 32 16 30 1)
        set_tests_properties(wkc2_async_vdec PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

    endif()
    add_executable(wkc2_adec_test tests/adec_test.cpp adec.cpp "${PS3RECOMP_DIR}/libs/codec/ffmpeg_runtime.c")
    target_include_directories(wkc2_adec_test PRIVATE "${PS3RECOMP_FFMPEG_INCLUDE_DIR}")
    target_compile_definitions(wkc2_adec_test PRIVATE PS3RECOMP_FFMPEG_RUNTIME)
    target_include_directories(wkc2_adec_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    target_link_libraries(wkc2_adec_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_adec COMMAND wkc2_adec_test)
    set_tests_properties(wkc2_adec PROPERTIES TIMEOUT 10)
    if(PS3RECOMP_FFMPEG_RUNTIME)
        set(AUDIO_FIXTURE "${CMAKE_BINARY_DIR}/audio-fixture")
        add_custom_command(OUTPUT "${AUDIO_FIXTURE}.ac3" "${AUDIO_FIXTURE}.stereo.f32be" "${AUDIO_FIXTURE}.surround.f32be"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_audio_fixture.py" "${AUDIO_FIXTURE}"
            DEPENDS tools/prepare_audio_fixture.py)
        add_custom_target(wkc2_audio_fixture DEPENDS "${AUDIO_FIXTURE}.ac3" "${AUDIO_FIXTURE}.stereo.f32be" "${AUDIO_FIXTURE}.surround.f32be")
        add_dependencies(wkc2_adec_test wkc2_audio_fixture)
        add_test(NAME wkc2_ac3_stereo COMMAND wkc2_adec_test ac3 "${AUDIO_FIXTURE}.ac3" "${AUDIO_FIXTURE}.stereo.f32be" 2)
        add_test(NAME wkc2_ac3_surround COMMAND wkc2_adec_test ac3 "${AUDIO_FIXTURE}.ac3" "${AUDIO_FIXTURE}.surround.f32be" 6)
        set_tests_properties(wkc2_ac3_stereo wkc2_ac3_surround PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
    endif()

    add_executable(wkc2_vpost_test tests/vpost_test.cpp vpost.cpp)
    target_include_directories(wkc2_vpost_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    target_link_libraries(wkc2_vpost_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_vpost COMMAND wkc2_vpost_test)
    set_tests_properties(wkc2_vpost PROPERTIES TIMEOUT 10)
    add_executable(wkc2_callback_stack_test tests/callback_stack_test.cpp)
    target_include_directories(wkc2_callback_stack_test PRIVATE "${PS3RECOMP_DIR}/runtime/ppu")
    target_link_libraries(wkc2_callback_stack_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_callback_stack COMMAND wkc2_callback_stack_test)
    set_tests_properties(wkc2_callback_stack PROPERTIES TIMEOUT 10)
    add_executable(wkc2_dmux_test tests/dmux_test.cpp dmux.cpp)
    target_include_directories(wkc2_dmux_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    target_link_libraries(wkc2_dmux_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_dmux COMMAND wkc2_dmux_test)
    set_tests_properties(wkc2_dmux PROPERTIES TIMEOUT 10)
    add_executable(wkc2_mpegps_test tests/mpegps_test.cpp)
    if(MSVC)
        target_compile_options(wkc2_mpegps_test PRIVATE /EHsc)
    endif()
    add_test(NAME wkc2_mpegps COMMAND wkc2_mpegps_test)
    set_tests_properties(wkc2_mpegps PROPERTIES TIMEOUT 10)
    add_executable(wkc2_pamf_test tests/pamf_test.cpp pamf.cpp)
    target_include_directories(wkc2_pamf_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    add_test(NAME wkc2_pamf COMMAND wkc2_pamf_test)
    set_tests_properties(wkc2_pamf PROPERTIES TIMEOUT 10)
    add_executable(wkc2_lwcond_test tests/lwcond_test.cpp lwcond.cpp)
    target_include_directories(wkc2_lwcond_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    target_link_libraries(wkc2_lwcond_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_lwcond COMMAND wkc2_lwcond_test)
    set_tests_properties(wkc2_lwcond PROPERTIES TIMEOUT 10)
    set(LWMUTEX_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/lwmutex-fixture.inc")
    add_custom_command(OUTPUT "${LWMUTEX_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_lwmutex_fixture.py"
            --source "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp" --output "${LWMUTEX_FIXTURE}"
        DEPENDS "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_lwmutex_fixture.py"
        VERBATIM)
    add_executable(wkc2_lwmutex_test tests/lwmutex_test.cpp lwcond.cpp "${LWMUTEX_FIXTURE}")
    target_include_directories(wkc2_lwmutex_test PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}" "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include"
        "${PS3RECOMP_DIR}/runtime/ppu" "${PS3RECOMP_DIR}/runtime/platform")
    target_compile_features(wkc2_lwmutex_test PRIVATE cxx_std_20)
    target_link_libraries(wkc2_lwmutex_test PRIVATE Threads::Threads)
    if(NOT WIN32)
        target_sources(wkc2_lwmutex_test PRIVATE "${PS3RECOMP_DIR}/runtime/platform/win32_compat.c")
        target_link_libraries(wkc2_lwmutex_test PRIVATE m)
    endif()
    add_test(NAME wkc2_lwmutex_user COMMAND wkc2_lwmutex_test)
    add_test(NAME wkc2_lwmutex_kernel COMMAND wkc2_lwmutex_test kernel)
    set_tests_properties(wkc2_lwmutex_user wkc2_lwmutex_kernel PROPERTIES TIMEOUT 20)
    add_executable(wkc2_memory_test tests/memory_test.c
        "${PS3RECOMP_DIR}/runtime/syscalls/sys_memory.c")
    target_include_directories(wkc2_memory_test PRIVATE "${PS3RECOMP_DIR}/runtime/syscalls")
    target_link_libraries(wkc2_memory_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_memory COMMAND wkc2_memory_test)
    set_tests_properties(wkc2_memory PROPERTIES TIMEOUT 10)
    add_executable(wkc2_platform_queries_test tests/platform_queries_test.c)
    target_include_directories(wkc2_platform_queries_test PRIVATE "${PS3RECOMP_DIR}/runtime/syscalls" "${PS3RECOMP_DIR}/runtime/ppu")
    target_link_libraries(wkc2_platform_queries_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_platform_queries COMMAND wkc2_platform_queries_test)
    add_executable(wkc2_ppu_thread_lifetime_test tests/ppu_thread_lifetime_test.c
        "${PS3RECOMP_DIR}/runtime/syscalls/sys_ppu_thread.c")
    target_include_directories(wkc2_ppu_thread_lifetime_test PRIVATE
        "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/runtime/syscalls")
    target_link_libraries(wkc2_ppu_thread_lifetime_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_ppu_thread_lifetime COMMAND wkc2_ppu_thread_lifetime_test)
    set_tests_properties(wkc2_ppu_thread_lifetime PROPERTIES TIMEOUT 30)
    set(SPU_ASYNC_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/spu_async_fixture.inc")
    add_custom_command(OUTPUT "${SPU_ASYNC_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_spu_async_fixture.py"
            "${PS3RECOMP_DIR}" "${SPU_ASYNC_FIXTURE}"
        DEPENDS "${PS3RECOMP_DIR}/runtime/spu/spu_workload.c"
            "${PS3RECOMP_DIR}/runtime/spu/spu_async_lifetime.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_spu_async_fixture.py"
        VERBATIM)
    add_executable(wkc2_spu_async_lifetime_test tests/spu_async_lifetime_test.c "${SPU_ASYNC_FIXTURE}"
        "${PS3RECOMP_DIR}/runtime/spu/spu_coherency.c")
    target_include_directories(wkc2_spu_async_lifetime_test PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}" "${PS3RECOMP_DIR}/runtime/spu")
    if(MSVC)
        target_compile_options(wkc2_spu_async_lifetime_test PRIVATE /experimental:c11atomics)
    endif()
    add_test(NAME wkc2_spu_async_lifetime COMMAND wkc2_spu_async_lifetime_test)
    set_tests_properties(wkc2_spu_async_lifetime PROPERTIES TIMEOUT 15)
    add_executable(wkc2_lfqueue_test tests/lfqueue_test.cpp lfqueue.cpp)
    target_include_directories(wkc2_lfqueue_test PRIVATE "${RECOMP_DIR}" "${PS3RECOMP_DIR}/include")
    target_link_libraries(wkc2_lfqueue_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_lfqueue COMMAND wkc2_lfqueue_test)
    set_tests_properties(wkc2_lfqueue PROPERTIES TIMEOUT 10)
    add_executable(wkc2_event_queue_test tests/event_queue_test.cpp
        "${PS3RECOMP_DIR}/runtime/syscalls/sys_event.c")
    target_include_directories(wkc2_event_queue_test PRIVATE "${PS3RECOMP_DIR}/runtime/syscalls")
    target_link_libraries(wkc2_event_queue_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_event_queue COMMAND wkc2_event_queue_test)
    set_tests_properties(wkc2_event_queue PROPERTIES TIMEOUT 10)
endif()
