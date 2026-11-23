cmake_minimum_required(VERSION 3.10)

project(util VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_DEBUG_POSTFIX d)

# Compiler flags
set(CXX_FLAGS
    -g
    -ggdb
    -DCHECK_PTHREAD_RETURN_VALUE
    -D_FILE_OFFSET_BITS=64
    -march=native
    -rdynamic
    -O0 -Wall
    -DDEBUG
)
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

# Output directories
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/output/debug/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/output/debug/lib)

# Source files
file(GLOB_RECURSE SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cc
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.cxx
    ${CMAKE_SOURCE_DIR}/src/*.c
)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/inc)
include_directories(${CMAKE_SOURCE_DIR}/extern_inc)
include_directories(${CMAKE_SOURCE_DIR}/src)

# Link directories
link_directories(${CMAKE_SOURCE_DIR}/lib/debug)

# Build targets
add_library(util STATIC
    ${SRC_FILES}
)

add_executable(Test_MsgBus
    ${CMAKE_SOURCE_DIR}/main/Test_MsgBus.cc
    ${SRC_FILES}
)

add_executable(Test_Timer
    ${CMAKE_SOURCE_DIR}/main/Test_Timer.cc
    ${SRC_FILES}
)

# Include dirs for target util
target_include_directories(util PRIVATE ${CMAKE_SOURCE_DIR}/inc)
target_include_directories(util PRIVATE ${CMAKE_SOURCE_DIR}/extern_inc)
target_include_directories(util PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Include dirs for target Test_MsgBus
target_include_directories(Test_MsgBus PRIVATE ${CMAKE_SOURCE_DIR}/inc)
target_include_directories(Test_MsgBus PRIVATE ${CMAKE_SOURCE_DIR}/extern_inc)
target_include_directories(Test_MsgBus PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Include dirs for target Test_Timer
target_include_directories(Test_Timer PRIVATE ${CMAKE_SOURCE_DIR}/inc)
target_include_directories(Test_Timer PRIVATE ${CMAKE_SOURCE_DIR}/extern_inc)
target_include_directories(Test_Timer PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Legacy dependencies
target_link_libraries(util
    protocol
    algorithm
    basic
    system
    ncurses++
    form
    menu
    panel
    ncurses
    gtest
    pthread
)
target_link_libraries(Test_MsgBus
    protocol
    algorithm
    basic
    system
    ncurses++
    form
    menu
    panel
    ncurses
    gtest
    pthread
)
target_link_libraries(Test_Timer
    protocol
    algorithm
    basic
    system
    ncurses++
    form
    menu
    panel
    ncurses
    gtest
    pthread
)

