#pragma once

/**
 * ESP-Mosaico V1.2 硬件引脚定义。
 * 来自官方用户指南 + Wokwi 模拟器文档。
 */

/* ===== I2C 总线 ===== */
// 对内 I2C0 (板载传感器)
#define MOSAICO_I2C0_SDA_GPIO   56
#define MOSAICO_I2C0_SCL_GPIO   3

// 对外 I2C1 (Interaction Module, 右槽 H1)
#define MOSAICO_I2C1_SDA_GPIO   0
#define MOSAICO_I2C1_SCL_GPIO   1

/* ===== I2C 设备地址 ===== */
#define I2C0_ADDR_CST9220_TOUCH  0x5A
#define I2C0_ADDR_BMI270_IMU     0x69
#define I2C0_ADDR_ES8311_CODEC   0x19
#define I2C0_ADDR_BMM150_2       0x11
#define I2C0_ADDR_BMM150_3       0x12
#define I2C0_ADDR_BQ27220_BATT   0x55

#define I2C1_ADDR_MODULE_EEPROM  0x51  // 右槽 H1

/* ===== LCD CO5300 QSPI =====
 * 依据官方 esp-mosaico-bsp/esp_mosaico.h：
 *   V1.0: SCL=44, RST=42
 *   V1.2: SCL=42, RST=44   ← 本板为 V1.2
 *   DATA0=36, DATA1=51, DATA2=35, DATA3=9, CS=50, TE=43
 */
#define LCD_SCL_GPIO      42
#define LCD_CS_GPIO       50
#define LCD_DATA0_GPIO    36
#define LCD_DATA1_GPIO    51
#define LCD_DATA2_GPIO    35
#define LCD_DATA3_GPIO    9
#define LCD_TE_GPIO       43
#define LCD_RST_GPIO      44
#define LCD_WIDTH         480
#define LCD_HEIGHT        480
#define LCD_H_RES         LCD_WIDTH
#define LCD_V_RES         LCD_HEIGHT

/* ===== Touch CST9220 ===== */
#define TP_INT_GPIO       6

/* ===== 音频 ES8311 (I2S) ===== */
#define I2S_MCLK_GPIO     54
#define I2S_BCK_GPIO      37
#define I2S_WS_GPIO       49
#define I2S_DIN_GPIO      52
#define I2S_DOUT_GPIO     40
#define PA_CTRL_GPIO      45

/* ===== 振动马达 ===== */
#define MOTOR_GPIO        8

/* ===== Function Button ===== */
#define AI_BUTTON_GPIO    7

/* ===== 电源控制 ===== */
#define VCC_3V3_CTRL_GPIO  60   // 低电平有效，启用屏幕 + 扩展 3.3V
#define POWER_SWITCH_GPIO  57

/* ===== Interaction Module 右槽 H1 ===== */
#define MOD_LDR_GPIO      46    // GL5506 光敏 ADC
#define MOD_IR_GPIO       47    // IR 发送
#define MOD_KEYL_GPIO     11    // 左按键/触摸 通道
#define MOD_KEYR_GPIO     10    // 右按键/触摸 通道
#define MOD_PIR_GPIO      5     // PIR 人体感应
#define MOD_WS2812_GPIO   38    // WS2812 RGB × 6 数据脚
