# SPDX-License-Identifier: GPL-2.0-only
#
# cloudnative/CMakeLists.txt — 云原生子仓的用户态构建入口
#
# 职责：构建 CRD 校验与规划件、agentctl 命令行；lib/ 提供各件共用的
# JSON 读取件。agentctl 依赖 headers_install 导出的 UAPI 头，未提供
# AIRY_HDR_ROOT 时跳过该目标并给出提示，其余目标不依赖内核头。
#
# 策略：C11；-Wall -Wextra -Werror；_FORTIFY_SOURCE=2；栈保护。

cmake_minimum_required(VERSION 3.20)

project(airy_cloudnative
	VERSION 0.1.1
	LANGUAGES C
	DESCRIPTION "AirymaxOS cloudnative submodule — CRD + agentctl")

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# [SC] 头文件引用（OS-IRON-014 / 决策 G1）
# 唯一物理宿主：kernel/include/uapi/linux/airymax/，禁止物理副本。
# 用户态只能针对 headers_install 导出的 UAPI 头构建：源码树的
# kernel/include/uapi 依赖 __EXPORTED_HEADERS__ 与内核内部头，
# 直接当搜索根会与 libc 的 loff_t/blkcnt_t 等定义冲突。
# 导出根经 -DAIRY_HDR_ROOT 传入；未传时只跳过 agentctl。
# ---------------------------------------------------------------------------
if(AIRY_HDR_ROOT)
	include_directories(${AIRY_HDR_ROOT})
	message(STATUS "cloudnative: 使用 UAPI 导出根 ${AIRY_HDR_ROOT}")
else()
	message(STATUS "cloudnative: 未提供 AIRY_HDR_ROOT，跳过 agentctl")
endif()

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

option(BUILD_TESTING "构建并注册云原生用户态测试" ON)
if(BUILD_TESTING)
	enable_testing()
endif()

add_subdirectory(lib)

if(AIRY_HDR_ROOT)
	add_subdirectory(agentctl)
	install(TARGETS agentctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

add_subdirectory(crd)
add_subdirectory(shim)
add_subdirectory(cni)
add_subdirectory(supernode)
