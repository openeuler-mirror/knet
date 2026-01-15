if (${KNET_BUILD_TYPE} STREQUAL "Test")
    add_compile_options(-g -O0 -fsanitize=address -fsanitize=undefined -fsanitize-coverage=trace-pc)
endif ()

if (${KNET_BUILD_TYPE} STREQUAL "Fuzz")
    add_compile_options(-Wall -g -fPIC -fno-omit-frame-pointer -fno-common -O0 -fsanitize=address -fsanitize=undefined -fsanitize-coverage=trace-pc)
endif ()

set(KET_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../)
set(TARGET_CORE knet_core)
set(TARGET_FRAME knet_frame)
set(TARGET_DAEMON knet_mp_daemon)
set(DPDK_DIR ${KET_SRC_DIR}/opensource/dpdk/output/lib64)

# 检查boundscheck库
find_library(BOUNDSCHECK_LIB
    NAMES boundscheck
    PATHS ${KET_SRC_DIR}/opensource/secure_c/lib
    NO_DEFAULT_PATH
)

# 使用boundscheck
if(BOUNDSCHECK_LIB)
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
    rte_net_bond
)

# 生成RPM包时，不包含RPATH
if (${KNET_BUILD_TYPE} STREQUAL "Release")
    set(CMAKE_SKIP_RPATH TRUE)
endif ()

# 获取knet.spec文件中的Version号
set(VERSION_REGEX "Version:[ \t]+(.+)")
file(STRINGS ${KET_SRC_DIR}/package/knet.spec VERSION_STRING REGEX ${VERSION_REGEX})
string(REGEX REPLACE ${VERSION_REGEX} "\\1" VERSION_NUM ${VERSION_STRING})

# 使用正则表达式提取主版本号
string(REGEX MATCH "^[0-9]" VERSION_MAJOR ${VERSION_NUM})

if (${KNET_STACK_TYPE} STREQUAL "dp")
    add_library(${TARGET_CORE} SHARED)
    # 每次更新无需手动更新VERSION和RELEASE，自动在knet.spec文件中获取
    set_target_properties(${TARGET_CORE} PROPERTIES VERSION ${VERSION_NUM} SOVERSION ${VERSION_MAJOR})
    add_definitions(-DKNET_STATIC=static)

    if(${KNET_BUILD_TYPE} STREQUAL "Release")
        add_custom_command(
            TARGET "${TARGET_CORE}" POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${TARGET_CORE}> "${LIBRARY_DEBUG_SYMBOL_DIR}/$<TARGET_FILE_NAME:${TARGET_CORE}>.debug"
            COMMAND ${CMAKE_OBJCOPY} --strip-all $<TARGET_FILE:${TARGET_CORE}>
            COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=${LIBRARY_DEBUG_SYMBOL_DIR}/$<TARGET_FILE_NAME:${TARGET_CORE}>.debug $<TARGET_FILE:${TARGET_CORE}>
            COMMENT "Stripping debug symbols and generating .debug file"
            VERBATIM
        )
    endif()

    # 根据找到的库设置链接选项
    target_link_libraries(${TARGET_CORE}
        base config dpdk_init log tun mem thread rand dpdk_utils osapi 
        mbuf transmission fmm sal_tcp hash knet_init rpc capability telemetry offload pktdump tcp_fd tcp_socket tcp_os
        signal_tcp tcp_event tcp_api_init bond stk_mp

        # 下面都是外部opensource的动态库
        libcjson.a
        -Wl,--whole-archive ${SECURE_LIB_NAME} -Wl,--no-whole-archive
        ${DPDK_LIBS}
        dl
        pthread
        cap
        ${FUZZ_LIBS}
    )
endif()

if (${KNET_STACK_TYPE} STREQUAL "dtoe")
    add_library(${TARGET_CORE} SHARED)
    # 每次更新无需手动更新VERSION和RELEASE，自动在knet.spec文件中获取
    set_target_properties(${TARGET_CORE} PROPERTIES VERSION ${VERSION_NUM} SOVERSION ${VERSION_MAJOR})
    add_definitions(-DKNET_STATIC=static)

    if(${KNET_BUILD_TYPE} STREQUAL "Release")
        add_custom_command(
            TARGET "${TARGET_CORE}" POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${TARGET_CORE}> "${LIBRARY_DEBUG_SYMBOL_DIR}/$<TARGET_FILE_NAME:${TARGET_CORE}>.debug"
            COMMAND ${CMAKE_OBJCOPY} --strip-all $<TARGET_FILE:${TARGET_CORE}>
            COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=${LIBRARY_DEBUG_SYMBOL_DIR}/$<TARGET_FILE_NAME:${TARGET_CORE}>.debug $<TARGET_FILE:${TARGET_CORE}>
            COMMENT "Stripping debug symbols and generating .debug file"
            VERBATIM
        )
    endif()

    # 根据找到的库设置链接选项
    target_link_libraries(${TARGET_CORE}
        base log
        # 下面都是外部opensource的动态库
        -Wl,--whole-archive ${SECURE_LIB_NAME} -Wl,--no-whole-archive
    )
endif()

add_library(${TARGET_FRAME} SHARED)
# 每次更新无需手动更新VERSION和RELEASE，自动在knet.spec文件中获取
set_target_properties(${TARGET_FRAME} PROPERTIES VERSION ${VERSION_NUM} SOVERSION ${VERSION_MAJOR})
if(${KNET_BUILD_TYPE} STREQUAL "Release")
    add_custom_command(
        TARGET "${TARGET_FRAME}" POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${TARGET_FRAME}> "${LIBRARY_DEBUG_SYMBOL_DIR}/$<TARGET_FILE_NAME:${TARGET_FRAME}>.debug"
        COMMAND ${CMAKE_OBJCOPY} --strip-all $<TARGET_FILE:${TARGET_FRAME}>
        COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=${LIBRARY_DEBUG_SYMBOL_DIR}/$<TARGET_FILE_NAME:${TARGET_FRAME}>.debug $<TARGET_FILE:${TARGET_FRAME}>
        COMMENT "Stripping debug symbols and generating .debug file"
        VERBATIM
    )
endif()

if (${KNET_STACK_TYPE} STREQUAL "dp")
    target_link_libraries(${TARGET_FRAME}
        posix_api socket_bridge log zcopy cothread extern_api
        ${TARGET_CORE}
        # 下面都是外部opensource的动态库
    )
endif()

if (${KNET_STACK_TYPE} STREQUAL "dtoe")
    target_link_libraries(${TARGET_FRAME}
        dtoe_api
        ${TARGET_CORE}
        # 下面都是外部opensource的动态库
        dtoe
    )
endif()

add_compile_definitions(KNET_VERSION="${VERSION_NUM}")
if (${KNET_STACK_TYPE} STREQUAL "dp")
    add_executable(${TARGET_DAEMON} daemon/knet_mp_daemon.c)
    if(${KNET_BUILD_TYPE} STREQUAL "Release")
        add_custom_command(
            TARGET "${TARGET_DAEMON}" POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${TARGET_DAEMON}> "$<TARGET_FILE_NAME:${TARGET_DAEMON}>.debug"
            COMMAND ${CMAKE_OBJCOPY} --strip-all $<TARGET_FILE:${TARGET_DAEMON}>
            COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE_NAME:${TARGET_DAEMON}>.debug $<TARGET_FILE:${TARGET_DAEMON}>
            COMMENT "Stripping debug symbols and generating .debug file"
            VERBATIM
        )
    endif()
    # 根据找到的库设置daemon的链接选项
    target_link_libraries(${TARGET_DAEMON}
        base config log mbuf fmm transmission rpc dpdk_init thread hash capability offload dpdk_utils pktdump tcp_fd
        bond
        tcp_fd
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
endif()

add_subdirectory(api)
add_subdirectory(framework)
if (${KNET_STACK_TYPE} STREQUAL "dp")
    add_subdirectory(hal)
    add_subdirectory(sal)
    add_subdirectory(signal)
endif()