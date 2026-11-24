idf_component_register(
    SRCS "src/peripherals.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_driver_ledc
        esp_lcd
        esp_lcd_co5300
        esp_lcd_touch
        esp_lcd_touch_cst9220
        esp_timer
        esp_system
        efuse
)
