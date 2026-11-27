# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
#
# supernode — 拓扑发现、令牌池、迁移准入与快照恢复

add_library(sn STATIC
    sn_topology.c
    sn_pool.c
    sn_migrate.c
    sn_snap.c
)
target_include_directories(sn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sn PUBLIC cnjson)

add_executable(airy-supernode sn_main.c)
target_link_libraries(airy-supernode PRIVATE sn)
install(TARGETS airy-supernode RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

if(BUILD_TESTING)
    add_executable(sn_test tests/sn_test.c)
    target_link_libraries(sn_test PRIVATE sn)
    target_compile_definitions(sn_test PRIVATE
        SN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME sn_test COMMAND sn_test)
endif()
