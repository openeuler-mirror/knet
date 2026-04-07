#获取源文件
macro(GetSrcFiles list_src_files src_dir)
    file(GLOB_RECURSE src_files ${src_dir}/* )
    list(APPEND ${list_src_files} ${src_files})
endmacro()

#获取头文件
macro(GetIncFiles list_inc_files inc_dir)
	file(GLOB_RECURSE inc_files ${inc_dir}/*)
    list(APPEND ${list_inc_files} ${inc_files})
endmacro()

#spec依赖文件处理
macro(ADD_MODULE_DEPENDS spec_file)
    execute_process(
        COMMAND python3 -B ${CMAKE_SPEC_DIR}/depends_parse.py ${spec_file} ${CMAKE_CURRENT_BINARY_DIR}/pkg_depends.cmake
        RESULT_VARIABLE PKG_DEPENDS_RESULT
    )
    if(NOT "${PKG_DEPENDS_RESULT}" STREQUAL "0")
        message(FATAL_ERROR "Unexpected failure while executing pkg_dpends_parse:${spec_file}")
    endif()
endmacro()

#编译C工程
macro(COMPILE target list_src_files list_inc_files specfile)
	#拷贝头文件到指定目录
	foreach(item ${${list_inc_files}})
		if("${item}" MATCHES ".h$")
			message("find h:${item}")
			file(COPY ${item} DESTINATION ${PROJECT_OUT_INC_DIR}/${target}/h)
			file(COPY ${item} DESTINATION ${PROJECT_OUT_INC_DIR}/h)
		endif()
	endforeach()

	#检查依赖关系
	ADD_MODULE_DEPENDS(${${specfile}})
	include(${CMAKE_CURRENT_BINARY_DIR}/pkg_depends.cmake)

	message("src_list:${${target}_list_src_files}")

	#指定编译为.o文件
	add_library(${target} OBJECT ${${list_src_files}} ${${list_inc_files}})

	#指定头文件路径
	target_include_directories(${target}
		PUBLIC
		"${PROJECT_OUT_INC_DIR}/${target}/h"
	)
	
	foreach(item ${${target}_depend_module})
		target_include_directories(${target}
			PUBLIC
			"${PROJECT_OUT_INC_DIR}/${item}/h"
            "${PROJECT_DEPENDS_INC_DIR}/${item}"
			"${PROJECT_DEPENDS_INC_DIR}/secure_c/include"
	)
	endforeach()
endmacro()

macro(ADD_MODULE_WITH_SPECIFIED_SPEC target spec_file)

	set(${target}_specfile ${spec_file})
	
	message("current:${CMAKE_CURRENT_SOURCE_DIR}")
	
	GetSrcFiles(${target}_list_src_files ${CMAKE_CURRENT_SOURCE_DIR}/src)
	GetIncFiles(${target}_list_inc_files ${CMAKE_CURRENT_SOURCE_DIR}/include)

	COMPILE(${target} ${target}_list_src_files ${target}_list_inc_files ${target}_specfile)

endmacro()

macro(ADD_MODULE target)
    ADD_MODULE_WITH_SPECIFIED_SPEC("${target}" ${CMAKE_CURRENT_SOURCE_DIR}/${target}.spec ${ARGN})
endmacro()