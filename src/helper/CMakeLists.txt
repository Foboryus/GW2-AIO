cmake_minimum_required(VERSION 3.16)
project(GW2AIOHelper VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build as DLL
add_library(GW2AIOHelper SHARED
    GW2AIOHelper.cpp
)

# No dependencies needed - pure Win32
target_link_libraries(GW2AIOHelper PRIVATE
    # Just Windows API
)

# Minimize DLL size
if(MSVC)
    target_compile_options(GW2AIOHelper PRIVATE /O2 /GL)
    target_link_options(GW2AIOHelper PRIVATE /LTCG /OPT:REF /OPT:ICF)
endif()

# Output to lib/ subdirectory (alongside main app exe and Qt DLLs)
# Using _RELEASE suffix to avoid MSVC multi-config adding extra Release/ subdirectory
set_target_properties(GW2AIOHelper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/lib"
)
