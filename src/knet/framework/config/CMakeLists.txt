ADD_MODULE(config)

#适配knet_config_hw_scan.c文件内相关接口在测试场景下条件编译
if (${KNET_BUILD_TYPE} STREQUAL "Test" OR ${KNET_BUILD_TYPE} STREQUAL "SDV" OR ${KNET_BUILD_TYPE} STREQUAL "Fuzz")
    add_compile_definitions(KNET_TEST)
endif ()