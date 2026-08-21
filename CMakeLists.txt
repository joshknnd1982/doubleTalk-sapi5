cmake_minimum_required(VERSION 3.15)
project(DoubleTalkSAPI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DT_ARCH "x64")
else()
    set(DT_ARCH "x86")
endif()
message(STATUS "DoubleTalk SAPI5: building ${DT_ARCH}")

# The DoubleTalk emulator ships as a prebuilt DLL for BOTH architectures
# (dtalk.dll / dtalk64.dll) and is loaded at runtime by name, so - unlike the
# BestSpeech wrapper this project grew out of - there is no 32-bit helper
# process, no named pipe, and no architecture-specific code path at all.
set(DT_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")

# ---------------------------------------------------------------- core

add_library(dt_core STATIC
    dtsrc/dtalk_api.cpp
    dtsrc/dt_engine.cpp
    dtsrc/dt_settings.cpp
    dtsrc/dt_log.cpp
)

target_include_directories(dt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dtsrc)

target_link_libraries(dt_core PUBLIC shlwapi ole32)

target_compile_definitions(dt_core PUBLIC
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# ------------------------------------------------------- offline renderer

# Console tool that renders text to a WAV using exactly the same code path the
# SAPI engine uses. This is the harness the voice and parameter samples are
# generated with, and it is what to reach for first when diagnosing whether a
# problem is in the engine or in the SAPI plumbing.
add_executable(dt_render dtsrc/dt_render.cpp)
target_link_libraries(dt_render PRIVATE dt_core)

# ------------------------------------------------------------ SAPI5 engine

# One DLL per architecture, same sources. A 32-bit host loads the x86 build and
# a 64-bit host the x64 build; both drive the emulator in-process.
add_library(DoubleTalkSAPI SHARED
    dtsrc/sapi_main.cpp
    dtsrc/com.cpp
    dtsrc/registry.cpp
    dtsrc/ISpDataKeyImpl.cpp
    dtsrc/IEnumSpObjectTokensImpl.cpp
    dtsrc/ISpTTSEngineImpl.cpp
    dtsrc/voice_token.cpp
    dtsrc/doubletalk_sapi.def
)

target_link_libraries(DoubleTalkSAPI PRIVATE
    dt_core
    ole32
    oleaut32
    advapi32
    shell32
)

# ------------------------------------------------------- config utility

add_executable(DoubleTalkConfig WIN32
    dtconfig/dtconfig.cpp
    dtconfig/dtconfig.rc
)
target_include_directories(DoubleTalkConfig PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dtconfig)
target_link_libraries(DoubleTalkConfig PRIVATE dt_core comctl32 winmm shell32)

# ------------------------------------------------------ SAPI end-to-end test

# Exercises the real SAPI stack while creating the voice token from our own
# enumerator, so the engine can be tested without an elevated (HKLM) install.
add_executable(dt_sapitest dtsrc/dt_sapitest.cpp)
target_link_libraries(dt_sapitest PRIVATE ole32 oleaut32)
target_compile_definitions(dt_sapitest PRIVATE
    _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# ---------------------------------------------------------------- warnings

if(MSVC)
    foreach(tgt dt_core dt_render DoubleTalkSAPI dt_sapitest DoubleTalkConfig)
        target_compile_options(${tgt} PRIVATE /W4 /MP /permissive-)
        # Static CRT: the SAPI DLL is loaded into arbitrary host processes
        # (screen readers, book readers), and depending on a redistributable
        # being present in each of them is a support burden with no upside.
        set_property(TARGET ${tgt} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
endif()

# ------------------------------------------------- engine payload staging

# Put the emulator DLL and the ROM next to the built binaries so everything
# runs straight from the build tree without an install step.
set(DT_PAYLOAD
    "${DT_ENGINE_DIR}/dtalk.dll"
    "${DT_ENGINE_DIR}/dtalk64.dll"
    "${DT_ENGINE_DIR}/doubletalkpc.bin"
)

foreach(f ${DT_PAYLOAD})
    if(EXISTS "${f}")
        get_filename_component(fname "${f}" NAME)
        add_custom_command(TARGET DoubleTalkSAPI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${f}" "$<TARGET_FILE_DIR:dt_render>/${fname}"
            COMMENT "Staging ${fname}")
    else()
        message(WARNING "Engine payload missing: ${f}")
    endif()
endforeach()
