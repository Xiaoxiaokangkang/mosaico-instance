cmake_minimum_required(VERSION 3.16)

# 本工程是 esp-mosaico-vibe 工作区中的子项目，依赖工作区里的两个 submodule：
#   <workspace>/submodule/esp-mosaico-utils   （含 ESP-Iris、mosaico-tools）
#   <workspace>/submodule/esp-mosaico-bsp     （含 esp-mosaico-bsp、右侧交互模块驱动）
#
# 支持的放置方式（按顺序探测）：
#   1. 环境变量 / CMake 变量 MOSAICO_UTILS_ROOT 显式指定
#   2. <workspace>/projects/<name>/     → ../../submodule/esp-mosaico-utils
#   3. <workspace>/<name>/              → ../submodule/esp-mosaico-utils
if(NOT MOSAICO_UTILS_ROOT AND DEFINED ENV{MOSAICO_UTILS_ROOT})
    set(MOSAICO_UTILS_ROOT "$ENV{MOSAICO_UTILS_ROOT}")
endif()
if(NOT MOSAICO_UTILS_ROOT)
    foreach(candidate
            "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils"
            "${CMAKE_CURRENT_LIST_DIR}/../submodule/esp-mosaico-utils"
            "${CMAKE_CURRENT_LIST_DIR}/submodule/esp-mosaico-utils")
        if(EXISTS "${candidate}/mosaico-tools/cmake/mosaico_idf_project.cmake")
            set(MOSAICO_UTILS_ROOT "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT MOSAICO_UTILS_ROOT)
    message(FATAL_ERROR
        "找不到 esp-mosaico-utils。请把本工程放到 esp-mosaico-vibe/projects/ 下，"
        "或设置环境变量 MOSAICO_UTILS_ROOT 指向 esp-mosaico-vibe/submodule/esp-mosaico-utils。")
endif()
if(NOT MOSAICO_BSP_ROOT)
    set(MOSAICO_BSP_ROOT "${MOSAICO_UTILS_ROOT}/../esp-mosaico-bsp")
endif()

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()

# 与仓库内其它 Mosaico 应用保持一致：走 mosaico 的 IDF 工程脚手架，
# 才能拿到 ESP-Iris 固件契约的默认配置与 system-update-bundle 目标。
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/mosaico_idf_project.cmake")
# 放在最后：recovery 适配层会追加自己的 defaults，这里才能覆盖 bootloader 体积相关项。
list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.override.defaults")
project(desk_pet VERSION 1.0.0)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
