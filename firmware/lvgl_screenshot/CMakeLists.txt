set(SRC_LIST "")
if(CONFIG_LVGL_SCREENSHOT_ENABLE)
    set(SRC_LIST "lvgl_screenshot.c")
endif()

idf_component_register(
    SRCS
        ${SRC_LIST}
    INCLUDE_DIRS
        "include"
    REQUIRES
        lvgl
        lvgl_port
)

