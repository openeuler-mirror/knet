cmake_minimum_required(VERSION 3.14.1)

project(DP LANGUAGES C)

set(CMAKE_SKIP_RPATH TRUE)

set(TARGET_DP dpstack)
set(CMAKE_C_STANDARD 11)
set(DP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(KNET_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../)

set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
#TCP协议栈相关编译选项
set(CMAKE_C_FLAGS
    "-Werror \
    -Wall \
    -Wfloat-equal \
    -Wshadow \
    -Wformat=2 \
    -fgnu89-inline \
    -fno-common \
    -fno-strict-aliasing \
    -fno-omit-frame-pointer \
    -pipe \
    -freg-struct-return \
    -fPIC \
    -fstack-protector-strong \
    -fsigned-char"
)
#如果Release版本，-O2优化
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2 -O2")
endif()

#设置头文件依赖路径
set(PROJECT_DEPENDS_INC_DIR ${KNET_SRC_DIR}/opensource)

#设置库文件依赖路径
set(PROJECT_DEPENDS_LIB_DIR ${KNET_SRC_DIR}/opensource)

#设置头文件输出路径
set(PROJECT_OUT_INC_DIR ${KNET_SRC_DIR}/build/include/dp)

#设置库文件输出路径
set(PROJIECT_OUT_LIB_DIR ${KNET_SRC_DIR}/build/lib/dp)

#设置连接选项
set(CMAKE_SHARED_LINKER_FLAGS
    "-rdynamic \
    -Wl,-EL \
    -Wl,-z,relro,-z,noexecstack \
    -Wl,-z,now \
    -lpthread \
    -ldl"
)

#如果Release版本
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -g")
endif()
include(${DP_SRC_DIR}/cmake/knet.cmake)

# 获取knet.spec文件中的Version号
set(VERSION_REGEX "Version:[ \t]+(.+)")
file(STRINGS ${KNET_SRC_DIR}/package/knet.spec VERSION_STRING REGEX ${VERSION_REGEX})
string(REGEX REPLACE ${VERSION_REGEX} "\\1" VERSION_NUM ${VERSION_STRING})

# 获取knet.spec文件中的Release号
set(RELEASE_REGEX "Release:[ \t]+(.+)")
file(STRINGS ${KNET_SRC_DIR}/package/knet.spec RELEASE_STRING REGEX ${RELEASE_REGEX})
string(REGEX REPLACE ${RELEASE_REGEX} "\\1" RELEASE_NUM ${RELEASE_STRING})

add_library(${TARGET_DP} SHARED)
# 每次更新无需手动更新VERSION和RELEASE，自动在knet.spec文件中获取
set_target_properties(${TARGET_DP} PROPERTIES VERSION ${VERSION_NUM} SOVERSION ${RELEASE_NUM})

set(DP_BYTE_ORDER DP_LITTLE_ENDIAN)

add_compile_definitions(DP_BYTE_ORDER=${DP_BYTE_ORDER})

# 根据找到的库设置链接选项
target_link_libraries(${TARGET_DP}
    dpfwk_base dpfwk_cpd dpfwk_netdev dpfwk_ns dpfwk_pbuf dpfwk_pmgr dpfwk_shm dpfwk_tbm dpfwk_utils dpfwk_worker
    dpitf_epoll dpitf_fd dpitf_init dpitf_poll dpitf_posix dpitf_select dpitf_socket
    dpl2_eth
    dpl3_ip
    dpl4_tcp
    dpl4_udp
)

add_subdirectory(src)
