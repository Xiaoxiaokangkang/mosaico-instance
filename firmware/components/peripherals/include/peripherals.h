#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 ESP-Mosaico 所有底层外设。
 * 调用顺序：先 I2C → 再 LCD → 再 Touch → 再其他。
 */
esp_err_t peripherals_init_all(void);

/** 对内 I2C0 (板载传感器) */
esp_err_t peripherals_init_i2c0(void);
i2c_master_bus_handle_t peripherals_get_i2c0(void);

/** 对外 I2C1 (Interaction Module) */
esp_err_t peripherals_init_i2c1(void);
i2c_master_bus_handle_t peripherals_get_i2c1(void);

/** LCD + Touch */
esp_err_t peripherals_init_lcd(esp_lcd_panel_handle_t *out_panel);
esp_err_t peripherals_init_touch(void);

/** 触摸句柄（CST9220）；未初始化成功时返回 NULL */
esp_lcd_touch_handle_t peripherals_get_touch(void);

/** LCD DMA 完成回调：QSPI 传输结束后才释放 LVGL 的绘制缓冲 */
typedef void (*peripherals_lcd_flush_done_cb_t)(void *ctx);
void peripherals_lcd_set_flush_done_cb(peripherals_lcd_flush_done_cb_t cb, void *ctx);

/** 电源：拉低 GPIO60 启用屏幕和扩展 3.3V */
void peripherals_init_power(void);

/** 振动马达 */
void motor_turn_on(void);
void motor_turn_off(void);
void motor_pulse(int ms);

#ifdef __cplusplus
}
#endif
