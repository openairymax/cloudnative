# SPDX-License-Identifier: GPL-2.0-only
#
# cloudnative/crd/CMakeLists.txt — Agent 自定义资源的校验与规划
#
# 产物：静态库 crd、可执行 airy-crdctl、测试 crd_test；
# CLI 安装到 sbin，清单与示例安装到 share/airymax/crd。
add_library(crd STATIC
	crd_schema.c
	crd_units.c
	crd_plan.c
	crd_state.c)
target_include_directories(crd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(crd PUBLIC cnjson)

add_executable(airy-crdctl crd_main.c)
target_link_libraries(airy-crdctl PRIVATE crd)

install(TARGETS airy-crdctl RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(DIRECTORY manifests/ DESTINATION
	${CMAKE_INSTALL_DATADIR}/airymax/crd/manifests)
install(DIRECTORY examples/ DESTINATION
	${CMAKE_INSTALL_DATADIR}/airymax/crd/examples)

if(BUILD_TESTING)
	add_executable(crd_test tests/crd_test.c)
	target_link_libraries(crd_test PRIVATE crd)
	add_test(NAME crd_test COMMAND crd_test)
endif()
