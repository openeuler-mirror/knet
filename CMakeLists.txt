cmake_minimum_required(VERSION 3.14.1)
project(K-NET C)
set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Werror -Wall")

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    add_compile_options(-march=armv8-a+crc)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
    add_compile_options(-msse4.2)
endif()

#设置头文件依赖路径
set(PROJECT_DEPENDS_INC_DIR ${PROJECT_BINARY_DIR}/opensource/include)

#设置库文件依赖路径
set(PROJECT_DEPENDS_LIB_DIR ${PROJECT_BINARY_DIR}/opensource/lib)

#设置头文件输出路径
set(PROJECT_OUT_INC_DIR ${PROJECT_BINARY_DIR}/include)

#设置库文件输出路径
set(PROJIECT_OUT_LIB_DIR ${PROJECT_BINARY_DIR}/lib)

set(LIBRARY_OUTPUT_PATH ${PROJIECT_OUT_LIB_DIR})

if (${KNET_BUILD_TYPE} STREQUAL "Debug")
    add_compile_options(-g -O0)
    add_compile_definitions(KNET_DEBUG_BUILD=1)
elseif (${KNET_BUILD_TYPE} STREQUAL "Test" OR ${KNET_BUILD_TYPE} STREQUAL "SDV" OR ${KNET_BUILD_TYPE} STREQUAL "Fuzz")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
    add_compile_definitions(KNET_SDV_TEST)
else ()
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    add_definitions(-UKNET_DEBUG_BUILD)
endif ()

include(${PROJECT_SOURCE_DIR}/cmake/knet.cmake)

add_subdirectory(src)

if (${KNET_BUILD_TYPE} STREQUAL "SDV" OR ${KNET_BUILD_TYPE} STREQUAL "Test")
    add_subdirectory(test)
endif ()
