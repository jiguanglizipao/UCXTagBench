cmake_minimum_required(VERSION 3.1)
project(UCXTagBench C CXX)

set (CMAKE_CXX_STANDARD 14)
set(THREADS_PREFER_PTHREAD_FLAG ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(PROJECT_DEPS_ThreadPool_DIR ${PROJECT_SOURCE_DIR}/deps/ThreadPool)
include_directories(${PROJECT_SOURCE_DIR} ${PROJECT_DEPS_ThreadPool_DIR})

find_package(PkgConfig)
pkg_check_modules(UCX REQUIRED ucx)
include_directories(${PROJECT_SOURCE_DIR} ${UCX_INCLUDE_DIRS})
link_directories(${UCX_LIBRARY_DIRS})

find_package(Threads REQUIRED)

add_compile_options(-DMUTEX=${MUTEX} -DSHARE_WORKER=${SHARE_WORKER})

add_executable(client client.cpp)
add_executable(server server.cpp)

target_link_libraries(client ${UCX_LIBRARIES} Threads::Threads)
target_link_libraries(server ${UCX_LIBRARIES} Threads::Threads)
