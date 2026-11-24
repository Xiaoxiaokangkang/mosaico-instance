/**
 * pet_ui.c — 480×480 LVGL v9 星芽小屋界面
 *
 * 日常界面：宠物脸、昼夜、三条属性、成长进度
 * 游戏界面：三轨道接星星、6 颗星环进度、暂停/结果
 * 新手界面：预设名字选择、规则页、屏幕兜底睡眠
 */

#include "pet_ui.h"
#include "pet_core.h"
#include "peripherals.h"
#include "interaction_module.h"

#include <stdio.h>
#include <string.h>

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "lvgl.h"

extern const lv_image_dsc_t ghost_art;

static const char *TAG = "PET_UI";

LV_FONT_DECLARE(lv_font_starpet_18);
#define UI_FONT (&lv_font_starpet_18)

#define UI_W 480
#define UI_H 480
#define UI_DRAW_LINES 20
#define UI_DRAW_BUF_SIZE (UI_W * UI_DRAW_LINES * 2)

static lv_display_t *s_disp;
static lv_indev_t *s_indev;
static lv_obj_t *s_root;

static lv_obj_t *s_lbl_name;
static lv_obj_t *s_lbl_phase;
static lv_obj_t *s_lbl_growth;
static lv_obj_t *s_val_sat;
static lv_obj_t *s_val_energy;
static lv_obj_t *s_val_bond;
static lv_obj_t *s_lbl_form;
static lv_obj_t *s_celestial;

static lv_obj_t *s_pet_body;
static lv_obj_t *s_pet_shadow;
static lv_obj_t *s_pet_highlight;
static lv_obj_t *s_shy;
static lv_obj_t *s_shy_body;
static lv_obj_t *s_shy_eye_l;
static lv_obj_t *s_shy_eye_r;
static lv_obj_t *s_shy_cheek_l;
static lv_obj_t *s_shy_cheek_r;
static lv_obj_t *s_shy_dots;
static uint32_t s_shy_until;
static int s_pet_bob_dy;
static lv_obj_t *s_pet_antenna;
static lv_obj_t *s_pet_bulb;
static lv_obj_t *s_eye_l;
static lv_obj_t *s_eye_r;
static lv_obj_t *s_hl_l;
static lv_obj_t *s_hl_r;
static lv_obj_t *s_cheek_l;
static lv_obj_t *s_cheek_r;
static lv_obj_t *s_mouth;
static lv_obj_t *s_zzz;
static lv_obj_t *s_heart;

static lv_obj_t *s_card;
static lv_obj_t *s_bar_sat;
static lv_obj_t *s_bar_energy;
static lv_obj_t *s_bar_bond;
static lv_obj_t *s_lbl_sat;
static lv_obj_t *s_lbl_energy;
static lv_obj_t *s_lbl_bond;

static lv_obj_t *s_bubble;
static lv_obj_t *s_bubble_text;

static lv_obj_t *s_setup;
static lv_obj_t *s_help;
static lv_obj_t *s_sleep_btn;
static lv_obj_t *s_sleep_label;
static lv_obj_t *s_care_sleep_btn;
static lv_obj_t *s_care_sleep_label;
static lv_obj_t *s_care_panel;

static lv_obj_t *s_game;
static lv_obj_t *s_game_timer;
static lv_obj_t *s_game_progress;
static lv_obj_t *s_game_hint;
static lv_obj_t *s_game_lanes[3];
static lv_obj_t *s_game_star;
static lv_obj_t *s_game_pet;
static lv_obj_t *s_game_dots[6];
static lv_obj_t *s_pause_btn;

static lv_obj_t *s_pause;
static lv_obj_t *s_result;
static lv_obj_t *s_result_title;
static lv_obj_t *s_result_detail;
static lv_obj_t *s_evolve;
static lv_obj_t *s_evolve_title;
static lv_obj_t *s_evolve_note;

static lv_obj_t *s_name_buttons[6];
static const char *s_name_choices[6] = {"啵啵", "星仔", "团团", "闪闪", "软糖", "小满"};

static SemaphoreHandle_t s_mux;
static bool s_ready;
static uint32_t s_toast_until;
static uint32_t s_anim_phase;
static pet_core_t *s_core;
static esp_lcd_panel_handle_t s_panel;
static uint8_t *s_buf1;
static uint8_t *s_buf2;

#define UI_LOCK()   do { if (s_mux) xSemaphoreTakeRecursive(s_mux, portMAX_DELAY); } while (0)
#define UI_UNLOCK() do { if (s_mux) xSemaphoreGiveRecursive(s_mux); } while (0)

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* 明亮色板 */
static const uint32_t C_INK = 0x284D49;
static const uint32_t C_MUTED = 0x7D8F86;
static const uint32_t C_CARD = 0xFFFDF6;
static const uint32_t C_PINK = 0xF29BB4;
static const uint32_t C_GOLD = 0xF3C55C;
static const uint32_t C_MINT = 0x8CDCC1;
static const uint32_t C_BODY_A = 0x8CDCC1;
static const uint32_t C_PET_BABY = 0x8FE3D0;
static const uint32_t C_PET_ACTIVE = 0xFFB877;
static const uint32_t C_PET_ROUND = 0xF6A6C1;
static const uint32_t C_PET_CLINGY = 0xB9A7F5;
static const uint32_t C_EYE = 0x20343C;
static const uint32_t C_BLUSH = 0xFF7F9E;
static const uint32_t C_DAY_TOP = 0xEAF7FF;    /* 浅天空蓝 */
static const uint32_t C_DAY_BOTTOM = 0xFFF1E0; /* 暖奶油 */
static const uint32_t C_NIGHT = 0x1E2142;
static const uint32_t C_NIGHT2 = 0x4A3F6B;

static lv_color_t col(uint32_t rgb) { return lv_color_hex(rgb); }

static void style_plain(lv_obj_t *o)
{
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, UI_FONT, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *make_round(lv_obj_t *parent, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    style_plain(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, col(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void style_button(lv_obj_t *o, uint32_t bg, uint32_t fg, int radius)
{
    lv_obj_set_style_bg_color(o, col(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_shadow_width(o, 12, 0);
    lv_obj_set_style_shadow_opa(o, 26, 0);
    lv_obj_set_style_shadow_offset_y(o, 4, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    (void)fg;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *text, uint32_t bg, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_obj_create(parent);
    style_button(b, bg, C_INK, 18);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_t *label = lv_label_create(b);
    lv_obj_set_style_text_font(label, UI_FONT, 0);
    lv_obj_set_style_text_color(label, col(C_INK), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

/* ─── touch input ──────────────────────────────────────── */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static lv_point_t last = {0, 0};
    esp_lcd_touch_handle_t tp = peripherals_get_touch();
    if (!tp) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    esp_lcd_touch_read_data(tp);
    esp_lcd_touch_point_data_t pt[1] = {0};
    uint8_t cnt = 1;
    if (esp_lcd_touch_get_data(tp, pt, &cnt, 1) == ESP_OK && cnt > 0) {
        last.x = (lv_coord_t)pt[0].x;
        last.y = (lv_coord_t)pt[0].y;
        data->point = last;
        data->state = LV_INDEV_STATE_PRESSED;
        if (s_core) pet_core_note_screen_touch(s_core, now_ms());
    } else {
        data->point = last;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ─── UI callbacks ────────────────────────────────────── */

static void name_clicked(lv_event_t *e)
{
    if (!s_core) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) return;
    pet_core_set_name(s_core, name);
    pet_core_complete_naming(s_core, now_ms());
    lv_obj_add_flag(s_setup, LV_OBJ_FLAG_HIDDEN);
    pet_ui_show_toast("以后请多关照，我们一起长大吧！");
}

static void help_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    lv_obj_clear_flag(s_help, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_help);
}

static void help_close_clicked(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_help, LV_OBJ_FLAG_HIDDEN);
}

static void care_feed_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    pet_core_feed(s_core, now_ms());
    lv_obj_add_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);
}

static void care_hug_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    pet_core_hug_fallback(s_core, now_ms());
    lv_obj_add_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);
}

static void care_game_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    pet_core_start_game(s_core, now_ms());
    lv_obj_add_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);
}

static void care_open_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    lv_obj_clear_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_care_panel);
}

static void care_close_clicked(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);
}

static void sleep_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    if (s_core->state.sleeping) {
        pet_core_manual_wake(s_core, now_ms());
    } else if (!pet_core_manual_sleep(s_core, now_ms())) {
        pet_ui_show_toast("等它玩完再睡吧");
    }
}

static void care_sleep_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    if (s_core->state.sleeping) pet_core_manual_wake(s_core, now_ms());
    else pet_core_manual_sleep(s_core, now_ms());
    lv_obj_add_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);
}

static void pause_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) pet_core_pause_game(s_core, now_ms());
}

static void pause_feed_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) pet_core_feed(s_core, now_ms());
}

static void pause_resume_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) pet_core_resume_game(s_core, now_ms());
}

static void game_return_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) {
        pet_core_abort_game(s_core, now_ms());
        pet_ui_show_toast("这局先到这里，回小屋啦");
    }
}

static void pause_end_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) {
        pet_core_abort_game(s_core, now_ms());
        pet_ui_show_toast("这局先到这里，回小屋啦");
    }
}

static void result_retry_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) pet_core_retry_game(s_core, now_ms());
}

static void result_exit_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) pet_core_exit_result(s_core, now_ms());
}

static void evolve_ack_clicked(lv_event_t *e)
{
    (void)e;
    if (s_core) pet_core_ack_evolution(s_core, now_ms());
}

/* ─── flush / animations ──────────────────────────────── */

/* CO5300 QSPI partial updates require 4-pixel aligned X bounds.
 * Expand the invalidated area before LVGL renders, so the draw buffer matches. */
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    if (!area) return;
    const int max_x = UI_W - 1;
    const int max_y = UI_H - 1;
    if (area->x1 < 0) area->x1 = 0;
    if (area->x2 > max_x) area->x2 = max_x;
    if (area->y1 < 0) area->y1 = 0;
    if (area->y2 > max_y) area->y2 = max_y;
    area->x1 = (area->x1 / 4) * 4;
    int x2 = ((area->x2 + 4) / 4) * 4 - 1;
    area->x2 = (x2 > max_x) ? max_x : x2;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (s_panel) {
        esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    }
    /* Do NOT call lv_display_flush_ready here: the QSPI transfer is async.
     * ui_flush_done() is called from the LCD DMA completion callback. */
    (void)disp;
}

static void IRAM_ATTR ui_flush_done(void *ctx)
{
    lv_display_flush_ready((lv_display_t *)ctx);
}

static uint32_t tick_cb(void) { return now_ms(); }

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    static const int8_t bob_table[12] = {-3, -3, -2, -1, 0, 1, 2, 3, 3, 2, 1, 0};
    s_anim_phase++;
    s_pet_bob_dy = bob_table[(s_anim_phase / 4) % 12]; /* 约 5.8 秒一个来回 */
    if (!s_core) return;
    /* 呼吸与睡眠时的小幅浮动；只在 LVGL 任务中修改对象。 */
    pet_state_t *s = &s_core->state;
    if (s->mode == PET_MODE_HUG) {
        int dy = (s_anim_phase % 10) < 5 ? -2 : 2;
        lv_obj_set_y(s_heart, 314 + dy);
        lv_obj_clear_flag(s_heart, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_heart, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_toast_until && now_ms() > s_toast_until) {
        s_toast_until = 0;
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        UI_LOCK();
        uint32_t wait = lv_timer_handler();
        UI_UNLOCK();
        if (wait < 10) wait = 10; /* 100 Hz tick: 5ms rounds to 0 and busy-spins */
        if (wait > 25) wait = 25;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

/* ─── build UI ────────────────────────────────────────── */

static void pet_shy_cb(lv_event_t *e)
{
    (void)e;
    if (!s_core) return;
    if (s_core->state.sleeping || s_core->state.mode != PET_MODE_IDLE) return;
    const uint32_t now = now_ms();
    s_shy_until = now + 1200;
    pet_core_note_screen_touch(s_core, now);
}

static void build_main_screen(void)
{
    s_celestial = make_round(s_root, 58, 58, C_GOLD);
    lv_obj_set_pos(s_celestial, 388, 122);
    lv_obj_set_style_shadow_width(s_celestial, 23, 0);
    lv_obj_set_style_shadow_opa(s_celestial, 80, 0);
    lv_obj_set_style_shadow_color(s_celestial, col(C_GOLD), 0);

    s_lbl_name = make_label(s_root, "小星芽", 24, 16, C_INK);
    lv_obj_set_style_text_font(s_lbl_name, UI_FONT, 0);

    s_lbl_phase = make_label(s_root, "白天", 24, 44, C_MUTED);
    lv_obj_set_style_text_font(s_lbl_phase, UI_FONT, 0);

    s_lbl_growth = make_label(s_root, "成长 0/10", 24, 68, C_MUTED);
    lv_obj_set_style_text_font(s_lbl_growth, UI_FONT, 0);

    s_lbl_form = make_label(s_root, "星芽宝宝", 292, 44, C_MUTED);
    lv_obj_set_style_text_font(s_lbl_form, UI_FONT, 0);


    s_sleep_btn = make_button(s_root, 320, 66, 58, 28, "睡眠", 0xFFE6B8, sleep_clicked, NULL);
    lv_obj_set_style_radius(s_sleep_btn, 14, 0);
    s_sleep_label = lv_obj_get_child(s_sleep_btn, 0);
    lv_obj_t *help = make_button(s_root, 390, 66, 58, 28, "规则", 0xE3F1E9, help_clicked, NULL);
    lv_obj_set_style_radius(help, 14, 0);
    lv_obj_t *care = make_button(s_root, 240, 66, 74, 28, "屏幕照顾", 0xE9E5FF, care_open_clicked, NULL);
    lv_obj_set_style_radius(care, 14, 0);

    /* 宠物：半透明果冻幽灵（单图统一透明度，避免多层叠加发脏） */
    s_pet_shadow = make_round(s_root, 150, 22, 0x2B4A44);
    lv_obj_set_pos(s_pet_shadow, 165, 334);
    lv_obj_set_style_bg_opa(s_pet_shadow, 45, 0);

    s_pet_body = lv_image_create(s_root);
    lv_image_set_src(s_pet_body, &ghost_art);
    lv_obj_set_size(s_pet_body, 220, 220);
    lv_obj_set_pos(s_pet_body, 130, 130);
    lv_obj_set_style_opa(s_pet_body, 200, 0);
    lv_obj_set_style_image_recolor_opa(s_pet_body, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_pet_body, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_pet_body, pet_shy_cb, LV_EVENT_CLICKED, NULL);

    s_pet_highlight = make_round(s_root, 46, 24, 0xFFFFFF);
    lv_obj_set_pos(s_pet_highlight, 160, 166);
    lv_obj_set_style_bg_opa(s_pet_highlight, 80, 0);

    s_pet_antenna = lv_obj_create(s_root);
    style_plain(s_pet_antenna);
    lv_obj_set_size(s_pet_antenna, 5, 38);
    lv_obj_set_style_radius(s_pet_antenna, 4, 0);
    lv_obj_set_style_bg_color(s_pet_antenna, col(0x2E7D72), 0);
    lv_obj_set_style_bg_opa(s_pet_antenna, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_pet_antenna, 205, 96);

    s_pet_bulb = make_round(s_root, 24, 24, C_GOLD);
    lv_obj_set_pos(s_pet_bulb, 195, 78);
    lv_obj_set_style_shadow_width(s_pet_bulb, 18, 0);
    lv_obj_set_style_shadow_opa(s_pet_bulb, 70, 0);
    lv_obj_set_style_shadow_color(s_pet_bulb, col(C_GOLD), 0);

    s_eye_l = make_round(s_root, 24, 30, C_EYE);
    s_eye_r = make_round(s_root, 24, 30, C_EYE);
    s_hl_l = make_round(s_root, 7, 7, 0xFFFFFF);
    s_hl_r = make_round(s_root, 7, 7, 0xFFFFFF);
    lv_obj_set_style_bg_opa(s_hl_l, 220, 0);
    lv_obj_set_style_bg_opa(s_hl_r, 220, 0);

    s_cheek_l = make_round(s_root, 24, 14, C_BLUSH);
    s_cheek_r = make_round(s_root, 24, 14, C_BLUSH);
    lv_obj_set_style_bg_opa(s_cheek_l, 165, 0);
    lv_obj_set_style_bg_opa(s_cheek_r, 165, 0);

    s_mouth = make_round(s_root, 22, 9, C_EYE);

    s_zzz = make_label(s_root, "z  z", 355, 156, 0x4A6B65);
    lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);

    s_heart = make_label(s_root, "心", 334, 312, C_PINK);
    lv_obj_set_style_text_font(s_heart, UI_FONT, 0);
    lv_obj_add_flag(s_heart, LV_OBJ_FLAG_HIDDEN);

    /* 害羞缩团：点宠物后显示 1.2 秒 */
    s_shy = lv_obj_create(s_root);
    style_plain(s_shy);
    lv_obj_set_size(s_shy, 240, 240);
    lv_obj_set_pos(s_shy, 120, 120);
    lv_obj_add_flag(s_shy, LV_OBJ_FLAG_HIDDEN);

    s_shy_body = make_round(s_shy, 130, 130, C_PET_BABY);
    lv_obj_set_pos(s_shy_body, 55, 65);
    lv_obj_set_style_bg_opa(s_shy_body, 225, 0);
    lv_obj_set_style_shadow_width(s_shy_body, 20, 0);
    lv_obj_set_style_shadow_opa(s_shy_body, 50, 0);
    s_shy_eye_l = make_round(s_shy, 20, 4, C_EYE);
    lv_obj_set_pos(s_shy_eye_l, 78, 120);
    s_shy_eye_r = make_round(s_shy, 20, 4, C_EYE);
    lv_obj_set_pos(s_shy_eye_r, 142, 120);
    s_shy_cheek_l = make_round(s_shy, 18, 10, C_BLUSH);
    lv_obj_set_pos(s_shy_cheek_l, 66, 140);
    lv_obj_set_style_bg_opa(s_shy_cheek_l, 170, 0);
    s_shy_cheek_r = make_round(s_shy, 18, 10, C_BLUSH);
    lv_obj_set_pos(s_shy_cheek_r, 156, 140);
    lv_obj_set_style_bg_opa(s_shy_cheek_r, 170, 0);
    s_shy_dots = make_label(s_shy, "…", 108, 28, C_EYE);
    lv_obj_set_style_text_font(s_shy_dots, UI_FONT, 0);

    /* 状态卡 */
    s_card = lv_obj_create(s_root);
    style_plain(s_card);
    lv_obj_set_style_radius(s_card, 26, 0);
    lv_obj_set_style_bg_color(s_card, col(C_CARD), 0);
    lv_obj_set_style_bg_opa(s_card, 190, 0);
    lv_obj_set_style_shadow_width(s_card, 18, 0);
    lv_obj_set_style_shadow_opa(s_card, 20, 0);
    lv_obj_set_pos(s_card, 20, 370);
    lv_obj_set_size(s_card, 440, 92);

    s_bar_sat = lv_bar_create(s_card);
    s_bar_energy = lv_bar_create(s_card);
    s_bar_bond = lv_bar_create(s_card);
    lv_obj_t *bars[3] = {s_bar_sat, s_bar_energy, s_bar_bond};
    for (int i = 0; i < 3; i++) {
        style_plain(bars[i]);
        lv_obj_set_style_radius(bars[i], 7, LV_PART_MAIN | LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bars[i], 50, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bars[i], col(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_bar_set_range(bars[i], 0, 100);
        lv_bar_set_value(bars[i], 60, LV_ANIM_OFF);
        lv_obj_set_size(bars[i], 310, 12);
        lv_obj_set_pos(bars[i], 92, 16 + i * 26);
    }
    lv_obj_set_style_bg_color(s_bar_sat, col(C_GOLD), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar_energy, col(C_MINT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar_bond, col(C_PINK), LV_PART_INDICATOR);

    s_lbl_sat = make_label(s_card, "饱食", 18, 10, C_INK);
    s_lbl_energy = make_label(s_card, "精力", 18, 36, C_INK);
    s_lbl_bond = make_label(s_card, "亲密", 18, 62, C_INK);
    lv_obj_set_style_text_font(s_lbl_sat, UI_FONT, 0);
    lv_obj_set_style_text_font(s_lbl_energy, UI_FONT, 0);
    lv_obj_set_style_text_font(s_lbl_bond, UI_FONT, 0);

    s_val_sat = make_label(s_card, "60", 408, 10, C_MUTED);
    s_val_energy = make_label(s_card, "60", 408, 36, C_MUTED);
    s_val_bond = make_label(s_card, "60", 408, 62, C_MUTED);
    lv_obj_set_style_text_font(s_val_sat, UI_FONT, 0);
    lv_obj_set_style_text_font(s_val_energy, UI_FONT, 0);
    lv_obj_set_style_text_font(s_val_bond, UI_FONT, 0);

    /* 气泡 */
    s_bubble = lv_obj_create(s_root);
    style_plain(s_bubble);
    lv_obj_set_style_radius(s_bubble, 22, 0);
    lv_obj_set_style_bg_color(s_bubble, col(0xFFFDF4), 0);
    lv_obj_set_style_bg_opa(s_bubble, 240, 0);
    lv_obj_set_style_shadow_width(s_bubble, 16, 0);
    lv_obj_set_style_shadow_opa(s_bubble, 30, 0);
    lv_obj_set_pos(s_bubble, 54, 108);
    lv_obj_set_size(s_bubble, 372, 50);
    lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    s_bubble_text = lv_label_create(s_bubble);
    lv_obj_set_style_text_font(s_bubble_text, UI_FONT, 0);
    lv_obj_set_style_text_color(s_bubble_text, col(C_INK), 0);
    lv_obj_set_style_text_align(s_bubble_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_bubble_text, 348);
    lv_label_set_long_mode(s_bubble_text, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_bubble_text);
}

static void build_game_layer(void)
{
    s_game = lv_obj_create(s_root);
    style_plain(s_game);
    lv_obj_set_size(s_game, 480, 480);
    lv_obj_set_pos(s_game, 0, 0);
    lv_obj_set_style_bg_color(s_game, col(0xF6F4E8), 0);
    lv_obj_set_style_bg_opa(s_game, 245, 0);
    lv_obj_add_flag(s_game, LV_OBJ_FLAG_HIDDEN);

    s_game_timer = make_label(s_game, "10.0s", 22, 20, C_INK);
    lv_obj_set_style_text_font(s_game_timer, UI_FONT, 0);

    s_game_progress = make_label(s_game, "接星 0/6", 200, 20, C_INK);
    lv_obj_set_style_text_font(s_game_progress, UI_FONT, 0);

    lv_obj_t *ret = make_button(s_game, 330, 12, 56, 34, "返回", 0xF2E7D6, game_return_clicked, NULL);
    lv_obj_set_style_radius(ret, 14, 0);
    s_pause_btn = make_button(s_game, 402, 12, 56, 34, "暂停", 0xFFE6B8, pause_clicked, NULL);
    lv_obj_set_style_radius(s_pause_btn, 14, 0);

    for (int i = 0; i < 3; i++) {
        s_game_lanes[i] = lv_obj_create(s_game);
        style_plain(s_game_lanes[i]);
        lv_obj_set_size(s_game_lanes[i], 2, 270);
        lv_obj_set_pos(s_game_lanes[i], 120 + i * 120, 72);
        lv_obj_set_style_bg_color(s_game_lanes[i], col(0xD8D8C8), 0);
        lv_obj_set_style_bg_opa(s_game_lanes[i], 160, 0);
    }

    s_game_star = make_round(s_game, 22, 22, C_GOLD);
    lv_obj_set_pos(s_game_star, 229, 110);
    lv_obj_set_style_shadow_width(s_game_star, 18, 0);
    lv_obj_set_style_shadow_opa(s_game_star, 60, 0);
    lv_obj_set_style_shadow_color(s_game_star, col(C_GOLD), 0);

    s_game_pet = make_round(s_game, 48, 48, C_BODY_A);
    lv_obj_set_pos(s_game_pet, 216, 338);

    for (int i = 0; i < 6; i++) {
        s_game_dots[i] = make_round(s_game, 17, 17, 0xDDD8C8);
        lv_obj_set_pos(s_game_dots[i], 42 + i * 30, 408);
    }

    s_game_hint = make_label(s_game, "左垫：向左移动      右垫：向右移动", 68, 442, C_MUTED);
    lv_obj_set_style_text_font(s_game_hint, UI_FONT, 0);
}

static void build_setup_layer(void)
{
    s_setup = lv_obj_create(s_root);
    style_plain(s_setup);
    lv_obj_set_size(s_setup, 480, 480);
    lv_obj_set_pos(s_setup, 0, 0);
    lv_obj_set_style_bg_color(s_setup, col(0xFFF9EC), 0);
    lv_obj_set_style_bg_opa(s_setup, 250, 0);

    lv_obj_t *title = make_label(s_setup, "给它取个名字", 72, 58, C_INK);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_t *sub = make_label(s_setup, "选一个喜欢的名字，之后就可以照顾它了", 56, 94, C_MUTED);
    lv_obj_set_style_text_font(sub, UI_FONT, 0);
    lv_obj_t *mascot = make_round(s_setup, 52, 52, C_MINT);
    lv_obj_set_pos(mascot, 214, 132);
    lv_obj_t *blush = make_round(s_setup, 12, 7, C_PINK);
    lv_obj_set_pos(blush, 246, 156);

    for (int i = 0; i < 6; i++) {
        int col_i = i % 3;
        int row_i = i / 3;
        s_name_buttons[i] = make_button(s_setup, 54 + col_i * 130, 230 + row_i * 64,
                                        112, 46, s_name_choices[i], 0xEAF5E8,
                                        name_clicked, (void *)s_name_choices[i]);
    }

    lv_obj_t *tip = make_label(s_setup, "名字会保存在设备里", 130, 390, C_MUTED);
    lv_obj_set_style_text_font(tip, UI_FONT, 0);
    lv_obj_add_flag(s_setup, LV_OBJ_FLAG_HIDDEN);
}

static void build_pause_layer(void)
{
    s_pause = lv_obj_create(s_root);
    style_plain(s_pause);
    lv_obj_set_size(s_pause, 480, 480);
    lv_obj_set_pos(s_pause, 0, 0);
    lv_obj_set_style_bg_color(s_pause, col(0x1B1D2D), 0);
    lv_obj_set_style_bg_opa(s_pause, 150, 0);
    lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(s_pause);
    style_plain(panel);
    lv_obj_set_style_radius(panel, 26, 0);
    lv_obj_set_style_bg_color(panel, col(0xFFFDF5), 0);
    lv_obj_set_style_bg_opa(panel, 250, 0);
    lv_obj_set_pos(panel, 54, 112);
    lv_obj_set_size(panel, 372, 244);
    lv_obj_t *title = make_label(panel, "休息一下", 118, 22, C_INK);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    make_button(panel, 28, 70, 148, 48, "喂一口", 0xFFE3BE, pause_feed_clicked, NULL);
    make_button(panel, 196, 70, 148, 48, "继续", 0xDFF4E6, pause_resume_clicked, NULL);
    make_button(panel, 28, 142, 316, 44, "返回主界面", 0xF2E7D6, pause_end_clicked, NULL);
    lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
}

static void build_result_layer(void)
{
    s_result = lv_obj_create(s_root);
    style_plain(s_result);
    lv_obj_set_size(s_result, 480, 480);
    lv_obj_set_pos(s_result, 0, 0);
    lv_obj_set_style_bg_color(s_result, col(0x1B1D2D), 0);
    lv_obj_set_style_bg_opa(s_result, 130, 0);
    lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(s_result);
    style_plain(panel);
    lv_obj_set_style_radius(panel, 28, 0);
    lv_obj_set_style_bg_color(panel, col(0xFFFDF5), 0);
    lv_obj_set_style_bg_opa(panel, 250, 0);
    lv_obj_set_pos(panel, 48, 124);
    lv_obj_set_size(panel, 384, 240);
    s_result_title = make_label(panel, "接住 6 颗星啦！", 88, 24, C_INK);
    lv_obj_set_style_text_font(s_result_title, UI_FONT, 0);
    s_result_detail = make_label(panel, "奖励 +0  消耗 -10\n精力净 -10", 22, 66, C_MUTED);
    lv_obj_set_style_text_font(s_result_detail, UI_FONT, 0);
    lv_obj_set_width(s_result_detail, 340);
    lv_obj_set_style_text_align(s_result_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_result_detail, LV_LABEL_LONG_WRAP);
    make_button(panel, 30, 152, 148, 48, "再来一局", 0xDFF4E6, result_retry_clicked, NULL);
    make_button(panel, 206, 152, 148, 48, "先休息", 0xF2E7D6, result_exit_clicked, NULL);
}

static void build_evolve_layer(void)
{
    s_evolve = lv_obj_create(s_root);
    style_plain(s_evolve);
    lv_obj_set_size(s_evolve, 480, 480);
    lv_obj_set_pos(s_evolve, 0, 0);
    lv_obj_set_style_bg_color(s_evolve, col(0x1B1D2D), 0);
    lv_obj_set_style_bg_opa(s_evolve, 150, 0);
    lv_obj_add_flag(s_evolve, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(s_evolve);
    style_plain(panel);
    lv_obj_set_style_radius(panel, 28, 0);
    lv_obj_set_style_bg_color(panel, col(0xFFFDF5), 0);
    lv_obj_set_style_bg_opa(panel, 250, 0);
    lv_obj_set_pos(panel, 46, 104);
    lv_obj_set_size(panel, 388, 272);
    lv_obj_t *eyebrow = make_label(panel, "成长时刻", 138, 22, 0xB08A4A);
    lv_obj_set_style_text_font(eyebrow, UI_FONT, 0);
    s_evolve_title = make_label(panel, "新形态解锁！", 98, 52, C_INK);
    lv_obj_set_style_text_font(s_evolve_title, UI_FONT, 0);
    s_evolve_note = make_label(panel, "图鉴 +1", 168, 92, 0xB08A4A);
    lv_obj_set_style_text_font(s_evolve_note, UI_FONT, 0);
    make_button(panel, 54, 166, 280, 48, "太棒啦，继续照顾", 0xF6CF86, evolve_ack_clicked, NULL);
}

static void build_help_layer(void)
{
    s_help = lv_obj_create(s_root);
    style_plain(s_help);
    lv_obj_set_size(s_help, 480, 480);
    lv_obj_set_pos(s_help, 0, 0);
    lv_obj_set_style_bg_color(s_help, col(0x1B1D2D), 0);
    lv_obj_set_style_bg_opa(s_help, 145, 0);
    lv_obj_add_flag(s_help, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(s_help);
    style_plain(panel);
    lv_obj_set_style_radius(panel, 26, 0);
    lv_obj_set_style_bg_color(panel, col(0xFFFDF5), 0);
    lv_obj_set_style_bg_opa(panel, 250, 0);
    lv_obj_set_pos(panel, 36, 46);
    lv_obj_set_size(panel, 408, 388);
    lv_obj_t *title = make_label(panel, "怎么照顾它", 112, 22, C_INK);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_t *text = make_label(panel,
        "左垫轻点：喂食\n"
        "双垫 2 秒：抱抱\n"
        "右垫开始：接星星\n"
        "游戏内左/右垫移动\n\n"
        "光敏亮醒 / 暗睡（自动）\n"
        "睡眠/叫醒按钮可手动覆盖\n\n"
        "失败不扣属性，慢慢来。",
        34, 70, C_MUTED);
    lv_obj_set_style_text_font(text, UI_FONT, 0);
    lv_obj_set_style_text_line_space(text, 1, 0);
    lv_obj_set_width(text, 340);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    make_button(panel, 96, 320, 216, 44, "知道啦", 0xF6CF86, help_close_clicked, NULL);
}

static void build_care_panel(void)
{
    s_care_panel = lv_obj_create(s_root);
    style_plain(s_care_panel);
    lv_obj_set_size(s_care_panel, 480, 480);
    lv_obj_set_pos(s_care_panel, 0, 0);
    lv_obj_set_style_bg_color(s_care_panel, col(0x1B1D2D), 0);
    lv_obj_set_style_bg_opa(s_care_panel, 140, 0);
    lv_obj_add_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(s_care_panel);
    style_plain(panel);
    lv_obj_set_style_radius(panel, 26, 0);
    lv_obj_set_style_bg_color(panel, col(0xFFFDF5), 0);
    lv_obj_set_style_bg_opa(panel, 250, 0);
    lv_obj_set_pos(panel, 46, 102);
    lv_obj_set_size(panel, 388, 276);
    lv_obj_t *title = make_label(panel, "用手点也可以照顾它", 96, 22, C_INK);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    make_button(panel, 28, 70, 150, 54, "喂一颗", 0xFFE3BE, care_feed_clicked, NULL);
    make_button(panel, 210, 70, 150, 54, "抱抱", 0xFFE0EA, care_hug_clicked, NULL);
    make_button(panel, 28, 140, 150, 54, "接星星", 0xDFF4E6, care_game_clicked, NULL);
    s_care_sleep_btn = make_button(panel, 210, 140, 150, 54, "睡眠", 0xE9E5FF, care_sleep_clicked, NULL);
    s_care_sleep_label = lv_obj_get_child(s_care_sleep_btn, 0);
    make_button(panel, 90, 214, 208, 40, "收起", 0xF2E7D6, care_close_clicked, NULL);
}

static void build_all(void)
{
    s_root = lv_obj_create(NULL);
    style_plain(s_root);
    lv_obj_set_size(s_root, 480, 480);
    lv_obj_set_style_bg_color(s_root, col(C_DAY_TOP), 0);
    lv_obj_set_style_bg_grad_color(s_root, col(C_DAY_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(s_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_screen_load(s_root);

    build_main_screen();
    build_game_layer();
    build_setup_layer();
    build_pause_layer();
    build_result_layer();
    build_evolve_layer();
    build_help_layer();
    build_care_panel();
}

/* ─── state → UI ──────────────────────────────────────── */

static uint32_t pet_form_color(pet_form_t form)
{
    switch (form) {
        case PET_FORM_ACTIVE: return C_PET_ACTIVE;
        case PET_FORM_ROUND:  return C_PET_ROUND;
        case PET_FORM_CLINGY: return C_PET_CLINGY;
        default:              return C_PET_BABY;
    }
}

static void set_pet_form(pet_form_t form)
{
    const uint32_t color = pet_form_color(form);
    const int dy = s_pet_bob_dy;

    /* 单张幽灵图统一染色，半透明只作用于整张图，不会在部件交叠处发脏。 */
    lv_obj_set_style_image_recolor(s_pet_body, col(color), 0);
    lv_obj_set_style_image_recolor_opa(s_pet_body, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_pet_body, 130, 130 + dy);
    lv_obj_set_pos(s_pet_highlight, 160, 166 + dy);

    lv_obj_set_pos(s_pet_antenna, 205, 96 + dy);
    lv_obj_set_pos(s_pet_bulb, 195, 78 + dy);

    lv_obj_set_pos(s_eye_l, 202, 220 + dy);
    lv_obj_set_pos(s_eye_r, 274, 220 + dy);
    lv_obj_set_pos(s_hl_l, 209, 227 + dy);
    lv_obj_set_pos(s_hl_r, 281, 227 + dy);
    lv_obj_set_pos(s_cheek_l, 178, 262 + dy);
    lv_obj_set_pos(s_cheek_r, 292, 262 + dy);
    lv_obj_set_pos(s_mouth, 228, 280 + dy);

    lv_obj_set_style_bg_color(s_shy_body, col(color), 0);
    lv_obj_set_style_shadow_color(s_shy_body, col(color), 0);
}

static void set_text_color(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_text_color(obj, col(color), 0);
}

static void update_game(pet_state_t *s)
{
    bool visible = (s->mode == PET_MODE_GAME || s->mode == PET_MODE_GAME_PAUSED);
    if (visible) {
        lv_obj_clear_flag(s_game, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_game);
        uint32_t left = (s->game_elapsed_ms >= PET_GAME_DURATION_MS)
                        ? 0 : (PET_GAME_DURATION_MS - s->game_elapsed_ms);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fs", left / 1000.0f);
        lv_label_set_text(s_game_timer, buf);
        snprintf(buf, sizeof(buf), "接星 %u/%u", (unsigned)s->game_stars, (unsigned)PET_GAME_TARGET_STARS);
        lv_label_set_text(s_game_progress, buf);
        int px = 120 + s->game_lane * 120 - 24;
        lv_obj_set_pos(s_game_pet, px, 338);
        lv_obj_set_style_bg_color(s_game_pet, col(pet_form_color(s->form)), 0);
        if (s->game_star_visible && s->game_star_lane >= 0) {
            lv_obj_clear_flag(s_game_star, LV_OBJ_FLAG_HIDDEN);
            uint32_t t = pet_core_game_star_time_left(s_core);
            int y = 100 + (int)((PET_GAME_STAR_FALL_MS - (t > PET_GAME_STAR_FALL_MS ? PET_GAME_STAR_FALL_MS : t))
                                * 170 / PET_GAME_STAR_FALL_MS);
            lv_obj_set_pos(s_game_star, 120 + s->game_star_lane * 120 - 11, y);
        } else {
            lv_obj_add_flag(s_game_star, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < 6; i++) {
            lv_obj_set_style_bg_color(s_game_dots[i],
                col(i < s->game_stars ? C_GOLD : 0xDDD8C8), 0);
        }
        if (s->mode == PET_MODE_GAME_PAUSED) lv_obj_clear_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_game, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_setup(pet_state_t *s)
{
    if (s->needs_name) {
        lv_obj_clear_flag(s_setup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_setup);
    } else {
        lv_obj_add_flag(s_setup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_result(pet_state_t *s)
{
    if (s->mode == PET_MODE_RESULT) {
        lv_obj_clear_flag(s_result, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_result);
        char buf[96];
        if (s->result == PET_RESULT_SUCCESS) {
            lv_label_set_text(s_result_title, "接住 6 颗星啦！");
        } else {
            snprintf(buf, sizeof(buf), "这局接住 %d 颗星", s->game_stars);
            lv_label_set_text(s_result_title, buf);
        }
        snprintf(buf, sizeof(buf), "奖励 +%d  消耗 -10\n精力净 %+d",
                 s->game_reward, s->game_net);
        lv_label_set_text(s_result_detail, buf);
    } else {
        lv_obj_add_flag(s_result, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_evolve(pet_state_t *s)
{
    if (s->mode == PET_MODE_EVOLVE) {
        lv_obj_clear_flag(s_evolve, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_evolve);
        lv_label_set_text(s_evolve_title, pet_core_form_name(s->form));
        lv_label_set_text(s_evolve_note, s->unlocked[s->form] ? "新形态已记入图鉴" : "图鉴已更新");
    } else {
        lv_obj_add_flag(s_evolve, LV_OBJ_FLAG_HIDDEN);
    }
}

void pet_ui_refresh(void)
{
    if (!s_ready || !s_core) return;
    UI_LOCK();
    pet_state_t *s = &s_core->state;
    bool dark = !s_core->light_bright;

    lv_obj_set_style_bg_color(s_root, col(dark ? C_NIGHT : C_DAY_TOP), 0);
    lv_obj_set_style_bg_grad_color(s_root, col(dark ? C_NIGHT2 : C_DAY_BOTTOM), 0);
    set_text_color(s_lbl_name, dark ? 0xF5F1E3 : C_INK);
    set_text_color(s_lbl_phase, dark ? 0xD2CEE1 : C_MUTED);
    set_text_color(s_lbl_growth, dark ? 0xD2CEE1 : C_MUTED);
    set_text_color(s_lbl_form, dark ? 0xD2CEE1 : C_MUTED);
    set_text_color(s_lbl_sat, C_INK);
    set_text_color(s_lbl_energy, C_INK);
    set_text_color(s_lbl_bond, C_INK);

    lv_label_set_text(s_lbl_name, s->name[0] ? s->name : "小星芽");
    char buf[48];
    snprintf(buf, sizeof(buf), "%s · %s",
             s_core->light_bright ? "环境亮" : "环境暗",
             s->sleeping ? "熟睡" : "醒着");
    lv_label_set_text(s_sleep_label, s->sleeping ? "叫醒" : "睡眠");
    lv_label_set_text(s_care_sleep_label, s->sleeping ? "叫醒" : "睡眠");
    lv_label_set_text(s_lbl_phase, buf);
    snprintf(buf, sizeof(buf), "成长 %d/%d", pet_core_round_total(s_core), PET_CARES_PER_FORM);
    lv_label_set_text(s_lbl_growth, buf);
    lv_label_set_text(s_lbl_form, pet_core_form_name(s->form));

    set_pet_form(s->form);
    lv_bar_set_value(s_bar_sat, (int)s->satiety, LV_ANIM_OFF);
    lv_bar_set_value(s_bar_energy, (int)s->energy, LV_ANIM_OFF);
    lv_bar_set_value(s_bar_bond, (int)s->intimacy, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d", (int)s->satiety);
    lv_label_set_text(s_val_sat, buf);
    snprintf(buf, sizeof(buf), "%d", (int)s->energy);
    lv_label_set_text(s_val_energy, buf);
    snprintf(buf, sizeof(buf), "%d", (int)s->intimacy);
    lv_label_set_text(s_val_bond, buf);

    const bool shy = s_shy_until != 0 &&
                     (int32_t)(s_shy_until - now_ms()) > 0 &&
                     s->mode == PET_MODE_IDLE && !s->sleeping;
    if (!shy) s_shy_until = 0;
    const bool closed = s->sleeping || s->mode == PET_MODE_HUG;

    if (shy) {
        lv_obj_add_flag(s_pet_shadow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pet_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pet_highlight, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pet_antenna, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pet_bulb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hl_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hl_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cheek_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cheek_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_shy, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_pet_shadow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pet_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pet_highlight, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pet_antenna, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pet_bulb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_cheek_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_cheek_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_shy, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_size(s_eye_l, 24, closed ? 7 : 30);
        lv_obj_set_size(s_eye_r, 24, closed ? 7 : 30);
        if (closed) {
            lv_obj_add_flag(s_hl_l, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_hl_r, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_hl_l, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_hl_r, LV_OBJ_FLAG_HIDDEN);
        }
        if (s->sleeping) lv_obj_clear_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    }
    set_text_color(s_zzz, dark ? 0xFFFFFF : 0x4A6B65);

    lv_obj_set_style_bg_color(s_celestial, col(dark ? 0xE8E8D8 : C_GOLD), 0);
    lv_obj_set_style_shadow_color(s_celestial, col(dark ? 0xE8E8D8 : C_GOLD), 0);
    lv_obj_set_style_bg_opa(s_celestial, dark ? 150 : 255, 0);

    lv_obj_set_style_bg_opa(s_card, s->sleeping ? 165 : 215, 0);
    lv_obj_set_style_bg_opa(s_bubble, dark ? 200 : 240, 0);
    set_text_color(s_bubble_text, dark ? 0xF7F1DE : C_INK);

    update_setup(s);
    update_game(s);
    update_result(s);
    update_evolve(s);

    /* 游戏、睡眠和结算时收起临时面板，避免遮住核心界面。 */
    if (s->sleeping || s->mode == PET_MODE_GAME || s->mode == PET_MODE_GAME_PAUSED ||
        s->mode == PET_MODE_RESULT || s->mode == PET_MODE_EVOLVE) {
        lv_obj_add_flag(s_help, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_care_panel, LV_OBJ_FLAG_HIDDEN);
    }

    /* 气泡只在 toast 时显示。 */
    if (s_toast_until && now_ms() > s_toast_until) {
        s_toast_until = 0;
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    }

    UI_UNLOCK();
}

void pet_ui_show_toast(const char *text)
{
    if (!s_ready || !text) return;
    UI_LOCK();
    lv_label_set_text(s_bubble_text, text);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_bubble);
    s_toast_until = now_ms() + 3000;
    UI_UNLOCK();
}

bool pet_ui_setup_active(void)
{
    if (!s_ready) return false;
    return s_core && s_core->state.needs_name;
}

esp_err_t pet_ui_init(esp_lcd_panel_handle_t panel, pet_core_t *core)
{
    if (panel == NULL || core == NULL) return ESP_ERR_INVALID_ARG;
    s_panel = panel;
    s_core = core;
    s_mux = xSemaphoreCreateRecursiveMutex();
    if (!s_mux) return ESP_ERR_NO_MEM;

    UI_LOCK();
    lv_init();
    lv_tick_set_cb(tick_cb);

    s_buf1 = heap_caps_malloc(UI_DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_buf2 = heap_caps_malloc(UI_DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_buf1 || !s_buf2) {
        UI_UNLOCK();
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(480, 480);
    lv_display_add_event_cb(s_disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, UI_DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    peripherals_lcd_set_flush_done_cb(ui_flush_done, s_disp);

    build_all();

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touch_read_cb);

    lv_timer_create(anim_cb, 120, NULL);
    s_ready = true;
    UI_UNLOCK();

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 3, NULL, 1);
    ESP_LOGI(TAG, "UI ready 480x480");
    return ESP_OK;
}
