file(GLOB DIR_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.cc)

set(SOURCES ${DIR_SRCS})
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(GTest REQUIRED)

add_executable(naxtest ${SOURCES})

target_include_directories(naxtest 
	PUBLIC 
	${CMAKE_CURRENT_LIST_DIR}/
	${CMAKE_CURRENT_LIST_DIR}/../include/
	${GTEST_INCLUDE_DIRS})

target_link_libraries(naxtest PUBLIC ${LIBNAX})
target_link_libraries(naxtest PUBLIC dl pthread rt ${GTEST_BOTH_LIBRARIES})#gtest gtest_main)
