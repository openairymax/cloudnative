# ===========================================================================
# agent-linux (AirymaxOS) cloudnative submodule CMakeLists.txt
# 构建入口：agentctl CLI + CRD + Controller + Scheduler + Registry + Packager
# 决策 F1：最小可编译骨架（非桩）
# ===========================================================================

cmake_minimum_required(VERSION 3.20)

project(airy_cloudnative
	VERSION 0.1.1
	LANGUAGES C
	DESCRIPTION "AirymaxOS cloudnative submodule — agentctl + control plane")

# ---------------------------------------------------------------------------
# C 标准：C11（用户态 CLI）
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# [SC] 头文件引用（OS-IRON-014 / 决策 G1）
# 唯一物理宿主：kernel/include/uapi/linux/airymax/
# 禁止物理副本，通过 -I 引用
# ---------------------------------------------------------------------------
include_directories(${CMAKE_SOURCE_DIR}/../kernel/include)
include_directories(${CMAKE_SOURCE_DIR}/../kernel/include/uapi/linux)

# ---------------------------------------------------------------------------
# 编译选项（OS-STD-FMT-001/002）
# ---------------------------------------------------------------------------
add_compile_options(
	-Wall
	-Wextra
	-Werror
	-Wno-unused-parameter
	-fstack-protector-strong
	-D_FORTIFY_SOURCE=2
)

# ---------------------------------------------------------------------------
# 子目录：agentctl CLI
# 0.1.1 仅 agentctl 可编译，其余 1.0.1 补齐
# ---------------------------------------------------------------------------
add_subdirectory(agentctl)

# ---------------------------------------------------------------------------
# 安装规则
# ---------------------------------------------------------------------------
install(TARGETS agentctl
	RUNTIME DESTINATION bin
)
