project(util)

target_include_directories(util PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(util PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/./msg_object.cc
		${CMAKE_CURRENT_SOURCE_DIR}/src/./timer.cc)
