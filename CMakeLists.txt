cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(util)

set(CMAKE_DEBUG_POSTFIX d)

set(CXX_FLAGS
    -g
    # -DVALGRIND
    -DCHECK_PTHREAD_RETURN_VALUE
    -D_FILE_OFFSET_BITS=64
    -march=native
    # -MMD
    -rdynamic
    )
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

# 设置生成文件输出路径
set(LIBRARY_OUTPUT_PATH, ./output/debug/lib)
add_library(util STATIC "")

target_include_directories(util PUBLIC ./inc/)
target_include_directories(util PRIVATE ./extern_inc/)

target_link_directories(util PRIVATE ./lib/debug/)

target_link_libraries(util PRIVATE gtest)
target_link_libraries(util PRIVATE protocold)
target_link_libraries(util PRIVATE systemd)
target_link_libraries(util PRIVATE basicd)
target_link_libraries(util PRIVATE pthread)
include(src/CMakeLists.txt)

