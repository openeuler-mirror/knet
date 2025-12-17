ADD_MODULE(dpdk_init)

if (${KNET_BUILD_TYPE} STREQUAL "SDV" OR ${KNET_BUILD_TYPE} STREQUAL "Fuzz")
    add_compile_definitions(KNET_TEST)
endif ()