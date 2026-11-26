# ===========================================================================
# agentctl CLI CMakeLists.txt
# AirymaxOS cloud-native control plane CLI（[IND]）
# 决策 F1：真实可编译实现（非桩）
# ===========================================================================

# 可执行目标 agentctl
add_executable(agentctl
	agentctl.c
)

# [SC] 头文件搜索根由顶层 CMakeLists.txt 统一登记

# 编译定义
target_compile_definitions(agentctl
	PRIVATE
		_GNU_SOURCE
)

# 安装
install(TARGETS agentctl
	RUNTIME DESTINATION bin
)
