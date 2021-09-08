cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(util)

set(CMAKE_DEBUG_POSTFIX d)

# 指定c++标准: 默认是 c++11
add_library(util_compiler_flags INTERFACE)
target_compile_features(util_compiler_flags INTERFACE cxx_std_11)
# 设置 c++ 警告标志
set(gcc_like_cxx "$<COMPILE_LANG_AND_ID:CXX,ARMClang,AppleClang,Clang,GNU>")
set(msvc_cxx "$<COMPILE_LANG_AND_ID:CXX,MSVC>")

target_compile_options(util_compiler_flags INTERFACE
		$<${gcc_like_cxx}:$<BUILD_INTERFACE:-Wall;-Wextra;-Wshadow;-Wformat=2;-Wunused>>
		$<${msvc_cxx}:$<BUILD_INTERFACE:-W3>>)

# 设置生成文件输出路径
set(LIBRARY_OUTPUT_PATH, ./output/debug/lib)
add_library(util STATIC "")

target_include_directories(util PUBLIC ./inc/)
target_include_directories(util PRIVATE ./extern_inc/)

target_link_directories(util PRIVATE ./lib/debug/)

include(src/CMakeLists.txt)

