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

set(SMARTTRACKPAD_COMMON_SOURCES
    src/common/Config.cpp
    src/common/ContactTracker.cpp
    src/common/GestureEngine.cpp
    src/common/HidDecode.cpp
    src/common/IniFile.cpp
    src/common/Log.cpp
    src/common/PathCompare.cpp
    src/common/Platform.cpp
    src/common/Text.cpp
)

set(SMARTTRACKPAD_SOURCES
    src/windows/main.cpp
    src/windows/App.cpp
    src/windows/Actions.cpp
    src/windows/Brightness.cpp
    src/windows/ConfigWatcherWin.cpp
    src/windows/CrashHandler.cpp
    src/windows/Diagnostics.cpp
    src/windows/Install.cpp
    src/windows/Osd.cpp
    src/windows/RawInput.cpp
    src/windows/Settings.cpp
    src/windows/Strings.cpp
    src/windows/TrayIcon.cpp
    src/windows/Volume.cpp
    src/windows/Resources.rc
    ${SMARTTRACKPAD_COMMON_SOURCES}
)

add_executable(SmartTrackpad WIN32 ${SMARTTRACKPAD_SOURCES})

if(MSVC)
    # Embedded application manifest (DPI awareness, long paths, asInvoker).
    target_sources(SmartTrackpad PRIVATE src/windows/app.manifest)
endif()

target_include_directories(SmartTrackpad PRIVATE src src/common src/windows)
target_compile_features(SmartTrackpad PRIVATE cxx_std_20)
target_compile_definitions(SmartTrackpad PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
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
    comctl32
    wtsapi32
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
        target_include_directories(${name} PRIVATE src src/common src/windows)
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_compile_definitions(${name} PRIVATE
            UNICODE
            _UNICODE
            _CRT_SECURE_NO_WARNINGS
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
        src/common/GestureEngine.cpp
    )

    # HID value decoding and contact-state combination.
    smarttrackpad_add_test(hid_decode
        tests/HidDecodeTests.cpp
        src/common/HidDecode.cpp
    )

    # Path identity / timestamp rules used by the installer.
    smarttrackpad_add_test(path_compare
        tests/PathCompareTests.cpp
        src/common/PathCompare.cpp
    )

    # Digitizer report lifecycle (one contact, multi-finger cancel, removal).
    smarttrackpad_add_test(contact_tracker
        tests/ContactTrackerTests.cpp
        src/common/ContactTracker.cpp
    )

    # INI parsing, clamping and legacy-schema migration.
    smarttrackpad_add_test(config
        tests/ConfigTests.cpp
        src/common/Config.cpp
        src/common/IniFile.cpp
        src/common/Log.cpp
        src/common/Platform.cpp
        src/common/Text.cpp
    )
endif()
