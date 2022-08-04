cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

project(nax VERSION 9.0)

add_subdirectory(src)
list(APPEND LIBNAX nax)

option(ENABLE_DEMO "enable demo program" ON)
if (ENABLE_DEMO)
	add_subdirectory(demo)
endif()

option(ENABLE_TEST "enable google-test" OFF)
if (ENABLE_TEST)
	add_subdirectory(test)
endif()