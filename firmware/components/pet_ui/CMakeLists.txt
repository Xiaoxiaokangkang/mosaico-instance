idf_component_register(
    SRCS "src/pet_ui.c" "src/lv_font_starpet_18.c" "src/ghost_art.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_lcd
        esp_lcd_touch
        esp_lcd_co5300
        esp_lcd_touch_cst9220
        esp_timer
        pet_core
        peripherals
        interaction_module
        lvgl
)
