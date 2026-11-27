# SPDX-License-Identifier: GPL-2.0-only
#
# cloudnative/lib/CMakeLists.txt — 云原生各件共用的 JSON 读取件
#
# 产物：静态库 cnjson，仅被同子仓目标链接，不安装到系统。
add_library(cnjson STATIC cn_json.c)
target_include_directories(cnjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
