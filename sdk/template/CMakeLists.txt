# ==========================================================================
# SoulCove 插件独立构建模板（第三方开发者入口）
#
# 用法：
#   1. 把本目录复制为你的插件工程，改名 MyPlugin（全文替换）
#   2. 准备 SDK 头文件：从宿主仓库 sdk/include/soulcove/ 取 IPlugin.h/IPluginAPI.h
#      放到 ./include/soulcove/（或用 SOULCOVE_SDK_INCLUDE 指向已有位置）
#   3. 安装与宿主同版本的 Qt6（Core 组件）与兼容的编译器（见 docs/plugin-sdk.md ABI 说明）
#   4. cmake -B build -DCMAKE_PREFIX_PATH=<Qt安装目录> && cmake --build build
#   5. 产物 myplugin.dll 放入宿主 plugins/ 目录，或经扩展市场分发
# ==========================================================================
cmake_minimum_required(VERSION 3.16)
project(myplugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

# SDK 头文件位置（默认取本工程 ./include/soulcove/）
set(SOULCOVE_SDK_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include" CACHE PATH
    "Directory containing soulcove/IPlugin.h and IPluginAPI.h")

add_library(myplugin MODULE
    MyPlugin.h
    MyPlugin.cpp
)
target_include_directories(myplugin PRIVATE "${SOULCOVE_SDK_INCLUDE}")
target_link_libraries(myplugin PRIVATE Qt6::Core)

# MinGW 产物默认带 lib 前缀（libmyplugin.dll），统一去掉便于市场发布
set_target_properties(myplugin PROPERTIES PREFIX "")
