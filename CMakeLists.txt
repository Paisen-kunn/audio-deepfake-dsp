cmake_minimum_required(VERSION 3.20)
project(AudioForensics VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(forensic_core src/audio_core.cpp)
target_include_directories(forensic_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
    target_compile_options(forensic_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(forensic_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(forensic_core PRIVATE mfplat mfreadwrite mfuuid)
    add_executable(audio_forensics WIN32 src/platform_windows.cpp)
    if(MSVC)
        target_compile_options(audio_forensics PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_link_libraries(audio_forensics PRIVATE forensic_core comdlg32 winmm)
elseif(APPLE)
    enable_language(OBJCXX)
    target_link_libraries(forensic_core PRIVATE "-framework AudioToolbox")
    add_executable(audio_forensics MACOSX_BUNDLE src/platform_macos.mm)
    target_compile_options(audio_forensics PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(audio_forensics PRIVATE
        forensic_core "-framework Cocoa" "-framework UniformTypeIdentifiers")
else()
    message(FATAL_ERROR "This native GUI currently supports Windows and macOS.")
endif()

add_custom_command(TARGET audio_forensics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:audio_forensics>/data")
add_custom_command(TARGET audio_forensics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ATTRIBUTION.md" "$<TARGET_FILE_DIR:audio_forensics>/ATTRIBUTION.md")

if(APPLE)
    set_target_properties(audio_forensics PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Audio Forensics"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.audioforensics.dsp"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(core_tests tests_cpp/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE forensic_core)
    add_test(NAME core_tests COMMAND core_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/demo_dataset")
endif()
