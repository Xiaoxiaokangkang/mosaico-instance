/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pet_app.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"
#include "mosaico_module_interact.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pet_core.h"
#include "pet_ui.h"

static const char *TAG = "desk_pet";

#define PET_NVS_NAMESPACE "pet"
#define PET_NVS_KEY "state"

#define APP_QUEUE_LEN 32
#define APP_TASK_STACK 8192
#define APP_TASK_PRIORITY 5
#define APP_LOOP_MS 20
#define LIGHT_SAMPLE_MS 100
#define UI_REFRESH_MS 100
#define PRESENCE_CHECK_MS 500
#define SAVE_MIN_INTERVAL_MS 1500
#define FEED_DEBOUNCE_MS 300
#define FEEDBACK_VISIBLE_MS 1200
#define INTERACT_RETRY_MS 2000

#define COLOR_LOVE 0xFF6B9A
#define COLOR_ENERGY 0x4FD1C5
#define COLOR_FULL 0xFFB454
#define COLOR_OK 0x34D399
#define COLOR_MUTED 0x8A9BB4

#define HINT_IDLE "HOLD 3s  PET   /   3x TAP  PLAY"

typedef enum {
    APP_EVT_PRESS,
    APP_EVT_RELEASE,
    APP_EVT_FEED,
    APP_EVT_BUBBLE,
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    uint32_t id;
    uint64_t now_ms;
} app_evt_t;

static QueueHandle_t s_queue;
static pet_t s_pet;
static pet_ui_t *s_ui;
static mosaico_interact_handle_t s_interact;
static bool s_light_available;

static uint64_t s_last_feed_ms;
static uint64_t s_last_save_ms;
static uint64_t s_last_light_ms;
static uint64_t s_last_ui_ms;
static uint64_t s_last_presence_ms;
static uint64_t s_last_open_attempt_ms;
static uint64_t s_feedback_until_ms;
static uint64_t s_hint_until_ms;
static bool s_attrs_dirty;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static void post_event(app_evt_type_t type, uint32_t id)
{
    if (!s_queue) {
        return;
    }
    const app_evt_t event = {.type = type, .id = id, .now_ms = now_ms()};
    (void)xQueueSend(s_queue, &event, 0);
}

static void show_feedback(const char *text, uint32_t color)
{
    if (!s_ui) {
        return;
    }
    pet_ui_feedback(s_ui, text, color);
    s_feedback_until_ms = now_ms() + FEEDBACK_VISIBLE_MS;
}

static void show_default_hint(void)
{
    if (!s_ui) {
        return;
    }
    if (pet_state(&s_pet) == PET_STATE_IDLE) {
        pet_ui_set_hint(s_ui, HINT_IDLE);
    } else {
        pet_ui_set_hint(s_ui, NULL);
    }
}

static void on_pet_event(void *ctx, const pet_event_t *event)
{
    (void)ctx;
    if (!s_ui) {
        return;
    }
    switch (event->type) {
    case PET_EVT_STATE:
        pet_ui_set_state(s_ui, (pet_state_t)event->a);
        if (event->a == PET_STATE_GAME) {
            pet_ui_game_begin(s_ui, now_ms(), s_pet.cfg.game_duration_ms);
            pet_ui_set_hint(s_ui, NULL);
        } else if (event->a == PET_STATE_IDLE) {
            show_default_hint();
        } else {
            pet_ui_set_hint(s_ui, NULL);
        }
        break;
    case PET_EVT_ATTRIBUTES:
        s_attrs_dirty = true;
        break;
    case PET_EVT_LOVE:
        show_feedback("LOVE +5", COLOR_LOVE);
        break;
    case PET_EVT_FED:
        if (event->a > 0) {
            char text[24];
            snprintf(text, sizeof(text), "FULL +%d", event->a);
            show_feedback(text, COLOR_FULL);
        } else {
            show_feedback("ALREADY FULL", COLOR_FULL);
        }
        break;
    case PET_EVT_GAME_SCORE:
        pet_ui_game_score(s_ui, (uint32_t)event->a);
        break;
    case PET_EVT_GAME_END:
        pet_ui_game_end(s_ui, (uint32_t)event->a, event->b);
        show_feedback("ROUND OVER", COLOR_ENERGY);
        break;
    case PET_EVT_SETTLE_END:
        pet_ui_settle_end(s_ui);
        break;
    case PET_EVT_SLEEP_ENTER:
        show_feedback("GOOD NIGHT", COLOR_MUTED);
        break;
    case PET_EVT_WAKE:
        show_feedback(event->a ? "WOKE UP!" : "GOOD MORNING", COLOR_OK);
        break;
    case PET_EVT_TAP_PROGRESS:
        if (s_ui) {
            char text[32];
            snprintf(text, sizeof(text), "TAP %d/3", event->a);
            pet_ui_set_hint(s_ui, text);
            s_hint_until_ms = now_ms() + FEEDBACK_VISIBLE_MS;
        }
        break;
    case PET_EVT_LIGHT:
        break;
    default:
        break;
    }
}

static void on_screen_pressed(lv_event_t *event)
{
    (void)event;
    post_event(APP_EVT_PRESS, 0);
}

static void on_screen_released(lv_event_t *event)
{
    (void)event;
    post_event(APP_EVT_RELEASE, 0);
}

static void on_bubble_hit(void *ctx, uint32_t bubble_id)
{
    (void)ctx;
    post_event(APP_EVT_BUBBLE, bubble_id);
}

static void on_ai_button(void *button, void *user_data)
{
    (void)user_data;
    if (iot_button_get_event(button) == BUTTON_SINGLE_CLICK) {
        post_event(APP_EVT_FEED, 0);
    }
}

static void handle_event(const app_evt_t *event)
{
    const pet_state_t state = pet_state(&s_pet);
    switch (event->type) {
    case APP_EVT_PRESS:
        /* 游戏/结算期间忽略所有屏幕按压，避免污染长按/连击内部状态。 */
        if (state == PET_STATE_GAME || state == PET_STATE_SETTLE) {
            break;
        }
        pet_touch_press(&s_pet, event->now_ms);
        break;
    case APP_EVT_RELEASE:
        if (state == PET_STATE_GAME || state == PET_STATE_SETTLE) {
            break;
        }
        pet_touch_release(&s_pet, event->now_ms);
        break;
    case APP_EVT_FEED:
        if (event->now_ms - s_last_feed_ms >= FEED_DEBOUNCE_MS) {
            s_last_feed_ms = event->now_ms;
            (void)pet_feed(&s_pet);
        }
        break;
    case APP_EVT_BUBBLE:
        (void)pet_game_hit(&s_pet, event->now_ms);
        break;
    default:
        break;
    }
}

static void load_state(void)
{
    nvs_handle_t handle;
    if (nvs_open(PET_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGI(TAG, "No saved attributes, using defaults");
        return;
    }
    uint8_t buffer[PET_SNAPSHOT_SIZE];
    size_t length = sizeof(buffer);
    const esp_err_t err = nvs_get_blob(handle, PET_NVS_KEY, buffer, &length);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved attributes (%s)", esp_err_to_name(err));
        return;
    }
    if (!pet_snapshot_read(&s_pet, buffer, length)) {
        ESP_LOGW(TAG, "Saved attributes rejected (magic/version/crc)");
        return;
    }
    ESP_LOGI(TAG, "Restored attributes love=%u energy=%u full=%u", pet_affection(&s_pet),
             pet_energy(&s_pet), pet_satiety(&s_pet));
}

static void save_state(void)
{
    uint8_t buffer[PET_SNAPSHOT_SIZE];
    const size_t length = pet_snapshot_write(&s_pet, buffer, sizeof(buffer));
    if (length == 0) {
        return;
    }
    nvs_handle_t handle;
    if (nvs_open(PET_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    const esp_err_t err = nvs_set_blob(handle, PET_NVS_KEY, buffer, length);
    if (err == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        pet_clear_dirty(&s_pet);
    }
}

static void try_open_interact(void)
{
    mosaico_interact_config_t config = MOSAICO_INTERACT_DEFAULT_CONFIG();
    /* 需求指定右侧多功能交互模块（EEPROM 0x51）。 */
    config.slot = MOSAICO_MODULE_MGR_SLOT_RIGHT;
    config.discovery_timeout_ms = 300;
    config.button_mode = MOSAICO_INTERACT_BUTTON_MODE_AUTO;
    config.led_brightness = 0;

    mosaico_interact_handle_t handle = NULL;
    const esp_err_t err = mosaico_interact_open(&config, &handle);
    if (err != ESP_OK) {
        if (handle) {
            (void)mosaico_interact_close(handle);
        }
        s_interact = NULL;
        if (err != ESP_ERR_TIMEOUT && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Right interaction module unavailable (%s)", esp_err_to_name(err));
        }
        return;
    }

    s_interact = handle;
    s_light_available = true;
    mosaico_interact_info_t info;
    if (mosaico_interact_get_info(handle, &info) == ESP_OK) {
        ESP_LOGI(TAG, "Interaction module ready: %s slot, %s input, %u LEDs",
                 mosaico_module_mgr_slot_to_name(info.slot),
                 info.button_mode == MOSAICO_INTERACT_BUTTON_MODE_TOUCH ? "touch" : "gpio",
                 info.led_count);
    }
}

static void sample_interact(uint64_t now)
{
    mosaico_interact_inputs_t inputs;
    if (mosaico_interact_read_inputs(s_interact, &inputs) == ESP_OK) {
        pet_light_sample(&s_pet, inputs.light_level, now);
    }
}

static void create_ai_button(void)
{
    button_handle_t buttons[BSP_BUTTON_NUM] = {0};
    int count = 0;
    if (bsp_iot_button_create(buttons, &count, BSP_BUTTON_NUM) != ESP_OK) {
        ESP_LOGW(TAG, "AI button unavailable");
        return;
    }
    if (iot_button_register_cb(buttons[BSP_BUTTON_AI], BUTTON_SINGLE_CLICK, NULL, on_ai_button,
                               NULL) != ESP_OK) {
        ESP_LOGW(TAG, "AI button callback registration failed");
    }
}

static void pet_task(void *argument)
{
    (void)argument;
    bool game_was_active = false;

    for (;;) {
        const uint64_t now = now_ms();
        const bool locked = bsp_display_lock(100);
        if (!locked) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        app_evt_t event;
        while (xQueueReceive(s_queue, &event, 0) == pdTRUE) {
            handle_event(&event);
        }

        if (!s_interact && now - s_last_open_attempt_ms >= INTERACT_RETRY_MS) {
            s_last_open_attempt_ms = now;
            try_open_interact();
        }

        if (s_interact && now - s_last_presence_ms >= PRESENCE_CHECK_MS) {
            s_last_presence_ms = now;
            mosaico_module_mgr_info_t info;
            if (mosaico_module_mgr_get_info(MOSAICO_MODULE_MGR_SLOT_RIGHT, &info) == ESP_OK &&
                info.presence == MOSAICO_MODULE_PRESENCE_ABSENT) {
                ESP_LOGW(TAG, "Right interaction module removed");
                (void)mosaico_interact_close(s_interact);
                s_interact = NULL;
                s_light_available = false;
            }
        }

        if (s_interact && now - s_last_light_ms >= LIGHT_SAMPLE_MS) {
            s_last_light_ms = now;
            sample_interact(now);
        }

        pet_tick(&s_pet, now);

        const bool game_active = pet_state(&s_pet) == PET_STATE_GAME;
        if (game_active) {
            pet_ui_game_tick(s_ui, now);
        }
        if (game_was_active && !game_active) {
            s_attrs_dirty = true;
        }
        game_was_active = game_active;

        if (s_attrs_dirty || now - s_last_ui_ms >= UI_REFRESH_MS) {
            s_attrs_dirty = false;
            s_last_ui_ms = now;
            pet_ui_set_attributes(s_ui, pet_affection(&s_pet), pet_energy(&s_pet),
                                  pet_satiety(&s_pet));
            pet_ui_set_light(s_ui, s_light_available && pet_light_is_known(&s_pet),
                             pet_light_is_dark(&s_pet), pet_light_level(&s_pet));
        }

        if (s_feedback_until_ms != 0 && now >= s_feedback_until_ms) {
            s_feedback_until_ms = 0;
            pet_ui_hide_feedback(s_ui);
        }
        if (s_hint_until_ms != 0 && now >= s_hint_until_ms) {
            s_hint_until_ms = 0;
            show_default_hint();
        }

        bsp_display_unlock();

        if (pet_is_dirty(&s_pet) && now - s_last_save_ms >= SAVE_MIN_INTERVAL_MS) {
            s_last_save_ms = now;
            save_state();
        }

        vTaskDelay(pdMS_TO_TICKS(APP_LOOP_MS));
    }
}

void pet_app_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    bsp_board_variant_t variant = BSP_BOARD_VARIANT_V1_0;
    if (bsp_board_variant_get(&variant) == ESP_OK) {
        ESP_LOGI(TAG, "ESP-Mosaico CoreBoard %s detected",
                 variant == BSP_BOARD_VARIANT_V1_2 ? "V1.2" : "V1.0");
    }

    bsp_display_config_t display_config = BSP_DISPLAY_DEFAULT_CONFIG();
    display_config.enable_touch = true;
    if (!bsp_display_start_with_config(&display_config)) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }

    s_queue = xQueueCreate(APP_QUEUE_LEN, sizeof(app_evt_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Event queue allocation failed");
        return;
    }

    pet_init(&s_pet, NULL, on_pet_event, NULL, now_ms());
    load_state();

    if (bsp_display_lock(-1)) {
        s_ui = pet_ui_create(on_bubble_hit, NULL);
        pet_ui_set_attributes(s_ui, pet_affection(&s_pet), pet_energy(&s_pet),
                              pet_satiety(&s_pet));
        pet_ui_set_state(s_ui, PET_STATE_IDLE);
        pet_ui_set_light(s_ui, false, false, 0);
        pet_ui_set_hint(s_ui, HINT_IDLE);

        lv_obj_t *screen = lv_screen_active();
        lv_obj_add_event_cb(screen, on_screen_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(screen, on_screen_released, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(screen, on_screen_released, LV_EVENT_PRESS_LOST, NULL);
        bsp_display_unlock();
    }

    create_ai_button();

    if (xTaskCreate(pet_task, "pet_task", APP_TASK_STACK, NULL, APP_TASK_PRIORITY, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "Pet task creation failed");
    }
}
