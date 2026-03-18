# CMake for unit tests
cmake_minimum_required(VERSION 3.20)

project(GW2AIOTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt
find_package(Qt6 REQUIRED COMPONENTS Core Test Network)

# Include main source directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Test executable
add_executable(GW2AIOTests
    tests.cpp
)

target_link_libraries(GW2AIOTests PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Network
)

# Enable testing
enable_testing()
add_test(NAME GW2AIOTests COMMAND GW2AIOTests)

# Copy test data if needed
# file(COPY testdata DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
