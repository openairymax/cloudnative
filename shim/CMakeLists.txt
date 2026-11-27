# SPDX-License-Identifier: GPL-2.0-only
#
# cloudnative/shim/CMakeLists.txt —
# Agent 容器的 bundle 解析、运行时引擎与命令行
#
# 产物：静态库 shm、可执行 airy-shim、测试 shm_test；
# 安装 CLI 到 sbin。

add_library(shm STATIC
	shm_spec.c
	shm_kern.c
	shm_proc.c)
target_include_directories(shm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shm PUBLIC cnjson)

add_executable(airy-shim shm_main.c)
target_link_libraries(airy-shim PRIVATE shm)

install(TARGETS airy-shim RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

if(BUILD_TESTING)
	add_executable(shm_test tests/shm_test.c)
	target_link_libraries(shm_test PRIVATE shm)
	target_compile_definitions(shm_test PRIVATE
		SHM_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
	add_test(NAME shm_test COMMAND shm_test)
endif()
