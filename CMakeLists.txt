cmake_minimum_required(VERSION 3.14)

project(qmllibcameraplugin)
find_package(Qt5 REQUIRED COMPONENTS Core Qml Quick)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)

set(SOURCES plugin.cpp)

add_library(qmllibcameraplugin SHARED ${SOURCES})

target_link_libraries(qmllibcameraplugin PRIVATE Qt5::Core Qt5::Qml Qt5::Quick PkgConfig::LIBCAMERA)

set_target_properties(qmllibcameraplugin PROPERTIES
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LibCamera")

add_custom_command(TARGET qmllibcameraplugin POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy
                           ${CMAKE_SOURCE_DIR}/LibCamera/qmldir
                           ${CMAKE_BINARY_DIR}/LibCamera/qmldir)
