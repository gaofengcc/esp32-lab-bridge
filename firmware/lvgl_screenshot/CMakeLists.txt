set(SRC_LIST "")
set(PRIV_REQUIRES_LIST "")

if(CONFIG_LVGL_SCREENSHOT_ENABLE)
    set(SRC_LIST "lvgl_screenshot.c")
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
