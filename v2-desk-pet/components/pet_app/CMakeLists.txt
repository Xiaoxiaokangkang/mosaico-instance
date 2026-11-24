idf_component_register(
    SRCS "pet_app.c"
    INCLUDE_DIRS "include"
    REQUIRES pet_core pet_ui lvgl esp_timer nvs_flash esp-mosaico-bsp mosaico_module_interact
)
