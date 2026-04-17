# 清除默认的编译选项
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")

# Unit Test编译选项
if (${KNET_BUILD_TYPE} STREQUAL "Test")
    set(CMAKE_C_FLAGS 
        "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage -g -O0 \
        -fsanitize=address \
        -fsanitize=undefined \
        -fsanitize-coverage=trace-pc"
    )
# Fuzz测试编译选项
elseif (${KNET_BUILD_TYPE} STREQUAL "Fuzz")
    set(CMAKE_C_FLAGS 
        "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage -Wall -g -fPIC \
        -fno-omit-frame-pointer \
        -fno-common \
        -O0 \
        -fsanitize=address \
        -fsanitize=undefined \
        -fsanitize-coverage=trace-pc"
    )
# SDV测试编译选项
elseif (${KNET_BUILD_TYPE} STREQUAL "SDV")
    set(CMAKE_C_FLAGS 
        "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage"
    )
# Release编译选项
elseif (${KNET_BUILD_TYPE} STREQUAL "Release")
    set(CMAKE_C_FLAGS
        "-Werror \
        -Wall \
        -O2 \
        -D_FORTIFY_SOURCE=2 \
        -fPIE \
        -fPIC \
        -fstack-protector-strong \
        -s"
    )
# Debug编译选项默认从项目CMake统一添加
elseif (${KNET_BUILD_TYPE} STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Werror -Wall -g -O0")
endif()


set(KET_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../)
set(TARGET_CORE knet_core)
set(TARGET_FRAME knet_frame)
set(TARGET_DAEMON knet_mp_daemon)
set(SECURE_C boundscheck)
set(DPDK_DIR ${KET_SRC_DIR}/opensource/dpdk/output/lib64)

link_directories(
    ${DPDK_DIR}
    ${KET_SRC_DIR}/opensource/cJSON/build
    ${KET_SRC_DIR}/opensource/secure_c/lib
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
if (${KNET_BUILD_TYPE} STREQUAL "Release" OR ${KNET_BUILD_TYPE} STREQUAL "Debug")
    set(CMAKE_SKIP_RPATH TRUE)
endif ()

# 获取knet.spec文件中的Version号
set(VERSION_REGEX "Version:[ \t]+(.+)")
file(STRINGS ${KET_SRC_DIR}/package/knet.spec VERSION_STRING REGEX ${VERSION_REGEX})
string(REGEX REPLACE ${VERSION_REGEX} "\\1" VERSION_NUM ${VERSION_STRING})

# 使用正则表达式提取主版本号
string(REGEX MATCH "^[0-9]" VERSION_MAJOR ${VERSION_NUM})

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

# knet_core链接选项
target_link_options(${TARGET_CORE} PRIVATE
    -rdynamic
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-EL
    -Wl,-z,noexecstack
)

# 根据找到的库设置链接选项
target_link_libraries(${TARGET_CORE}
    base config dpdk_init log tun mem thread rand dpdk_utils osapi 
    mbuf transmission fmm sal_tcp hash knet_init rpc capability telemetry offload pktdump tcp_fd tcp_socket tcp_os
    signal_tcp tcp_event tcp_api_init bond stk_mp

    # 下面都是外部opensource的动态库
    libcjson.a
    -Wl,--whole-archive ${SECURE_C} -Wl,--no-whole-archive
    ${DPDK_LIBS}
    dl
    pthread
    cap
    ${FUZZ_LIBS}
)

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

# knet_frame链接选项
target_link_options(${TARGET_FRAME} PRIVATE
    -rdynamic
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-EL
    -Wl,-z,noexecstack
)

target_link_libraries(${TARGET_FRAME}
    posix_api socket_bridge log zcopy cothread extern_api
    ${TARGET_CORE}
    # 下面都是外部opensource的动态库
)

add_compile_definitions(KNET_VERSION="${VERSION_NUM}")
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

# knet_daemon链接选项
target_link_options(${TARGET_DAEMON} PRIVATE
    -rdynamic
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-EL
    -Wl,-z,noexecstack
    -pie
)

# 根据找到的库设置daemon的链接选项
target_link_libraries(${TARGET_DAEMON}
    ${TARGET_FRAME}
    rte_telemetry
    libcjson.a
    ${SECURE_C}
    dl
    pthread
    cap
    ${DPDK_LIBS}
    ${FUZZ_LIBS}
)

add_subdirectory(api)
add_subdirectory(framework)
add_subdirectory(hal)
add_subdirectory(sal)
add_subdirectory(signal)