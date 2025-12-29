add_subdirectory(knet)
if (${KNET_STACK_TYPE} STREQUAL "dp")
    add_subdirectory(stack)
endif()