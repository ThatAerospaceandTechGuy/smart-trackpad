cmake_minimum_required(VERSION 3.21)

project(SmartTrackpad VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Shared core: gesture logic, configuration, logging, paths.  Identical on every
# platform and covered by the test binaries below.
# ---------------------------------------------------------------------------
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

set(SMARTTRACKPAD_INCLUDE_DIRS src src/common)

if(WIN32)
    # Resource compiler for the icon, version block and (MSVC) the manifest.
    enable_language(RC)

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
elseif(APPLE)
    enable_language(OBJCXX)

    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(CORE_AUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(IO_KIT_FRAMEWORK IOKit REQUIRED)
    find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

    set(SMARTTRACKPAD_SOURCES
        src/common/CrashHandlerPosix.cpp
        src/macos/main.mm
        src/macos/AppMac.mm
        src/macos/InputMultitouch.mm
        src/macos/AudioMac.mm
        src/macos/BrightnessMac.mm
        src/macos/OverlayMac.mm
        src/macos/ConfigWatcherMac.cpp
        src/macos/LoginItemMac.mm
        ${SMARTTRACKPAD_COMMON_SOURCES}
    )
else()
    find_package(X11)
    set(SMARTTRACKPAD_SOURCES
        src/common/CrashHandlerPosix.cpp
        src/linux/main.cpp
        src/linux/AppLinux.cpp
        src/linux/AudioLinux.cpp
        src/linux/BrightnessLinux.cpp
        src/linux/ConfigWatcherLinux.cpp
        src/linux/InputEvdev.cpp
        src/linux/OsdLinux.cpp
        src/linux/ZoneTestLinux.cpp
        ${SMARTTRACKPAD_COMMON_SOURCES}
    )
endif()

add_executable(SmartTrackpad ${SMARTTRACKPAD_SOURCES})

if(WIN32)
    set_target_properties(SmartTrackpad PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        # Embedded application manifest (DPI awareness, long paths, asInvoker).
        target_sources(SmartTrackpad PRIVATE src/windows/app.manifest)
    endif()

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
elseif(APPLE)
    set_target_properties(SmartTrackpad PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Smart Trackpad"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.smarttrackpad.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/macos/Info.plist.in"
    )
    target_compile_definitions(SmartTrackpad PRIVATE
        SMARTTRACKPAD_VERSION=L"${PROJECT_VERSION}"
        SMARTTRACKPAD_VERSION_TEXT="${PROJECT_VERSION}"
    )
    target_link_libraries(SmartTrackpad PRIVATE
        ${COCOA_FRAMEWORK}
        ${CORE_AUDIO_FRAMEWORK}
        ${IO_KIT_FRAMEWORK}
        ${APPLICATION_SERVICES_FRAMEWORK}
        ${CORE_FOUNDATION_FRAMEWORK}
    )
else()
    target_compile_definitions(SmartTrackpad PRIVATE
        SMARTTRACKPAD_VERSION=L"${PROJECT_VERSION}"
        SMARTTRACKPAD_VERSION_TEXT="${PROJECT_VERSION}"
    )
    find_package(Threads REQUIRED)
    target_link_libraries(SmartTrackpad PRIVATE Threads::Threads)
    if(X11_FOUND)
        target_include_directories(SmartTrackpad PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(SmartTrackpad PRIVATE X11::X11)
        target_compile_definitions(SmartTrackpad PRIVATE SMARTTRACKPAD_HAVE_X11)
    else()
        message(WARNING
            "X11 development files were not found: the level overlay will be "
            "compiled out. Install libx11-dev to get it.")
    endif()
endif()

target_include_directories(SmartTrackpad PRIVATE ${SMARTTRACKPAD_INCLUDE_DIRS})
target_compile_features(SmartTrackpad PRIVATE cxx_std_20)

if(MSVC)
    set_property(TARGET SmartTrackpad PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(SmartTrackpad PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
elseif(APPLE)
    target_compile_options(SmartTrackpad PRIVATE -Wall -Wextra -Wpedantic
        -fobjc-arc)
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
        target_include_directories(${name} PRIVATE ${SMARTTRACKPAD_INCLUDE_DIRS})
        target_compile_features(${name} PRIVATE cxx_std_20)

        if(WIN32)
            target_compile_definitions(${name} PRIVATE
                UNICODE
                _UNICODE
                _CRT_SECURE_NO_WARNINGS
                WIN32_LEAN_AND_MEAN
                NOMINMAX
            )
        endif()

        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8
                                                   /Zc:__cplusplus)
        else()
            target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
            find_package(Threads REQUIRED)
            target_link_libraries(${name} PRIVATE Threads::Threads)
        endif()

        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    # Zone/gesture state machine.
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
