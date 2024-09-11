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

# taken from https://stackoverflow.com/a/20239564
set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "Build leveldb tests" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "Build leveldb benchmarks" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "Install leveldb library" FORCE)
add_subdirectory(leveldb)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE leveldb)
target_include_directories(main PRIVATE leveldb/include)
