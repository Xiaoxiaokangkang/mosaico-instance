/**
 * interaction_module.c — ESP-Mosaico 多功能交互模块 v0.3
 *
 * 使用：
 *   - 两块机械/触摸焊盘（本固件读取机械触点，轻按即可）
 *   - GL5506 光敏
 *   - 6 颗 WS2812
 *
 * PIR 与 IR 在 v0.3 中明确不参与玩法，因此这里不初始化、不暴露接口。
 * 模块自动识别左槽 H2 / 右槽 H1，并按槽位选择引脚。
 */

#include "interaction_module.h"
#include "peripherals.h"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_adc/adc_oneshot.h"
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "IM";

#define RMT_RESOLUTION_HZ  (10 * 1000 * 1000)
#define IM_I2C_ADDR_LEFT   0x50
#define IM_I2C_ADDR_RIGHT  0x51

/* 左槽 H2 的逻辑 GPIO。右槽是镜像。 */
#define IM_LEFT_LDR_GPIO   53
#define IM_LEFT_KEYL_GPIO  13
#define IM_LEFT_KEYR_GPIO  12
#define IM_LEFT_WS_GPIO    15

#define IM_RIGHT_LDR_GPIO  46
#define IM_RIGHT_KEYL_GPIO 11
#define IM_RIGHT_KEYR_GPIO 10
#define IM_RIGHT_WS_GPIO   38

#define IM_ADDR_SEL_LEFT_GPIO  14
#define IM_ADDR_SEL_RIGHT_GPIO 39

/* ESP32-S31 only has one ADC attenuation option; using DB_12 there makes
 * adc_oneshot_config_channel() fail with ESP_ERR_INVALID_ARG. */
#if SOC_ADC_ATTEN_NUM <= 1
#define IM_ADC_ATTEN  ADC_ATTEN_DB_0
#else
#define IM_ADC_ATTEN  ADC_ATTEN_DB_12
#endif

typedef struct {
    gpio_num_t ldr;
    gpio_num_t key_l;
    gpio_num_t key_r;
    gpio_num_t ws;
} im_pins_t;

static im_state_t s_state;
static im_pins_t s_pins;
static bool s_ws_ready;
static bool s_ldr_ready;
static adc_oneshot_unit_handle_t s_adc;
static adc_unit_t s_adc_unit;
static adc_channel_t s_adc_channel;
static adc_oneshot_unit_handle_t s_adc_alt;
static adc_unit_t s_adc_unit_alt;
static adc_channel_t s_adc_channel_alt;
static bool s_ldr_alt_ready;
static rmt_channel_handle_t s_rmt_chan;
static rmt_encoder_handle_t s_rmt_enc;
static QueueHandle_t s_pad_queue;

/* ─── slot discovery ───────────────────────────────────── */

static void configure_address_select(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << IM_ADDR_SEL_LEFT_GPIO) | (1ULL << IM_ADDR_SEL_RIGHT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(IM_ADDR_SEL_LEFT_GPIO, 0);   /* 左槽 EEPROM 0x50 */
    gpio_set_level(IM_ADDR_SEL_RIGHT_GPIO, 1);  /* 右槽 EEPROM 0x51 */
    vTaskDelay(pdMS_TO_TICKS(10));
}

static bool detect_module_slot(bool *right)
{
    *right = true;
    i2c_master_bus_handle_t bus = peripherals_get_i2c1();
    if (!bus) {
        s_state.module_present = false;
        return false;
    }

    esp_err_t r51 = i2c_master_probe(bus, IM_I2C_ADDR_RIGHT, 50);
    esp_err_t r50 = i2c_master_probe(bus, IM_I2C_ADDR_LEFT, 50);
    if (r51 == ESP_OK) {
        *right = true;
        s_state.module_present = true;
        return true;
    }
    if (r50 == ESP_OK) {
        *right = false;
        s_state.module_present = true;
        return true;
    }
    s_state.module_present = false;
    return false;
}

/* ─── LDR ──────────────────────────────────────────────── */

static esp_err_t adc_init_pin(int gpio, adc_oneshot_unit_handle_t *out_handle,
                              adc_unit_t *out_unit, adc_channel_t *out_channel)
{
    esp_err_t err = adc_oneshot_io_to_channel(gpio, out_unit, out_channel);
    if (err != ESP_OK) return err;
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = *out_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&unit_cfg, out_handle);
    if (err != ESP_OK) return err;
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = IM_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_oneshot_config_channel(*out_handle, *out_channel, &chan_cfg);
}

static esp_err_t init_ldr(void)
{
    esp_err_t err = adc_init_pin((int)s_pins.ldr, &s_adc, &s_adc_unit, &s_adc_channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LDR GPIO%d init failed: %s", s_pins.ldr, esp_err_to_name(err));
        return err;
    }
    s_ldr_ready = true;
    s_state.ldr_channel = (int)s_adc_channel;
    ESP_LOGI(TAG, "LDR ready GPIO%d (unit=%d ch=%d)", s_pins.ldr, s_adc_unit, s_adc_channel);

    /* Diagnostic: also sample the other slot's LDR pin, so we can see which
     * pin actually responds when the sensor is covered. */
    const int alt_gpio = (s_pins.ldr == IM_RIGHT_LDR_GPIO) ? IM_LEFT_LDR_GPIO : IM_RIGHT_LDR_GPIO;
    err = adc_init_pin(alt_gpio, &s_adc_alt, &s_adc_unit_alt, &s_adc_channel_alt);
    if (err == ESP_OK) {
        s_ldr_alt_ready = true;
        s_state.ldr_channel_alt = (int)s_adc_channel_alt;
        ESP_LOGI(TAG, "LDR alt GPIO%d ready (unit=%d ch=%d)", alt_gpio, s_adc_unit_alt, s_adc_channel_alt);
    } else {
        ESP_LOGW(TAG, "LDR alt GPIO%d init failed: %s", alt_gpio, esp_err_to_name(err));
    }
    return ESP_OK;
}

float im_light_pct(void)
{
    return s_state.light_pct;
}

static uint8_t normalize_ldr(int raw)
{
#if CONFIG_IDF_TARGET_ESP32S31
    /* Binary day/night state. The old 2196 endpoint was far too sensitive:
     * measured bright ~114, covered ~36600-46900. Require a much darker raw
     * value before declaring night. Even channels are N-side (raw rises in
     * the dark); odd channels are P-side (raw falls in the dark). */
    #define IM_LDR_DARK_RAW 30000
    const bool inverted = (((int)s_adc_channel & 1) == 0);
    const bool dark = inverted ? (raw > IM_LDR_DARK_RAW) : (raw < IM_LDR_DARK_RAW);
    return dark ? 0 : 100;
#else
    return (raw < 2048) ? 0 : 100;
#endif
}

static void sample_ldr(void)
{
    if (!s_ldr_ready || !s_adc) return;
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, s_adc_channel, &raw) == ESP_OK) sum += raw;
    }

    static int filtered_q8;
    static bool filter_valid;
    const int target_q8 = (int)normalize_ldr(sum / 8) * 256;
    if (!filter_valid) {
        filtered_q8 = target_q8;
        filter_valid = true;
    } else {
        filtered_q8 = (filtered_q8 * 3 + target_q8 + 2) / 4;
    }
    s_state.light_pct = (float)((filtered_q8 + 128) / 256);
    s_state.light_valid = true;
    s_state.ldr_raw = sum / 8;
    s_state.ldr_channel = (int)s_adc_channel;

    if (s_ldr_alt_ready && s_adc_alt) {
        int alt = 0;
        if (adc_oneshot_read(s_adc_alt, s_adc_channel_alt, &alt) == ESP_OK) {
            s_state.ldr_raw_alt = alt;
            s_state.ldr_channel_alt = (int)s_adc_channel_alt;
        }
    }
}

/* ─── keys ─────────────────────────────────────────────── */

static esp_err_t init_keys(void)
{
    gpio_reset_pin(s_pins.key_l);
    gpio_reset_pin(s_pins.key_r);
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << s_pins.key_l) | (1ULL << s_pins.key_r),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "key gpio");
    s_state.pad_l = false;
    s_state.pad_r = false;
    ESP_LOGI(TAG, "Pads ready KEY_L=GPIO%d KEY_R=GPIO%d", s_pins.key_l, s_pins.key_r);
    return ESP_OK;
}

/* ─── pad sampling task ──────────────────────────────────
 * Input must not depend on the UI/render loop. This task polls the pads
 * every 2 ms, debounces 8 ms, and queues every edge with its timestamp.
 */
static void pad_task(void *arg)
{
    (void)arg;
    uint8_t last_l = gpio_get_level(s_pins.key_l);
    uint8_t last_r = gpio_get_level(s_pins.key_r);
    bool stable_l = (last_l == 0);
    bool stable_r = (last_r == 0);
    uint32_t change_l_ms = 0;
    uint32_t change_r_ms = 0;
    s_state.pad_l = stable_l;
    s_state.pad_r = stable_r;

    while (1) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        const uint8_t l = gpio_get_level(s_pins.key_l);
        const uint8_t r = gpio_get_level(s_pins.key_r);

        if (l != last_l) {
            last_l = l;
            change_l_ms = now;
        }
        if (r != last_r) {
            last_r = r;
            change_r_ms = now;
        }

        if (now - change_l_ms >= 20) {
            const bool down = (l == 0);
            if (down != stable_l) {
                stable_l = down;
                s_state.pad_l = down;
                const im_pad_event_t ev = { .left = true, .down = down, .timestamp_ms = now };
                if (s_pad_queue) (void)xQueueSend(s_pad_queue, &ev, 0);
            }
        }
        if (now - change_r_ms >= 20) {
            const bool down = (r == 0);
            if (down != stable_r) {
                stable_r = down;
                s_state.pad_r = down;
                const im_pad_event_t ev = { .left = false, .down = down, .timestamp_ms = now };
                if (s_pad_queue) (void)xQueueSend(s_pad_queue, &ev, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ─── WS2812 ───────────────────────────────────────────── */

static esp_err_t init_ws2812(void)
{
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = s_pins.ws,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 256,
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags.with_dma = false,
        .flags.init_level = 0,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_rmt_chan), TAG, "rmt tx");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rmt_chan), TAG, "rmt enable");

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0.level0 = 1, .bit0.duration0 = 4,
        .bit0.level1 = 0, .bit0.duration1 = 8,
        .bit1.level0 = 1, .bit1.duration0 = 8,
        .bit1.level1 = 0, .bit1.duration1 = 5,
        .flags.msb_first = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&enc_cfg, &s_rmt_enc), TAG, "rmt encoder");
    s_ws_ready = true;
    im_leds_clear();
    ESP_LOGI(TAG, "WS2812 x%d ready GPIO%d", IM_LED_COUNT, s_pins.ws);
    return ESP_OK;
}

void im_leds_write(const im_rgb_t colors[IM_LED_COUNT])
{
    if (!s_ws_ready || !colors) return;
    uint8_t payload[IM_LED_COUNT * 3];
    for (int i = 0; i < IM_LED_COUNT; i++) {
        payload[i * 3 + 0] = colors[i].g;
        payload[i * 3 + 1] = colors[i].r;
        payload[i * 3 + 2] = colors[i].b;
    }
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 1,
        .flags.queue_nonblocking = false,
    };
    esp_err_t err = rmt_transmit(s_rmt_chan, s_rmt_enc, payload, sizeof(payload), &tx_cfg);
    if (err == ESP_OK) {
        rmt_tx_wait_all_done(s_rmt_chan, pdMS_TO_TICKS(20));
    }
}

void im_leds_fill(uint8_t r, uint8_t g, uint8_t b)
{
    im_rgb_t colors[IM_LED_COUNT];
    for (int i = 0; i < IM_LED_COUNT; i++) {
        colors[i] = (im_rgb_t){ .r = r, .g = g, .b = b };
    }
    im_leds_write(colors);
}

void im_leds_clear(void)
{
    im_leds_fill(0, 0, 0);
}

void im_leds_show_stage(int stage, bool ok)
{
    im_rgb_t colors[IM_LED_COUNT];
    for (int i = 0; i < IM_LED_COUNT; i++) {
        if (i < stage) colors[i] = (im_rgb_t){ .r = 0, .g = 45, .b = 0 };
        else if (i == stage) {
            colors[i] = ok ? (im_rgb_t){ .r = 0, .g = 220, .b = 20 }
                           : (im_rgb_t){ .r = 220, .g = 0, .b = 0 };
        } else {
            colors[i] = (im_rgb_t){ .r = 0, .g = 0, .b = 0 };
        }
    }
    im_leds_write(colors);
}

/* ─── init / tick ──────────────────────────────────────── */

esp_err_t im_init_all(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.ldr_raw = -1;
    s_state.ldr_channel = -1;
    s_state.ldr_raw_alt = -1;
    s_state.ldr_channel_alt = -1;
    configure_address_select();

    bool right = true;
    detect_module_slot(&right);
    s_state.slot_right = right;
    snprintf(s_state.slot_name, sizeof(s_state.slot_name), "%s", s_state.module_present ? (right ? "R" : "L") : "NONE");

    if (right) {
        s_pins = (im_pins_t){
            .ldr = IM_RIGHT_LDR_GPIO,
            .key_l = IM_RIGHT_KEYL_GPIO,
            .key_r = IM_RIGHT_KEYR_GPIO,
            .ws = IM_RIGHT_WS_GPIO,
        };
    } else {
        s_pins = (im_pins_t){
            .ldr = IM_LEFT_LDR_GPIO,
            .key_l = IM_LEFT_KEYL_GPIO,
            .key_r = IM_LEFT_KEYR_GPIO,
            .ws = IM_LEFT_WS_GPIO,
        };
    }

    if (!s_state.module_present) {
        ESP_LOGW(TAG, "No interaction module EEPROM found — UI fallback only");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Interaction module on %s slot (EEPROM 0x%02X)",
             s_state.slot_right ? "RIGHT" : "LEFT",
             s_state.slot_right ? IM_I2C_ADDR_RIGHT : IM_I2C_ADDR_LEFT);

    if (init_keys() != ESP_OK) ESP_LOGE(TAG, "key init failed");
    if (init_ldr() != ESP_OK) ESP_LOGE(TAG, "LDR init failed");
    if (init_ws2812() != ESP_OK) ESP_LOGE(TAG, "WS2812 init failed");

    s_pad_queue = xQueueCreate(32, sizeof(im_pad_event_t));
    if (!s_pad_queue) {
        ESP_LOGE(TAG, "pad queue alloc failed");
    } else if (xTaskCreatePinnedToCore(pad_task, "im_pads", 3072, NULL, 6, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "pad task create failed");
    }
    return ESP_OK;
}

void im_tick(uint32_t now_ms)
{
    if (!s_state.module_present) return;
    static uint32_t last_ldr_ms;
    if (now_ms - last_ldr_ms >= 200) {
        last_ldr_ms = now_ms;
        sample_ldr();
    }
}

bool im_pad_event_pop(im_pad_event_t *out)
{
    if (!out || !s_pad_queue) return false;
    return xQueueReceive(s_pad_queue, out, 0) == pdTRUE;
}

const im_state_t *im_get_state(void) { return &s_state; }
bool im_module_present(void) { return s_state.module_present; }
