file(GLOB DIR_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.c)

set(SOURCES ${DIR_SRCS})

add_executable(demo ${SOURCES})

message(${CMAKE_CURRENT_LIST_DIR} )
target_include_directories(demo 
	PUBLIC 
	${CMAKE_CURRENT_LIST_DIR}/
	${CMAKE_CURRENT_LIST_DIR}/../include/)

target_link_libraries(demo PUBLIC ${LIBNAX})

target_link_libraries(demo PUBLIC dl pthread rt)