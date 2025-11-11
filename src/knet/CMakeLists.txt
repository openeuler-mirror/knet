if (${KNET_BUILD_TYPE} STREQUAL "Test")
    add_compile_options(-g -O0 -fsanitize=address -fsanitize=undefined -fsanitize-coverage=trace-pc)
endif ()

if (${KNET_BUILD_TYPE} STREQUAL "Fuzz")
    add_compile_options(-Wall -g -fPIC -fno-omit-frame-pointer -fno-common -O0 -fsanitize=address -fsanitize=undefined -fsanitize-coverage=trace-pc)
endif ()

set(KET_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../)
set(TARGET_CORE knet_core)
set(TARGET_FRAME knet_frame)
set(DPDK_DIR ${KET_SRC_DIR}/opensource/dpdk/output/lib64)

# 检查securec和boundscheck库
find_library(SECUREC_LIB 
    NAMES securec
    PATHS ${KET_SRC_DIR}/opensource/secure_c/lib
    NO_DEFAULT_PATH
)

find_library(BOUNDSCHECK_LIB
    NAMES boundscheck
    PATHS ${KET_SRC_DIR}/opensource/secure_c/lib
    NO_DEFAULT_PATH
)

# 优先使用securec，如果找不到则使用boundscheck
if(SECUREC_LIB)
    message(STATUS "Found securec library: ${SECUREC_LIB}")
    set(SECURE_LIB_NAME securec)
    set(SECURE_LIB_PATH ${KET_SRC_DIR}/opensource/secure_c/lib)
elseif(BOUNDSCHECK_LIB)
    message(STATUS "Found boundscheck library: ${BOUNDSCHECK_LIB}")
    set(SECURE_LIB_NAME boundscheck)
    set(SECURE_LIB_PATH ${KET_SRC_DIR}/opensource/secure_c/lib)
else()
    message(WARNING "Neither securec nor boundscheck library found")
    set(SECURE_LIB_NAME "")
    set(SECURE_LIB_PATH "")
endif()

link_directories(
    ${DPDK_DIR}
    ${KET_SRC_DIR}/opensource/cJSON/build
    ${SECURE_LIB_PATH}
    ${KET_SRC_DIR}/opensource/Secodefuzz/lib
)

if (${KNET_BUILD_TYPE} STREQUAL "Fuzz")
set(FUZZ_LIBS
    ubsan
    asan
    Secodefuzz
)
else()
set(FUZZ_LIBS)
endif ()

set(DPDK_LIBS
    rte_eal
    rte_ethdev
    rte_fib
    rte_hash
    rte_mbuf
    rte_mempool
    rte_mempool_ring
    rte_rib
    rte_timer
    rte_net_virtio
    # 抓包依赖
    rte_telemetry
    rte_pdump
    rte_distributor
    rte_ipsec
    rte_efd
    rte_member
    rte_ring
    rte_bpf
    rte_pcapng
)

# 生成RPM包时，不包含RPATH
if (${KNET_BUILD_TYPE} STREQUAL "Release")
    set(CMAKE_SKIP_RPATH TRUE)
endif ()

# 获取knet.spec文件中的Version号
set(VERSION_REGEX "Version:[ \t]+(.+)")
file(STRINGS ${KET_SRC_DIR}/package/knet.spec VERSION_STRING REGEX ${VERSION_REGEX})
string(REGEX REPLACE ${VERSION_REGEX} "\\1" VERSION_NUM ${VERSION_STRING})

# 获取knet.spec文件中的Release号
set(RELEASE_REGEX "Release:[ \t]+(.+)")
file(STRINGS ${KET_SRC_DIR}/package/knet.spec RELEASE_STRING REGEX ${RELEASE_REGEX})
string(REGEX REPLACE ${RELEASE_REGEX} "\\1" RELEASE_NUM ${RELEASE_STRING})

add_library(${TARGET_CORE} SHARED)
# 每次更新无需手动更新VERSION和RELEASE，自动在knet.spec文件中获取
set_target_properties(${TARGET_CORE} PROPERTIES VERSION ${VERSION_NUM} SOVERSION ${RELEASE_NUM})
add_definitions(-DKNET_STATIC=static)

# 根据找到的库设置链接选项
target_link_libraries(${TARGET_CORE}
    base config io_init log tun mem thread rand symbols
    mbuf transmission fmm sal_dp hash init rpc capability telemetry
    # 下面都是外部opensource的动态库
    libcjson.a
    -Wl,--whole-archive ${SECURE_LIB_NAME} -Wl,--no-whole-archive
    ${DPDK_LIBS}
    dl
    pthread
    cap
    ${FUZZ_LIBS}
)

add_library(${TARGET_FRAME} SHARED)
# 每次更新无需手动更新VERSION和RELEASE，自动在knet.spec文件中获取
set_target_properties(${TARGET_FRAME} PROPERTIES VERSION ${VERSION_NUM} SOVERSION ${RELEASE_NUM})

target_link_libraries(${TARGET_FRAME}
    dp_hijack log statistics symbols
    ${TARGET_CORE}
    # 下面都是外部opensource的动态库
)

set(TARGET_DAEMON knet_mp_daemon)
add_compile_definitions(KNET_VERSION="${VERSION_NUM}")
add_executable(${TARGET_DAEMON} daemon/knet_mp_daemon.c)

# 根据找到的库设置daemon的链接选项
target_link_libraries(${TARGET_DAEMON}
    base config log tun mem mbuf fmm transmission rpc io_init thread hash capability
    telemetry
    rte_telemetry
    libcjson.a
    ${SECURE_LIB_NAME}
    dl
    pthread
    cap
    ${DPDK_LIBS}
    ${FUZZ_LIBS}
)

add_subdirectory(core)
add_subdirectory(framework)
add_subdirectory(sal)
add_subdirectory(common)