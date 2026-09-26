# 配置关闭时不编译源文件，也不引入 LVGL 相关依赖。
set(SRC_LIST "")
set(PRIV_REQUIRES_LIST "")

if(CONFIG_LVGL_SCREENSHOT_ENABLE)
    set(SRC_LIST "lvgl_screenshot.c")
    # LVGL 和 lvgl_port 由目标项目提供；freertos、heap 为 ESP-IDF 组件。
    set(PRIV_REQUIRES_LIST
        lvgl
        lvgl_port
        freertos
        heap
    )
endif()

if(PRIV_REQUIRES_LIST)
    idf_component_register(
        SRCS
            ${SRC_LIST}
        INCLUDE_DIRS
            "include"
        PRIV_REQUIRES
            ${PRIV_REQUIRES_LIST}
    )
else()
    idf_component_register(
        SRCS
            ${SRC_LIST}
        INCLUDE_DIRS
            "include"
    )
endif()
