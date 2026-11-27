# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
#
# cni — 链式插件、网络策略判定与注册记录

add_library(cni STATIC
    cni_conf.c
    cni_policy.c
    cni_state.c
)
target_include_directories(cni PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cni PUBLIC cnjson)

add_executable(airy-cni cni_main.c)
target_link_libraries(airy-cni PRIVATE cni)
install(TARGETS airy-cni RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

if(BUILD_TESTING)
    add_executable(cni_test tests/cni_test.c)
    target_link_libraries(cni_test PRIVATE cni)
    target_compile_definitions(cni_test PRIVATE
        CNI_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME cni_test COMMAND cni_test)
endif()
