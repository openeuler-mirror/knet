set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # 导出编译命令compile_commands.json，用于pre-commit代码分析工具

add_subdirectory(knet)
add_subdirectory(stack)
