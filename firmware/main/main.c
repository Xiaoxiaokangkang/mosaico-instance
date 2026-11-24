/**
 * main.c — 星芽小屋 · ESP-Mosaico v0.3
 *
 * 交互：
 *   左垫短触        喂食
 *   右垫短触        开始三轨道接星星
 *   双垫按住 2 秒    抱抱（有 220ms 仲裁与状态互斥）
 *   光敏亮/暗        亮=醒，暗=睡（唯一状态来源）
 *   AI 键短按       呼唤/应答
 *   AI 键长按 1.2s  静音
 *   PIR / IR        不启用（按 v0.3 方案删除）
 *
 * 当前固件不包含 ES8311 音频输出；声音反馈留作下一阶段。
 * 振动、6 颗 WS2812、屏幕、触摸屏和 LDR 已接入。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "interaction_module.h"
#include "mosaico_pins.h"
#include "peripherals.h"
#include "pet_core.h"
#include "pet_store.h"
#include "pet_ui.h"

static const char *TAG = "STAR_PET";

static pet_core_t s_core;
static uint32_t s_motor_until_ms;
static uint32_t s_last_core_tick_ms;
static uint32_t s_last_ui_ms;
static uint32_t s_last_led_ms;
static uint32_t s_last_save_ms;
static uint32_t s_last_light_ms;
static uint32_t s_last_heartbeat_ms;
static bool s_prev_needs_name = true;
static bool s_save_requested;

/* ─── haptic ───────────────────────────────────────────── */

static void request_motor_pulse(uint32_t ms)
{
    uint32_t until = (uint32_t)(esp_timer_get_time() / 1000) + ms;
    if (until > s_motor_until_ms) s_motor_until_ms = until;
}

/* ─── events → text / haptic / save ────────────────────── */

static void on_pet_event(pet_event_t event, void *user_data)
{
    (void)user_data;
    switch (event) {
        case PET_EV_FEED_OK:
            pet_ui_show_toast("啊呜啊呜，好满足！");
            request_motor_pulse(90);
            s_save_requested = true;
            break;
        case PET_EV_FEED_BLOCKED:
            pet_ui_show_toast("小肚子圆滚滚啦，真的吃饱啦");
            break;
        case PET_EV_FEED_COOLDOWN:
            pet_ui_show_toast("还在嚼呢，等等再喂～");
            break;
        case PET_EV_HUG_DONE:
            pet_ui_show_toast("嘿嘿，好舒服……");
            request_motor_pulse(140);
            s_save_requested = true;
            break;
        case PET_EV_HUG_COOLDOWN:
            pet_ui_show_toast("再抱一会儿也很好");
            break;
        case PET_EV_GAME_START:
            pet_ui_show_toast("开局 -10 精力，左垫向左 / 右垫向右");
            request_motor_pulse(80);
            break;
        case PET_EV_LOW_ENERGY:
            pet_ui_show_toast("精力不够，先休息一会儿吧");
            break;
        case PET_EV_GAME_STAR:
            request_motor_pulse(30);
            break;
        case PET_EV_GAME_WON:
            pet_ui_show_toast("接住 6 颗星啦！");
            request_motor_pulse(260);
            s_save_requested = true;
            break;
        case PET_EV_GAME_LOST:
            pet_ui_show_toast("差一点点，失败不扣属性");
            s_save_requested = true;
            break;
        case PET_EV_GAME_ABORTED:
            s_save_requested = true;
            break;
        case PET_EV_SLEEP:
            pet_ui_show_toast("睡一会儿啦");
            request_motor_pulse(170);
            s_save_requested = true;
            break;
        case PET_EV_WAKE:
            pet_ui_show_toast("醒来啦");
            request_motor_pulse(110);
            s_save_requested = true;
            break;
        case PET_EV_EVOLVE:
            pet_ui_show_toast("它长大了一点点");
            request_motor_pulse(300);
            s_save_requested = true;
            break;
        case PET_EV_IDLE_REMINDER:
            pet_ui_show_toast("你在吗？陪我坐一会儿嘛～");
            request_motor_pulse(120);
            break;
        case PET_EV_PAD_HINT:
            pet_ui_show_toast("两只手一起按住，才是抱抱哦");
            break;
        default:
            break;
    }
}

/* ─── LEDs ─────────────────────────────────────────────── */

typedef struct {
    im_rgb_t pixels[IM_LED_COUNT];
} led_frame_t;

static led_frame_t s_last_frame;

static im_rgb_t form_color(pet_form_t form)
{
    switch (form) {
        case PET_FORM_ACTIVE: return (im_rgb_t){ .r = 255, .g = 174, .b = 86 };
        case PET_FORM_ROUND:  return (im_rgb_t){ .r = 245, .g = 145, .b = 177 };
        case PET_FORM_CLINGY: return (im_rgb_t){ .r = 171, .g = 156, .b = 240 };
        default:              return (im_rgb_t){ .r = 116, .g = 216, .b = 183 };
    }
}

static uint8_t scale8(uint8_t v, uint8_t mul)
{
    return (uint8_t)(((uint16_t)v * mul) / 255);
}

static im_rgb_t scaled(im_rgb_t c, uint8_t mul)
{
    return (im_rgb_t){ .r = scale8(c.r, mul), .g = scale8(c.g, mul), .b = scale8(c.b, mul) };
}

static void update_leds(uint32_t now_ms)
{
    if (!im_module_present()) return;

    const im_state_t *im = im_get_state();
    pet_state_t *s = &s_core.state;
    led_frame_t f;
    memset(&f, 0, sizeof(f));

    im_rgb_t fc = form_color(s->form);
    uint32_t phase = (now_ms / 220) % 12;
    uint8_t breath = (uint8_t)(110 + (phase < 6 ? phase : 12 - phase) * 12);
    bool flash_left = s->pad_down[PET_PAD_LEFT];
    bool flash_right = s->pad_down[PET_PAD_RIGHT];

    if (s->mode == PET_MODE_SLEEP || s->sleeping) {
        for (int i = 0; i < IM_LED_COUNT; i++) {
            f.pixels[i] = (im_rgb_t){ .r = 18, .g = 9, .b = 4 };
        }
        f.pixels[0] = (im_rgb_t){ .r = 24, .g = 12, .b = 5 };
    } else if (s->mode == PET_MODE_GAME || s->mode == PET_MODE_GAME_PAUSED) {
        for (int i = 0; i < IM_LED_COUNT; i++) {
            if (i < s->game_stars) {
                f.pixels[i] = (im_rgb_t){ .r = 240, .g = 190, .b = 65 };
            } else if (s->game_star_visible && i == (s->game_stars % IM_LED_COUNT)) {
                f.pixels[i] = (im_rgb_t){ .r = 255, .g = 245, .b = 185 };
            } else {
                f.pixels[i] = (im_rgb_t){ .r = 35, .g = 38, .b = 34 };
            }
        }
    } else if (s->mode == PET_MODE_RESULT) {
        im_rgb_t c = (s->result == PET_RESULT_SUCCESS)
                     ? (im_rgb_t){ .r = 100, .g = 230, .b = 130 }
                     : (im_rgb_t){ .r = 100, .g = 150, .b = 230 };
        for (int i = 0; i < IM_LED_COUNT; i++) f.pixels[i] = c;
    } else if (s->mode == PET_MODE_EVOLVE) {
        for (int i = 0; i < IM_LED_COUNT; i++) {
            f.pixels[i] = (i == (int)(phase % IM_LED_COUNT)) ? (im_rgb_t){ .r = 255, .g = 250, .b = 220 }
                                                              : scaled(fc, 180);
        }
    } else if (s->mode == PET_MODE_HUG) {
        for (int i = 0; i < IM_LED_COUNT; i++) f.pixels[i] = (im_rgb_t){ .r = 245, .g = 120, .b = 160 };
    } else if (s->mode == PET_MODE_EATING) {
        for (int i = 0; i < IM_LED_COUNT; i++) f.pixels[i] = (im_rgb_t){ .r = 240, .g = 160, .b = 55 };
    } else {
        im_rgb_t idle = scaled(fc, breath);
        for (int i = 0; i < IM_LED_COUNT; i++) f.pixels[i] = idle;
        if (s->satiety < 30) {
            uint8_t pulse = (phase < 6) ? 210 : 90;
            for (int i = 0; i < IM_LED_COUNT; i++) f.pixels[i] = (im_rgb_t){ .r = 225, .g = 135, .b = 35 };
            f.pixels[0] = (im_rgb_t){ .r = pulse, .g = (uint8_t)(pulse / 2), .b = 0 };
        } else if (s->energy < 20) {
            for (int i = 0; i < IM_LED_COUNT; i++) f.pixels[i] = (im_rgb_t){ .r = 30, .g = 70, .b = 150 };
        }
    }

    /* 左右垫触摸应答：短暂覆盖对应 LED。 */
    if (flash_left) f.pixels[5] = (im_rgb_t){ .r = 240, .g = 240, .b = 225 };
    if (flash_right) f.pixels[4] = (im_rgb_t){ .r = 240, .g = 240, .b = 225 };

    /* 环境光越暗，灯越暗。 */
    float light = (im->light_valid ? im->light_pct : 70.0f);
    if (light < 0) light = 0;
    if (light > 100) light = 100;
    uint8_t brightness = (uint8_t)(70 + light * 1.2f); /* 70..190 */
    if (s->sleeping) brightness = 45;

    for (int i = 0; i < IM_LED_COUNT; i++) f.pixels[i] = scaled(f.pixels[i], brightness);

    if (memcmp(&f, &s_last_frame, sizeof(f)) != 0) {
        im_leds_write(f.pixels);
        s_last_frame = f;
    }
    s_last_led_ms = now_ms;
}

/* ─── AI button ────────────────────────────────────────── */

static void init_ai_button(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << AI_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void handle_ai_button(uint32_t now_ms)
{
    static bool prev = true;
    static uint32_t down_ms;
    static bool long_fired;

    bool level = gpio_get_level(AI_BUTTON_GPIO) == 0;
    if (level && !prev) {
        down_ms = now_ms;
        long_fired = false;
    }
    if (level && !long_fired && now_ms - down_ms >= 1200) {
        long_fired = true;
        bool next = !s_core.state.muted;
        pet_core_set_muted(&s_core, next);
        pet_ui_show_toast(next ? "已静音" : "声音打开了");
        request_motor_pulse(70);
        s_save_requested = true;
    }
    if (!level && prev) {
        if (!long_fired && now_ms - down_ms < 900) {
            pet_core_note_screen_touch(&s_core, now_ms);
            pet_ui_show_toast(s_core.state.sleeping ? "我在……再睡一会儿" : "我在呢");
            request_motor_pulse(60);
        }
    }
    prev = level;
}

/* ─── main ─────────────────────────────────────────────── */

static bool init_hardware(esp_lcd_panel_handle_t *out_panel)
{
    peripherals_init_power();
    vTaskDelay(pdMS_TO_TICKS(250));

    esp_err_t err = peripherals_init_all();
    if (err != ESP_OK) ESP_LOGE(TAG, "peripherals_init_all: %s", esp_err_to_name(err));

    esp_lcd_panel_handle_t panel = NULL;
    err = peripherals_init_lcd(&panel);
    if (err != ESP_OK || panel == NULL) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = peripherals_init_touch();
    if (err != ESP_OK) ESP_LOGW(TAG, "touch init failed: %s", esp_err_to_name(err));

    if (im_init_all() != ESP_OK) {
        ESP_LOGW(TAG, "interaction module unavailable; screen fallback enabled");
    }

    *out_panel = panel;
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 星芽小屋 v0.3 boot ===");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_lcd_panel_handle_t panel = NULL;
    if (!init_hardware(&panel)) {
        ESP_LOGE(TAG, "No display; stopping app");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (!pet_store_load(&s_core, now)) {
        pet_core_init(&s_core, now);
        /* 首次启动保留 needs_name，由 UI 的选择面板完成命名。 */
    }
    s_prev_needs_name = s_core.state.needs_name;

    pet_core_set_callbacks(&s_core, on_pet_event, NULL);
    init_ai_button();

    esp_err_t ui_err = pet_ui_init(panel, &s_core);
    if (ui_err != ESP_OK) {
        ESP_LOGE(TAG, "UI init failed: %s", esp_err_to_name(ui_err));
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    pet_ui_refresh();

    const im_state_t *im = im_get_state();
    s_last_core_tick_ms = now;
    s_last_ui_ms = now;
    s_last_led_ms = now;
    s_last_save_ms = now;
    s_last_light_ms = now;
    s_last_heartbeat_ms = now;

    ESP_LOGI(TAG, "=== ready: %s, module=%s ===",
             s_core.state.name, im->module_present ? im->slot_name : "NONE");

    for (;;) {
        now = (uint32_t)(esp_timer_get_time() / 1000);
        im_tick(now);
        handle_ai_button(now);

        const bool setup = pet_ui_setup_active();
        im_pad_event_t pad_ev;
        while (im_pad_event_pop(&pad_ev)) {
            if (!setup) {
                pet_core_pad_event(&s_core,
                                   pad_ev.left ? PET_PAD_LEFT : PET_PAD_RIGHT,
                                   pad_ev.down, pad_ev.timestamp_ms);
            }
        }

        im = im_get_state();
        if (!setup) {
            if (im->light_valid && now - s_last_light_ms >= 200) {
                s_last_light_ms = now;
                pet_core_update_light(&s_core, im->light_pct, now);
            }

            if (now - s_last_core_tick_ms >= 100) {
                s_last_core_tick_ms = now;
                pet_core_tick(&s_core, now);
            }
        }

        /* 抱起（hug）时的心跳节拍。 */
        if (s_core.state.mode == PET_MODE_HUG &&
            s_core.state.pad_down[PET_PAD_LEFT] &&
            s_core.state.pad_down[PET_PAD_RIGHT] &&
            now - s_last_heartbeat_ms > 500) {
            s_last_heartbeat_ms = now;
            request_motor_pulse(60);
        }

        if (now < s_motor_until_ms) motor_turn_on();
        else motor_turn_off();

        if (now - s_last_ui_ms >= 100) {
            s_last_ui_ms = now;
            pet_ui_refresh();
        }
        if (now - s_last_led_ms >= 100) {
            update_leds(now);
        }

        if (s_core.state.needs_name != s_prev_needs_name) {
            s_prev_needs_name = s_core.state.needs_name;
            if (!s_prev_needs_name) {
                s_last_core_tick_ms = now;
                s_save_requested = true;
            }
        }

        if (s_save_requested) {
            s_save_requested = false;
            pet_store_save(&s_core);
            s_last_save_ms = now;
        } else if (now - s_last_save_ms >= 5000) {
            s_last_save_ms = now;
            pet_store_save(&s_core);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
