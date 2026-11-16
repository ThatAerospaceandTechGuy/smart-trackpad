cmake_minimum_required(VERSION 3.21)

project(SmartTrackpad VERSION 1.2.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SmartTrackpad is a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Resource compiler for the icon, version block and (MSVC) the manifest.
enable_language(RC)

set(SMARTTRACKPAD_SOURCES
    src/main.cpp
    src/App.cpp
    src/Actions.cpp
    src/Brightness.cpp
    src/Config.cpp
    src/CrashHandler.cpp
    src/GestureEngine.cpp
    src/HidDecode.cpp
    src/Install.cpp
    src/Log.cpp
    src/Osd.cpp
    src/RawInput.cpp
    src/TrayIcon.cpp
    src/Volume.cpp
    src/Resources.rc
)

add_executable(SmartTrackpad WIN32 ${SMARTTRACKPAD_SOURCES})

if(MSVC)
    # Embedded application manifest (DPI awareness, long paths, asInvoker).
    target_sources(SmartTrackpad PRIVATE src/app.manifest)
endif()

target_include_directories(SmartTrackpad PRIVATE src)
target_compile_features(SmartTrackpad PRIVATE cxx_std_20)
target_compile_definitions(SmartTrackpad PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    SMARTTRACKPAD_VERSION=L"${PROJECT_VERSION}"
)
target_link_libraries(SmartTrackpad PRIVATE
    user32
    hid
    dxva2
    shell32
    wbemuuid
    ole32
    oleaut32
    gdiplus
    dwmapi
    dbghelp
    uuid
)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT SmartTrackpad)

if(MSVC)
    set_property(TARGET SmartTrackpad PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(SmartTrackpad PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(SmartTrackpad PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(SmartTrackpad PRIVATE -municode)
    endif()
endif()

option(SMARTTRACKPAD_BUILD_TESTS "Build the unit tests" ON)

if(SMARTTRACKPAD_BUILD_TESTS)
    enable_testing()

    # Console test binary from the given sources; each one registers itself
    # with CTest under the target name.
    function(smarttrackpad_add_test name)
        add_executable(${name} ${ARGN})
        target_include_directories(${name} PRIVATE src)
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_compile_definitions(${name} PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )

        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8
                                                   /Zc:__cplusplus)
        else()
            target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    # Zone/gesture state machine (no Win32 dependency).
    smarttrackpad_add_test(gesture_engine
        tests/GestureEngineTests.cpp
        src/GestureEngine.cpp
    )

    # HID value decoding and contact-state combination.
    smarttrackpad_add_test(hid_decode
        tests/HidDecodeTests.cpp
        src/HidDecode.cpp
    )

    # INI parsing, clamping and legacy-schema migration.
    smarttrackpad_add_test(config
        tests/ConfigTests.cpp
        src/Config.cpp
        src/Log.cpp
    )
endif()
