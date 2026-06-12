if (${KNET_STACK_TYPE} STREQUAL "dp")
    add_subdirectory(extern_api)
    add_subdirectory(posix_api)
    add_subdirectory(socket_bridge)
endif()

if (${KNET_STACK_TYPE} STREQUAL "dtoe")
    add_subdirectory(dtoe_api)
endif()

if (${KNET_STACK_TYPE} STREQUAL "kbdtoe")
    add_subdirectory(kbdtoe_api)
endif()