cmake_minimum_required(VERSION 3.20)
project(WKC2Native
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Recompiled PS3 game project (ps3recomp)"
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
#         -DRECOMP_DIR=/path/to/my_game/recompiled
#
if(NOT DEFINED PS3RECOMP_DIR)
    # Default: this project is still sitting inside ps3recomp/templates/project/
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
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)

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
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
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

add_executable(${PROJECT_NAME}
    main.cpp
    stubs.cpp
    lwcond.cpp
    lfqueue.cpp
    pamf.cpp
    dmux.cpp
    vpost.cpp
    vdec.cpp
    adec.cpp
    thread_diagnostics.cpp
    graphics_diagnostics.cpp
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)

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

# app.rc carries the icon, the version block and the manifest, and needs a
# resource compiler. Listing it as a source on a host that has none is not a
# link error, it is a "no rule to make" for a file nothing can consume.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
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
    find_package(Threads REQUIRED)
    if(WIN32)
        add_executable(wkc2_spurs_job_test tests/spurs_job_test.c
            "${PS3RECOMP_DIR}/libs/spurs/cellSpurs.c")
        target_include_directories(wkc2_spurs_job_test PRIVATE
            "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/spurs"
            "${PS3RECOMP_DIR}/runtime/spu" "${PS3RECOMP_DIR}/runtime/ppu")
        add_test(NAME wkc2_spurs_job COMMAND wkc2_spurs_job_test)
        set_tests_properties(wkc2_spurs_job PROPERTIES TIMEOUT 20)
    endif()
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
    add_executable(wkc2_memory_test tests/memory_test.c
        "${PS3RECOMP_DIR}/runtime/syscalls/sys_memory.c")
    target_include_directories(wkc2_memory_test PRIVATE "${PS3RECOMP_DIR}/runtime/syscalls")
    target_link_libraries(wkc2_memory_test PRIVATE Threads::Threads)
    add_test(NAME wkc2_memory COMMAND wkc2_memory_test)
    set_tests_properties(wkc2_memory PROPERTIES TIMEOUT 10)
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
