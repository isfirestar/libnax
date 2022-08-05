file(GLOB
    DIR_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/../*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/wosi/*.c)

set(SOURCES ${DIR_SRCS})
set(CMAKE_C_VISIBILITY_PRESET hidden)

add_library(nax SHARED ${SOURCES})

target_include_directories(nax
			PRIVATE
			${CMAKE_CURRENT_LIST_DIR}/../../include/
			PUBLIC
			"${PROJECT_BINARY_DIR}"
			${CMAKE_CURRENT_LIST_DIR}
			)

target_compile_definitions(nax PUBLIC _GNU_SOURCE)

# using cmake relation code for example : version export method
target_compile_definitions(nax PRIVATE _GENERATE_BY_CMAKE)
