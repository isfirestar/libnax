file(GLOB
    DIR_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/posix/*.c
    ${CMAKE_CURRENT_LIST_DIR}/posix/wosi/*.c)

set(SOURCES ${DIR_SRCS})
set(CMAKE_C_VISIBILITY_PRESET hidden)

add_library(nax SHARED ${SOURCES})

target_include_directories(nax 
			PRIVATE 
			${CMAKE_CURRENT_LIST_DIR}/../include/
			PUBLIC
			${CMAKE_CURRENT_LIST_DIR}/posix/
			)

target_compile_definitions(nax PUBLIC _GNU_SOURCE)
