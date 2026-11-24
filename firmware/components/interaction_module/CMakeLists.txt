idf_component_register(
    SRCS "src/interaction_module.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_driver_gpio
        esp_driver_rmt
        esp_adc
        esp_timer
        peripherals
)
