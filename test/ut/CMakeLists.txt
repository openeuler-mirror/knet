cmake_minimum_required(VERSION 3.14.1)
set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)

set(DPDK_STATIC_LIBRARIES
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

project(knet_ut)
cmake_minimum_required(VERSION 3.16)
add_compile_options(-g -fPIC -O0 -fpermissive -fprofile-arcs -ftest-coverage -fsanitize=address -static-libasan)

set(UT_NAME knet_ut_BIN)

add_definitions(-DTEST_SDV_DEBUG)
add_definitions(-DKNET_TEST)
add_definitions(-DKNET_MP_DAEMON)
add_definitions(-DKNET_STATIC=)
add_definitions(-DSDV_LOG_LEVEL=4) # 参数值可为log级别枚举量(KNET_LOG_DEFAULT)或对应的整数量(4)
 
set(KNET_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(KNET_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/knet)
set(SDV_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
 
FILE(GLOB_RECURSE testcases_list
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mock/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mock/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/testcases/*.cpp
)
FILE(GLOB_RECURSE KNET_SRC ${KNET_SRC_DIR}/*.c)
set(TEST_SRC main.cpp ${testcases_list} ${KNET_SRC})
 
set(DPDK_INCLUDE_DIRS ${KNET_ROOT_DIR}/build/opensource/include/dpdk)
set(CJSON_INCLUDE_DIRS ${KNET_ROOT_DIR}/build/opensource/include/cJSON)
set(SECUREC_INCLUDE_DIRS ${KNET_ROOT_DIR}/build/opensource/include/securec)
set(KNET_INCLUDE_DIRS ${KNET_ROOT_DIR}/build/include/h)

set(DPDK_LIB_DIRS ${KNET_ROOT_DIR}/build/opensource/lib/dpdk)
set(CJSON_LIB_DIRS ${KNET_ROOT_DIR}/build/opensource/lib/cJSON)
set(SECUREC_LIB_DIRS ${KNET_ROOT_DIR}/build/opensource/lib/securec)
set(KNET_LIB_DIRS ${KNET_ROOT_DIR}/build/lib)

set(SECURE_C boundscheck)

# 添加函数查找所有头文件目录
function(find_header_directories base_dir result_var) # 新增
    set(dirs "") # 新增
    # 查找所有包含 .h 文件的目录
    file(GLOB_RECURSE h_files "${base_dir}/src/*.h")     # 新增
    foreach(h_file IN LISTS h_files) # 新增
        get_filename_component(dir ${h_file} DIRECTORY) # 新增
        list(APPEND dirs ${dir}) # 新增
    endforeach() # 新增
    # 去重
    list(REMOVE_DUPLICATES dirs) # 新增
    set(${result_var} ${dirs} PARENT_SCOPE) # 新增
endfunction() # 新增
 
# 查找所有头文件目录
find_header_directories(${KNET_ROOT_DIR} ALL_HEADER_DIRS) # 新增

# link_directories(${KNET_ROOT_DIR}/opensource/secure_c/lib) # 指定securec动态库查找路径
# link_directories(${KNET_ROOT_DIR}/build/lib) # 指定knet动态库查找路径

message("普通信息：当前源码目录是 ${KNET_SRC_DIR}")

add_executable(${UT_NAME} ${TEST_SRC}) # 指定生成一个名为knet_sdv的可执行文件, 使用${TEST_SRC}作为源文件
target_include_directories(${UT_NAME}  PRIVATE
    # 所有头文件路径
    ${DPDK_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
    ${SDV_ROOT_DIR}/mock
    ${SDV_ROOT_DIR}/common
    ${KNET_INCLUDE_DIRS}
    ${SECUREC_INCLUDE_DIRS}
    ${ALL_HEADER_DIRS}
)

target_link_directories(${UT_NAME}  PRIVATE
    ${DPDK_LIB_DIRS}
    ${CJSON_LIB_DIRS}
    ${KNET_LIB_DIRS}
    ${SECUREC_LIB_DIRS}
)

# 指定链接到可执行文件的库
target_link_libraries(${UT_NAME} PRIVATE
    # knet_core knet_frame
    dpstack
    dl pthread gtest gmock 
    -Wl,--whole-archive
    -Wl,--as-needed
    ${DPDK_STATIC_LIBRARIES}
    -Wl,--no-whole-archive
    -Wl,--export-dynamic
    dl
    cap
    ${SECURE_C}
    cjson
    gcov
    -fsanitize=address
)

set_target_properties(${UT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${SDV_ROOT_DIR})
