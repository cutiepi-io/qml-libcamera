cmake_minimum_required(VERSION 3.14)

project(qmllibcameraplugin)

# Specify the required Qt modules
find_package(Qt5 REQUIRED COMPONENTS Core Qml Quick)

# Set the C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add the source files
set(SOURCES plugin.cpp)

# If you're building a library
add_library(qmllibcameraplugin ${SOURCES})

# Link the required Qt modules
target_link_libraries(qmllibcameraplugin PRIVATE Qt5::Core Qt5::Qml Qt5::Quick)

# For pkg-config integration
find_package(PkgConfig REQUIRED)

# Check for the required package
pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)

# Link the external library
target_link_libraries(qmllibcameraplugin PRIVATE PkgConfig::LIBCAMERA)

# Set the destination directory for the library
set_target_properties(qmllibcameraplugin PROPERTIES
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LibCamera")
