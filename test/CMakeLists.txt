file(GLOB DIR_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.cc)

set(SOURCES ${DIR_SRCS})
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(naxtest ${SOURCES})

# message(${CMAKE_CURRENT_LIST_DIR} )

target_include_directories(naxtest 
	PUBLIC 
	${CMAKE_CURRENT_LIST_DIR}/
	${CMAKE_CURRENT_LIST_DIR}/../include/)

target_link_libraries(naxtest PUBLIC ${LIBNAX})
target_link_libraries(naxtest PUBLIC dl pthread rt gtest gtest_main)