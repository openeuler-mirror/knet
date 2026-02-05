ADD_MODULE(bond)

# DPDK 23版本以上把bond的member_add和members_get置为__rte_experimental，告知开发者本版本的接口属于较新特性，未来可能会抛弃
set_source_files_properties(src/knet_bond.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-deprecated-declarations"
)