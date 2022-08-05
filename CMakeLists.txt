# use command "cmake . -DCMAKE_BUILD_TYPE=Debug" to generat debug binary file
# use command "cmake .. -DCMAKE_USER_ARCH=arm" to generat arm binary file
cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

project(nax VERSION 9.9.1)
configure_file(cmake/naxConfig.h.in naxConfig.h)

include(cmake/FindGTest.cmake)

set(CMAKE_C_FLAGS_DEBUG "$ENV{CFLAGS} -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -DDEBUG=1")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CPPFLAGS} -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -DDEBUG=1")
set(CMAKE_C_FLAGS_RELEASE "$ENV{CFLAGS} -O2 -g -funroll-loops -D NO_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CPPFLAGS} -O2 -g -funroll-loops -D NO_DEBUG")

if (DEFINED CMAKE_USER_ARCH)
	message("Building arch: ${CMAKE_USER_ARCH}")
	if (CMAKE_USER_ARCH STREQUAL "arm")
		set(CMKAE_SYSTEM_NAME Linux)
		set(CMAKE_C_COMPILER /srv/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc)
		set(CMAKE_CXX_COMPILER /srv/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-g++)
		set(CMAKE_FIND_ROOT_PATH /srv/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/)
		set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
		set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
		set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
	endif()
endif()

if (DEFINED MY_CONTEXT)
	message("${MY_CONTEXT}")
	set(SOME_NUMBER)
	string(FIND ${MY_CONTEXT} "34" SOME_NUMBER)
	message("${SOME_NUMBER}")
endif()

option(ENABLE_LIBNAX  "enable libnax" ON)
if (ENABLE_LIBNAX)
	add_subdirectory(src)
	list(APPEND LIBNAX nax)
endif()

option(ENABLE_DEMO "enable demo program" ON)
if (ENABLE_DEMO)
	add_subdirectory(demo)
endif()

option(ENABLE_TEST "enable google-test" ON)
if (ENABLE_TEST)
	add_subdirectory(test)
endif()
