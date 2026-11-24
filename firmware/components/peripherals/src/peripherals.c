/**
 * peripherals.c — ESP-Mosaico V1.2 底层外设初始化
 *
 * ESP-IDF v6 API 迁移版：
 *   - driver/i2c_master.h        ← v6 i2c_master API
 *   - esp_lcd_io_spi.h           ← v6 替代旧的 esp_lcd_panel_io_spi.h
 *   - esp_lcd_io_i2c.h           ← v6 新增（给触摸传感器包一层 LCD Panel IO）
 *   - spi_bus_initialize         ← QSPI 用 flags.quad + 4 条数据线
 *   - esp_lcd_new_panel_co5300   ← v6 新 API (不是 esp_lcd_new_panel_io_co5300)
 *   - esp_lcd_touch_new_i2c_cst9220 需要 esp_lcd_panel_io_handle_t（LCD I2C 包装）
 */

#include "peripherals.h"
#include "mosaico_pins.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

/* ─── v6 esp_lcd 头文件（新路径） ────────────────────────── */
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9220.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PERIPH";

/* ─── 板型检测（对齐官方 BSP）─────────────────────────────
 * eFuse USER_DATA 存硬件版本：0x0100 = V1.0，0x0101/0x0102 = V1.2。
 * 两块板 LCD 的 SCL / RST 是互换的，接错必然黑屏。
 */
static bool s_is_v1_0 = false;

static void detect_board_variant(void)
{
    uint16_t ver = 0;
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &ver, sizeof(ver) * 8);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "eFuse USER_DATA read failed: %s, assume V1.2", esp_err_to_name(err));
        s_is_v1_0 = false;
        return;
    }
    s_is_v1_0 = (ver == 0x0100);
    ESP_LOGI(TAG, "Board HW version raw=0x%04X -> %s", (unsigned)ver, s_is_v1_0 ? "V1.0" : "V1.2");
}

/* ─── 全局句柄 ──────────────────────────────────────────── */
esp_lcd_touch_handle_t s_touch = NULL;
i2c_master_bus_handle_t s_i2c0_bus = NULL;  // 对内 (板载)
i2c_master_bus_handle_t s_i2c1_bus = NULL;  // 对外 (Interaction Module)
static esp_lcd_panel_io_handle_t s_touch_io = NULL;  // CST9220 的 LCD I2C 包装

/* ─── 电源 ─────────────────────────────────────────────── */

void peripherals_init_power(void)
{
    // GPIO60 低电平启用屏幕 + 扩展 3.3V
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VCC_3V3_CTRL_GPIO) | (1ULL << MOTOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(VCC_3V3_CTRL_GPIO, 0);  // 低有效，打开
    gpio_set_level(MOTOR_GPIO, 0);         // 振动马达默认关闭
    ESP_LOGI(TAG, "Power rails enabled (VCC_3V3_CTRL=LOW, MOTOR=OFF)");
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* ─── 振动马达 ──────────────────────────────────────────── */

void motor_turn_on(void)   { gpio_set_level(MOTOR_GPIO, 1); }
void motor_turn_off(void)  { gpio_set_level(MOTOR_GPIO, 0); }
void motor_pulse(int ms)
{
    motor_turn_on();
    vTaskDelay(pdMS_TO_TICKS(ms));
    motor_turn_off();
}

/* ─── I2C0 (对内，板载) ─────────────────────────────────── */

esp_err_t peripherals_init_i2c0(void)
{
    if (s_i2c0_bus) return ESP_OK;

    detect_board_variant();
    /* V1.0 主板 I2C 在 GPIO0/1；V1.2 移到 GPIO56/3（官方 BSP） */
    const gpio_num_t sda = s_is_v1_0 ? 0 : MOSAICO_I2C0_SDA_GPIO;
    const gpio_num_t scl = s_is_v1_0 ? 1 : MOSAICO_I2C0_SCL_GPIO;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c0_bus), TAG, "I2C0 bus");
    ESP_LOGI(TAG, "I2C0 (internal) ready SDA=%d SCL=%d", sda, scl);
    return ESP_OK;
}

/* ─── I2C1 (对外，Interaction Module) ──────────────────── */

esp_err_t peripherals_init_i2c1(void)
{
    if (s_i2c1_bus) return ESP_OK;

    detect_board_variant();
    if (s_is_v1_0) {
        /* V1.0 子板与主板共用同一组 I2C 引脚 */
        if (s_i2c0_bus == NULL) {
            ESP_RETURN_ON_ERROR(peripherals_init_i2c0(), TAG, "I2C0 for subboard");
        }
        s_i2c1_bus = s_i2c0_bus;
        ESP_LOGI(TAG, "I2C1 shares I2C0 on V1.0");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = MOSAICO_I2C1_SDA_GPIO,
        .scl_io_num = MOSAICO_I2C1_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c1_bus), TAG, "I2C1 bus");
    ESP_LOGI(TAG, "I2C1 (external) ready SDA=%d SCL=%d", MOSAICO_I2C1_SDA_GPIO, MOSAICO_I2C1_SCL_GPIO);
    return ESP_OK;
}

i2c_master_bus_handle_t peripherals_get_i2c0(void) { return s_i2c0_bus; }
i2c_master_bus_handle_t peripherals_get_i2c1(void) { return s_i2c1_bus; }

/* ─── 0. CO5300 vendor config (从 esp-mosaico-claw 官方抄) ─
 *
 * 官方 LCD init 命令 + QSPI 模式使能
 */
static const co5300_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0x11, NULL, 0, 600},              // Sleep Out，官方等待 600ms（CO5300 要求 ≥120ms）
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
};
static const co5300_vendor_config_t s_co5300_vendor_cfg = {
    .init_cmds = s_lcd_init_cmds,
    .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
    .flags.use_qspi_interface = 1,  // ★ 关键：告诉 CO5300 驱动走 QSPI
};

/* ─── LCD CO5300 (QSPI, ESP-IDF v6 API) ────────────────── */

static peripherals_lcd_flush_done_cb_t s_lcd_flush_done_cb;
static void *s_lcd_flush_done_ctx;

void peripherals_lcd_set_flush_done_cb(peripherals_lcd_flush_done_cb_t cb, void *ctx)
{
    s_lcd_flush_done_cb = cb;
    s_lcd_flush_done_ctx = ctx;
}

static bool IRAM_ATTR lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t io,
                                              esp_lcd_panel_io_event_data_t *edata,
                                              void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    if (s_lcd_flush_done_cb) {
        s_lcd_flush_done_cb(s_lcd_flush_done_ctx);
    }
    return false;
}

esp_err_t peripherals_init_lcd(esp_lcd_panel_handle_t *out_panel)
{
    esp_err_t ret;

    // ── 0. 板型 → LCD SCL/RST 引脚（V1.0 与 V1.2 互换）──
    detect_board_variant();
    const gpio_num_t lcd_scl = s_is_v1_0 ? 44 : 42;
    const gpio_num_t lcd_rst = s_is_v1_0 ? 42 : 44;

    // ── 1. LCD 电源复位 ──
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << lcd_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);
    gpio_set_level(lcd_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(lcd_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(lcd_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // ── 2. SPI Bus (CO5300 用 QSPI，4 线) ──
    // v6 spi_bus_config_t 用 data0-3，自动隐式映射到 mosi/miso/quadwp/quadhd
    spi_bus_config_t bus_cfg = {
        .data0_io_num       = LCD_DATA0_GPIO,
        .data1_io_num       = LCD_DATA1_GPIO,
        .data2_io_num       = LCD_DATA2_GPIO,
        .data3_io_num       = LCD_DATA3_GPIO,
        .sclk_io_num        = lcd_scl,
        .max_transfer_sz    = LCD_WIDTH * LCD_HEIGHT * 2 + 8,
        .flags              = SPICOMMON_BUSFLAG_MASTER,
    };
    ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "SPI bus init");

    // 官方做法：QSPI 总线引脚驱动能力设为 2（GPIO_DRIVE_CAP_DEFAULT）
    {
        const gpio_num_t qspi_pins[] = {
            lcd_scl, LCD_DATA0_GPIO, LCD_DATA1_GPIO, LCD_DATA2_GPIO, LCD_DATA3_GPIO,
        };
        for (size_t i = 0; i < sizeof(qspi_pins) / sizeof(qspi_pins[0]); i++) {
            gpio_set_drive_capability(qspi_pins[i], GPIO_DRIVE_CAP_2);
        }
    }

    // ── 3. LCD Panel IO (SPI QSPI 模式) ──
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num     = LCD_CS_GPIO,
        .dc_gpio_num     = -1,        // QSPI 无 DC 脚
        .spi_mode        = 0,
        .pclk_hz         = 40 * 1000 * 1000,   // 官方 CO5300 QSPI 固定 40MHz
        .trans_queue_depth = 10,
        .lcd_cmd_bits    = 32,
        .lcd_param_bits  = 8,
        .on_color_trans_done = NULL,
        .user_ctx        = NULL,
        .flags.quad_mode = 1,         // ★ v6: 启用 QSPI 4 线模式
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_cfg, &io_handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "LCD panel io spi");

    const esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = lcd_color_trans_done_cb,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, NULL),
                      err, TAG, "LCD panel io callbacks");

    // ── 4. CO5300 面板 (v6 API: esp_lcd_new_panel_co5300) ──
    esp_lcd_panel_dev_config_t panel_cfg = {
        .rgb_ele_order    = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel   = 16,
        .reset_gpio_num   = lcd_rst,
        .vendor_config    = (void *)&s_co5300_vendor_cfg,  // ★ 官方写法
        .flags.reset_active_high = 0,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ret = esp_lcd_new_panel_co5300(io_handle, &panel_cfg, &panel);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "CO5300 panel");

    // ── 5. 基础操作 ──
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_set_gap(panel, 0, 0);
    esp_lcd_panel_invert_color(panel, false);
    /* 逆时针 90°（官方 BSP ROTATE_270 的 flag 组合） */
    esp_lcd_panel_swap_xy(panel, true);
    esp_lcd_panel_mirror(panel, false, true);
    esp_lcd_panel_disp_on_off(panel, true);

    ESP_LOGI(TAG, "LCD CO5300 %dx%d init OK (SCL=%d RST=%d RST_ON_HOST=SPI3)",
             LCD_WIDTH, LCD_HEIGHT, lcd_scl, lcd_rst);
    *out_panel = panel;
    return ESP_OK;

err:
    ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(ret));
    return ret;
}

/* ─── Touch CST9220 (ESP-IDF v6 API) ────────────────────
 *
 * v6 的 esp_lcd_touch_new_i2c_cst9220() 需要 esp_lcd_panel_io_handle_t，
 * 不是直接的 i2c_master_bus_handle_t。所以先包一层 LCD I2C Panel IO。
 */

esp_err_t peripherals_init_touch(void)
{
    if (!s_i2c0_bus) {
        ESP_LOGE(TAG, "I2C0 must be init before touch");
        return ESP_ERR_INVALID_STATE;
    }

    // ── 1. 创建 LCD I2C Panel IO 包装 ──
    esp_lcd_panel_io_i2c_config_t i2c_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    i2c_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST9220_ADDRESS;
    esp_err_t ret = esp_lcd_new_panel_io_i2c(s_i2c0_bus, &i2c_io_cfg, &s_touch_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "touch i2c io: %s", esp_err_to_name(ret));
        return ret;
    }

    // ── 2. CST9220 驱动 ──
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = -1,             // 官方 BSP：触摸 RST = NC（不共用 LCD 复位）
        .int_gpio_num = TP_INT_GPIO,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    ret = esp_lcd_touch_new_i2c_cst9220(s_touch_io, &tp_cfg, &s_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CST9220 not found: %s", esp_err_to_name(ret));
        return ret;
    }
    /* 触摸坐标必须与 LCD 同步旋转，否则点击位置会错位。 */
    esp_lcd_touch_set_swap_xy(s_touch, true);
    esp_lcd_touch_set_mirror_x(s_touch, false);
    esp_lcd_touch_set_mirror_y(s_touch, true);
    ESP_LOGI(TAG, "CST9220 touch init OK @0x%02X (CCW90)",
             ESP_LCD_TOUCH_IO_I2C_CST9220_ADDRESS);
    return ESP_OK;
}

esp_lcd_touch_handle_t peripherals_get_touch(void) { return s_touch; }

/* ─── 一键全初始化 ──────────────────────────────────────── */

esp_err_t peripherals_init_all(void)
{
    ESP_LOGW(TAG, "=== peripherals_init_all ===");
    peripherals_init_power();

    esp_err_t e0 = peripherals_init_i2c0();
    if (e0 != ESP_OK) ESP_LOGE(TAG, "I2C0 init failed: %s", esp_err_to_name(e0));
    esp_err_t e1 = peripherals_init_i2c1();
    if (e1 != ESP_OK) ESP_LOGE(TAG, "I2C1 init failed: %s", esp_err_to_name(e1));

    ESP_LOGI(TAG, "I2C0 %s (板载 BMI270/CST9220/ES8311)", e0 == ESP_OK ? "ready" : "FAILED");
    ESP_LOGI(TAG, "I2C1 %s (Interaction Module EEPROM)", e1 == ESP_OK ? "ready" : "FAILED");

    // I2C 总线扫描跳过（i2c_master_transmit_receive 需要 dev_handle_t，
    // 直接对 bus handle 调用会报类型错误。需要探测设备时改用 i2c_master_probe）

    return ESP_OK;
}
