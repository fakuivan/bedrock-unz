cmake_minimum_required(VERSION 3.10)

project(bedrock-unz)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(ZSTD_BUILD_PROGRAMS OFF)
add_subdirectory(zstd/build/cmake)

set(ZLIB_BUILD_EXAMPLES OFF)
add_subdirectory(zlib)

# workaround for https://github.com/google/leveldb/issues/891
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

add_subdirectory(leveldb-mcpe)

add_executable(main main.cpp)
include_directories(leveldb-mcpe/include)
add_compile_definitions(DLLX=)
target_link_libraries(main PRIVATE leveldb pthread)
target_include_directories(main PRIVATE leveldb/include)
