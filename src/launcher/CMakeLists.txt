cmake_minimum_required(VERSION 3.20)
project(GW2AIOLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate Windows resource file with correct icon path
# Use CMAKE_SOURCE_DIR (project root) because app.rc.in references
# @CMAKE_CURRENT_SOURCE_DIR@/resources/icons/ which must resolve to the root
if(WIN32)
    set(CMAKE_CURRENT_SOURCE_DIR_BACKUP ${CMAKE_CURRENT_SOURCE_DIR})
    set(CMAKE_CURRENT_SOURCE_DIR ${CMAKE_SOURCE_DIR})
    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/app.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
        @ONLY
    )
    set(CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR_BACKUP})
endif()

# Launcher stub — pure Win32, no Qt dependency
add_executable(GW2AIO WIN32
    main.cpp
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_BINARY_DIR}/app.rc>
)

# No libraries needed — pure Windows API
# (kernel32 and user32 are linked implicitly by MSVC)

# Require admin elevation (same as main app for consistency)
if(WIN32 AND MSVC)
    set_target_properties(GW2AIO PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
    # Minimize stub size
    target_compile_options(GW2AIO PRIVATE /O2 /GL)
    target_link_options(GW2AIO PRIVATE /LTCG /OPT:REF /OPT:ICF)
endif()

# Output to build/Release/ (root — where users launch from)
# Using _RELEASE suffix to avoid MSVC multi-config adding extra Release/ subdirectory
set_target_properties(GW2AIO PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)
