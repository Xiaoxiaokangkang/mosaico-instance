/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pet_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "lvgl.h"

#define UI_SIZE 480

#define COLOR_BG 0x0E1521
#define COLOR_CARD 0x1A2436
#define COLOR_TEXT 0xF2F6FC
#define COLOR_MUTED 0x8A9BB4
#define COLOR_LOVE 0xFF6B9A
#define COLOR_ENERGY 0x4FD1C5
#define COLOR_FULL 0xFFB454
#define COLOR_OK 0x34D399

#define BUBBLE_SPAWN_MS 650
#define BUBBLE_LIFE_MS 3000
#define BUBBLE_WARN_MS 500           /* 生命最后 500ms 做 shrink + fade */
#define PLUS_LIFE_MS 800
#define GAME_LAYER_START_DELAY_MS 350
#define INTRO_READY_MS 800           /* "READY..." 持续 */
#define INTRO_COUNTDOWN_MS 3000      /* "3"→"2"→"1" 各 1s */
#define INTRO_GO_MS 500              /* "GO!" 持续 */
#define INTRO_TOTAL_MS (INTRO_READY_MS + INTRO_COUNTDOWN_MS + INTRO_GO_MS)

/* 泡泡运动参数：上升为主，水平小幅漂移，边缘反弹。 */
#define BUBBLE_SPEED_UP_MIN 25.0f    /* px/s */
#define BUBBLE_SPEED_UP_MAX 55.0f
#define BUBBLE_SPEED_X_MIN (-12.0f)
#define BUBBLE_SPEED_X_MAX 12.0f
#define BUBBLE_X_BOUNDS 24           /* 屏幕左右安全边距 */
#define BUBBLE_Y_TOP_FLOOR 64        /* 不能飘到顶（要让开倒计时文字） */
#define BUBBLE_Y_BOTTOM_CEIL 430     /* 屏幕下沿之上（480-50） */

typedef struct {
    lv_obj_t *obj;
    uint32_t id;
    int x;
    int y;
    int radius;
    uint64_t born_ms;
    bool used;
    float vx;  /* 水平速度 px/s，正向右 */
    float vy;  /* 垂直速度 px/s，正向下（负值=上升） */
} pet_bubble_t;

typedef struct {
    lv_obj_t *label;
    uint64_t born_ms;
} pet_plus_t;

struct pet_ui {
    lv_obj_t *screen;
    lv_obj_t *badge;
    lv_obj_t *light_label;

    lv_obj_t *pet_root;
    lv_obj_t *body;
    lv_obj_t *ear_l;
    lv_obj_t *ear_r;
    lv_obj_t *eye_l;
    lv_obj_t *eye_r;
    lv_obj_t *sleep_eye_l;
    lv_obj_t *sleep_eye_r;
    lv_obj_t *mouth;
    lv_obj_t *cheek_l;
    lv_obj_t *cheek_r;
    lv_obj_t *zzz;

    lv_obj_t *hint;
    lv_obj_t *feedback;
    lv_obj_t *bar[3];
    lv_obj_t *value[3];

    lv_obj_t *game_layer;
    lv_obj_t *countdown;
    lv_obj_t *score_label;
    lv_obj_t *result_card;
    lv_obj_t *result_score;
    lv_obj_t *result_gain;
    lv_obj_t *intro_label;   /* "READY / 3 / 2 / 1 / GO!" 开场文字 */

    pet_bubble_t bubbles[PET_UI_MAX_BUBBLES];
    pet_plus_t plus[PET_UI_MAX_PLUS];
    uint32_t next_bubble_id;

    uint64_t now_ms;
    uint64_t end_ms;
    uint64_t intro_end_ms;   /* 开场结束时间，之后才开始生成泡泡 */
    uint64_t next_spawn_ms;  /* 下一颗泡泡的生成时间（uint64 方便和 now_ms 比较） */
    uint32_t last_seconds;
    bool game_active;

    pet_ui_bubble_cb_t bubble_cb;
    void *bubble_ctx;
};

static const uint32_t s_bubble_colors[] = {0x5AA9FF, 0xFF6B9A, 0x4FD1C5, 0xFFB454, 0xB388FF};

static void apply_sleep_look(pet_ui_t *ui, bool sleeping);

static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color,
                          int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, uint32_t color,
                            const lv_font_t *font, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static void create_pet(pet_ui_t *ui)
{
    ui->pet_root = lv_obj_create(ui->screen);
    lv_obj_set_pos(ui->pet_root, 110, 78);
    lv_obj_set_size(ui->pet_root, 260, 250);
    lv_obj_set_style_bg_opa(ui->pet_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui->pet_root, 0, 0);
    lv_obj_set_style_pad_all(ui->pet_root, 0, 0);
    lv_obj_remove_flag(ui->pet_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui->pet_root, LV_OBJ_FLAG_CLICKABLE);

    ui->ear_l = make_box(ui->pet_root, 22, 6, 62, 62, COLOR_ENERGY, LV_RADIUS_CIRCLE);
    ui->ear_r = make_box(ui->pet_root, 176, 6, 62, 62, COLOR_ENERGY, LV_RADIUS_CIRCLE);
    ui->body = make_box(ui->pet_root, 30, 34, 200, 196, COLOR_ENERGY, 98);
    lv_obj_set_style_shadow_width(ui->body, 26, 0);
    lv_obj_set_style_shadow_opa(ui->body, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(ui->body, lv_color_hex(COLOR_ENERGY), 0);

    ui->cheek_l = make_box(ui->pet_root, 68, 150, 28, 16, COLOR_LOVE, 8);
    lv_obj_set_style_bg_opa(ui->cheek_l, LV_OPA_60, 0);
    ui->cheek_r = make_box(ui->pet_root, 164, 150, 28, 16, COLOR_LOVE, 8);
    lv_obj_set_style_bg_opa(ui->cheek_r, LV_OPA_60, 0);

    ui->eye_l = make_box(ui->pet_root, 92, 112, 30, 30, COLOR_BG, LV_RADIUS_CIRCLE);
    ui->eye_r = make_box(ui->pet_root, 138, 112, 30, 30, COLOR_BG, LV_RADIUS_CIRCLE);

    ui->sleep_eye_l = make_box(ui->pet_root, 92, 126, 30, 6, COLOR_BG, 3);
    ui->sleep_eye_r = make_box(ui->pet_root, 138, 126, 30, 6, COLOR_BG, 3);
    lv_obj_add_flag(ui->sleep_eye_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui->sleep_eye_r, LV_OBJ_FLAG_HIDDEN);

    ui->mouth = make_box(ui->pet_root, 110, 168, 40, 12, COLOR_BG, 6);

    ui->zzz = make_label(ui->pet_root, "Z z z", COLOR_TEXT, &lv_font_montserrat_20, 190, 4);
    lv_obj_add_flag(ui->zzz, LV_OBJ_FLAG_HIDDEN);
}

static void create_attributes(pet_ui_t *ui)
{
    static const char *names[3] = {"LOVE", "ENERGY", "FULL"};
    static const uint32_t colors[3] = {COLOR_LOVE, COLOR_ENERGY, COLOR_FULL};

    for (int i = 0; i < 3; ++i) {
        const int y = 348 + i * 32;
        lv_obj_t *label = make_label(ui->screen, names[i], COLOR_MUTED, &lv_font_montserrat_14,
                                     90, y - 3);
        (void)label;

        ui->bar[i] = lv_bar_create(ui->screen);
        lv_obj_set_pos(ui->bar[i], 156, y);
        lv_obj_set_size(ui->bar[i], 170, 12);
        lv_bar_set_range(ui->bar[i], 0, 100);
        lv_bar_set_value(ui->bar[i], 50, LV_ANIM_OFF);
        lv_obj_set_style_radius(ui->bar[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui->bar[i], lv_color_hex(COLOR_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui->bar[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(ui->bar[i], LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui->bar[i], lv_color_hex(colors[i]), LV_PART_INDICATOR);
        lv_obj_remove_flag(ui->bar[i], LV_OBJ_FLAG_CLICKABLE);

        ui->value[i] = make_label(ui->screen, "50", COLOR_TEXT, &lv_font_montserrat_14, 332, y - 3);
        lv_obj_set_width(ui->value[i], 48);
        lv_obj_set_style_text_align(ui->value[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
}

static void create_game(pet_ui_t *ui)
{
    ui->game_layer = lv_obj_create(ui->screen);
    lv_obj_set_pos(ui->game_layer, 0, 0);
    lv_obj_set_size(ui->game_layer, UI_SIZE, UI_SIZE);
    lv_obj_set_style_bg_color(ui->game_layer, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(ui->game_layer, LV_OPA_90, 0);
    lv_obj_set_style_border_width(ui->game_layer, 0, 0);
    lv_obj_set_style_radius(ui->game_layer, 0, 0);
    lv_obj_set_style_pad_all(ui->game_layer, 0, 0);
    lv_obj_remove_flag(ui->game_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui->game_layer, LV_OBJ_FLAG_CLICKABLE);

    ui->countdown = make_label(ui->game_layer, "15", COLOR_TEXT, &lv_font_montserrat_28, 24, 22);
    ui->score_label = make_label(ui->game_layer, "SCORE 0", COLOR_ENERGY, &lv_font_montserrat_20,
                                 330, 30);

    /* 开场倒计时文字：居中、大字、醒目的能量色。 */
    ui->intro_label = lv_label_create(ui->game_layer);
    lv_label_set_text(ui->intro_label, "READY");
    lv_obj_set_style_text_color(ui->intro_label, lv_color_hex(COLOR_ENERGY), 0);
    lv_obj_set_style_text_font(ui->intro_label, &lv_font_montserrat_28, 0);
    lv_obj_center(ui->intro_label);
    lv_obj_add_flag(ui->intro_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(ui->game_layer, LV_OBJ_FLAG_HIDDEN);

    ui->result_card = lv_obj_create(ui->screen);
    lv_obj_set_size(ui->result_card, 340, 230);
    lv_obj_center(ui->result_card);
    lv_obj_set_style_bg_color(ui->result_card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(ui->result_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui->result_card, 32, 0);
    lv_obj_set_style_border_width(ui->result_card, 1, 0);
    lv_obj_set_style_border_color(ui->result_card, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_remove_flag(ui->result_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ui->result_card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(ui->result_card);
    lv_label_set_text(title, "ROUND OVER");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    ui->result_score = lv_label_create(ui->result_card);
    lv_label_set_text(ui->result_score, "0");
    lv_obj_set_style_text_color(ui->result_score, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(ui->result_score, &lv_font_montserrat_28, 0);
    lv_obj_align(ui->result_score, LV_ALIGN_TOP_MID, 0, 62);

    ui->result_gain = lv_label_create(ui->result_card);
    lv_label_set_text(ui->result_gain, "ENERGY +0");
    lv_obj_set_style_text_color(ui->result_gain, lv_color_hex(COLOR_ENERGY), 0);
    lv_obj_set_style_text_font(ui->result_gain, &lv_font_montserrat_20, 0);
    lv_obj_align(ui->result_gain, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_add_flag(ui->result_card, LV_OBJ_FLAG_HIDDEN);
}

pet_ui_t *pet_ui_create(pet_ui_bubble_cb_t bubble_cb, void *bubble_ctx)
{
    static pet_ui_t ui_storage;
    pet_ui_t *ui = &ui_storage;
    memset(ui, 0, sizeof(*ui));
    ui->bubble_cb = bubble_cb;
    ui->bubble_ctx = bubble_ctx;
    ui->next_bubble_id = 1;

    ui->screen = lv_screen_active();
    lv_obj_remove_flag(ui->screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui->screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ui->screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(ui->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui->screen, 0, 0);

    ui->badge = lv_label_create(ui->screen);
    lv_label_set_text(ui->badge, "AWAKE");
    lv_obj_set_style_text_color(ui->badge, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_font(ui->badge, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ui->badge, lv_color_hex(COLOR_OK), 0);
    lv_obj_set_style_bg_opa(ui->badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui->badge, 14, 0);
    lv_obj_set_style_pad_hor(ui->badge, 16, 0);
    lv_obj_set_style_pad_ver(ui->badge, 6, 0);
    lv_obj_align(ui->badge, LV_ALIGN_TOP_MID, 0, 22);

    ui->light_label = make_label(ui->screen, "LIGHT --", COLOR_MUTED, &lv_font_montserrat_14,
                                 28, 32);

    create_pet(ui);
    create_attributes(ui);
    create_game(ui);

    ui->hint = make_label(ui->screen, "HOLD 3s  PET   /   3x TAP  PLAY", COLOR_MUTED,
                          &lv_font_montserrat_14, 0, 0);
    lv_obj_align(ui->hint, LV_ALIGN_TOP_MID, 0, 330);

    ui->feedback = make_label(ui->screen, "", COLOR_OK, &lv_font_montserrat_20, 0, 0);
    lv_obj_align(ui->feedback, LV_ALIGN_TOP_MID, 0, 286);
    lv_obj_add_flag(ui->feedback, LV_OBJ_FLAG_HIDDEN);

    return ui;
}

void pet_ui_set_attributes(pet_ui_t *ui, int affection, int energy, int satiety)
{
    const int values[3] = {affection, energy, satiety};
    char text[8];
    for (int i = 0; i < 3; ++i) {
        lv_bar_set_value(ui->bar[i], values[i], LV_ANIM_OFF);
        snprintf(text, sizeof(text), "%d", values[i]);
        lv_label_set_text(ui->value[i], text);
    }
}

void pet_ui_set_state(pet_ui_t *ui, pet_state_t state)
{
    switch (state) {
    case PET_STATE_GAME:
        lv_obj_add_flag(ui->pet_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui->badge, "PLAYING");
        lv_obj_set_style_bg_color(ui->badge, lv_color_hex(COLOR_LOVE), 0);
        break;
    case PET_STATE_SETTLE:
        lv_obj_remove_flag(ui->pet_root, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui->badge, "RESULT");
        lv_obj_set_style_bg_color(ui->badge, lv_color_hex(COLOR_FULL), 0);
        break;
    case PET_STATE_SLEEP:
        lv_obj_remove_flag(ui->pet_root, LV_OBJ_FLAG_HIDDEN);
        apply_sleep_look(ui, true);
        lv_label_set_text(ui->badge, "SLEEPING");
        lv_obj_set_style_bg_color(ui->badge, lv_color_hex(0x5B6B86), 0);
        lv_obj_add_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
        break;
    case PET_STATE_IDLE:
    default:
        lv_obj_remove_flag(ui->pet_root, LV_OBJ_FLAG_HIDDEN);
        apply_sleep_look(ui, false);
        lv_label_set_text(ui->badge, "AWAKE");
        lv_obj_set_style_bg_color(ui->badge, lv_color_hex(COLOR_OK), 0);
        lv_obj_remove_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void pet_ui_set_light(pet_ui_t *ui, bool known, bool dark, int level)
{
    if (!known) {
        lv_label_set_text(ui->light_label, "LIGHT --");
        return;
    }
    lv_label_set_text_fmt(ui->light_label, "LIGHT %d%%  %s", level, dark ? "MOON" : "SUN");
}

void pet_ui_set_hint(pet_ui_t *ui, const char *text)
{
    if (!text) {
        lv_obj_add_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(ui->hint, text);
    lv_obj_remove_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
}

void pet_ui_feedback(pet_ui_t *ui, const char *text, uint32_t color)
{
    lv_label_set_text(ui->feedback, text);
    lv_obj_set_style_text_color(ui->feedback, lv_color_hex(color), 0);
    lv_obj_remove_flag(ui->feedback, LV_OBJ_FLAG_HIDDEN);
}

void pet_ui_hide_feedback(pet_ui_t *ui)
{
    lv_obj_add_flag(ui->feedback, LV_OBJ_FLAG_HIDDEN);
}

void pet_ui_game_begin(pet_ui_t *ui, uint64_t now_ms, uint32_t duration_ms)
{
    ui->now_ms = now_ms;
    ui->end_ms = now_ms + duration_ms;
    ui->intro_end_ms = now_ms + INTRO_TOTAL_MS;
    ui->next_spawn_ms = ui->intro_end_ms + BUBBLE_SPAWN_MS; /* 开场结束后再等一个 spawn 周期 */
    ui->last_seconds = UINT32_MAX;
    ui->game_active = true;

    for (int i = 0; i < PET_UI_MAX_BUBBLES; ++i) {
        if (ui->bubbles[i].obj) {
            lv_obj_delete(ui->bubbles[i].obj);
        }
        memset(&ui->bubbles[i], 0, sizeof(ui->bubbles[i]));
    }
    for (int i = 0; i < PET_UI_MAX_PLUS; ++i) {
        if (ui->plus[i].label) {
            lv_obj_delete(ui->plus[i].label);
        }
        memset(&ui->plus[i], 0, sizeof(ui->plus[i]));
    }

    lv_label_set_text(ui->countdown, "15");
    lv_label_set_text(ui->score_label, "SCORE 0");
    lv_obj_add_flag(ui->result_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui->game_layer, LV_OBJ_FLAG_HIDDEN);

    /* 显示开场 READY 文字（后续 tick 会推进到 3-2-1-GO!）。 */
    lv_label_set_text(ui->intro_label, "READY");
    lv_obj_remove_flag(ui->intro_label, LV_OBJ_FLAG_HIDDEN);
}

static void bubble_clicked(lv_event_t *event)
{
    pet_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(event);
    for (int i = 0; i < PET_UI_MAX_BUBBLES; ++i) {
        pet_bubble_t *bubble = &ui->bubbles[i];
        if (bubble->obj != target || bubble->used) {
            continue;
        }
        bubble->used = true;
        lv_obj_add_flag(bubble->obj, LV_OBJ_FLAG_HIDDEN);
        if (ui->now_ms >= ui->end_ms) {
            return;
        }
        /* 本地立即反馈 +1，计分由应用任务调用 pet_core 完成。 */
        for (int p = 0; p < PET_UI_MAX_PLUS; ++p) {
            if (!ui->plus[p].label) {
                lv_obj_t *label = lv_label_create(ui->game_layer);
                lv_label_set_text(label, "+1");
                lv_obj_set_style_text_color(label, lv_color_hex(COLOR_OK), 0);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
                lv_obj_set_pos(label, bubble->x, bubble->y);
                ui->plus[p].label = label;
                ui->plus[p].born_ms = ui->now_ms;
                break;
            }
        }
        if (ui->bubble_cb) {
            ui->bubble_cb(ui->bubble_ctx, bubble->id);
        }
        return;
    }
}

static bool bubble_overlaps(pet_ui_t *ui, int x, int y, int radius)
{
    for (int i = 0; i < PET_UI_MAX_BUBBLES; ++i) {
        const pet_bubble_t *other = &ui->bubbles[i];
        if (!other->obj) {
            continue;
        }
        const int dx = (x + radius) - (other->x + other->radius);
        const int dy = (y + radius) - (other->y + other->radius);
        const int min_dist = radius + other->radius + 8;
        if (dx * dx + dy * dy < min_dist * min_dist) {
            return true;
        }
    }
    return false;
}

static float rand_float_range(float lo, float hi)
{
    /* esp_random() 均匀分布在 [0, 2^32)；映射到 [lo, hi)。 */
    const float t = (float)(esp_random() >> 8) / 16777216.0f;
    return lo + t * (hi - lo);
}

static void spawn_bubble(pet_ui_t *ui)
{
    int slot = -1;
    for (int i = 0; i < PET_UI_MAX_BUBBLES; ++i) {
        if (!ui->bubbles[i].obj) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }

    const int radius = 34 + (int)(esp_random() % 15);
    int x = 0;
    int y = 0;
    bool placed = false;
    /* 主要在屏幕下方生成，给上升空间。 */
    const int y_min = 250;
    const int y_max = BUBBLE_Y_BOTTOM_CEIL - radius * 2;
    const int x_min = BUBBLE_X_BOUNDS;
    const int x_max = UI_SIZE - BUBBLE_X_BOUNDS - radius * 2;
    for (int attempt = 0; attempt < 10; ++attempt) {
        x = x_min + (int)(esp_random() % (uint32_t)(x_max - x_min + 1));
        y = y_min + (int)(esp_random() % (uint32_t)(y_max - y_min + 1));
        if (!bubble_overlaps(ui, x, y, radius)) {
            placed = true;
            break;
        }
    }
    if (!placed) {
        return;
    }

    const float vx = rand_float_range(BUBBLE_SPEED_X_MIN, BUBBLE_SPEED_X_MAX);
    const float vy = rand_float_range(-BUBBLE_SPEED_UP_MAX, -BUBBLE_SPEED_UP_MIN);

    const uint32_t color = s_bubble_colors[esp_random() % (sizeof(s_bubble_colors) / sizeof(s_bubble_colors[0]))];
    lv_obj_t *obj = make_box(ui->game_layer, x, y, radius * 2, radius * 2, color, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_border_width(obj, 3, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_add_event_cb(obj, bubble_clicked, LV_EVENT_CLICKED, ui);

    ui->bubbles[slot] = (pet_bubble_t){
        .obj = obj,
        .id = ui->next_bubble_id++,
        .x = x,
        .y = y,
        .radius = radius,
        .born_ms = ui->now_ms,
        .used = false,
        .vx = vx,
        .vy = vy,
    };
}

void pet_ui_game_tick(pet_ui_t *ui, uint64_t now_ms)
{
    if (!ui->game_active) {
        return;
    }

    /* 计算帧间隔（秒），限制最大步长防止跳变。 */
    float dt = 0.0f;
    if (now_ms >= ui->now_ms && ui->now_ms > 0) {
        dt = (float)(now_ms - ui->now_ms) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
    }
    ui->now_ms = now_ms;

    /* --- 开场阶段：显示 READY / 3 / 2 / 1 / GO! --- */
    if (now_ms < ui->intro_end_ms) {
        const uint64_t intro_elapsed = now_ms - (ui->intro_end_ms - INTRO_TOTAL_MS);
        if (intro_elapsed < INTRO_READY_MS) {
            lv_label_set_text(ui->intro_label, "READY");
        } else if (intro_elapsed < INTRO_READY_MS + 1000) {
            lv_label_set_text(ui->intro_label, "3");
        } else if (intro_elapsed < INTRO_READY_MS + 2000) {
            lv_label_set_text(ui->intro_label, "2");
        } else if (intro_elapsed < INTRO_READY_MS + 3000) {
            lv_label_set_text(ui->intro_label, "1");
        } else {
            lv_label_set_text(ui->intro_label, "GO!");
        }
        /* 开场期间也更新倒计时（包括消耗的开场时间）。 */
        const uint32_t remaining = (uint32_t)(ui->end_ms - now_ms);
        const uint32_t seconds = (remaining + 999) / 1000;
        if (seconds != ui->last_seconds) {
            ui->last_seconds = seconds;
            lv_label_set_text_fmt(ui->countdown, "%u", (unsigned)seconds);
        }
        /* 开场期间不生成泡泡，但清理 plus 残留。 */
        for (int i = 0; i < PET_UI_MAX_PLUS; ++i) {
            if (ui->plus[i].label && now_ms - ui->plus[i].born_ms >= PLUS_LIFE_MS) {
                lv_obj_delete(ui->plus[i].label);
                ui->plus[i].label = NULL;
            }
        }
        return;
    }
    /* 开场结束后隐藏 intro_label（首次进入就隐藏）。 */
    lv_obj_add_flag(ui->intro_label, LV_OBJ_FLAG_HIDDEN);

    /* --- 清理 "+1" 浮字 --- */
    for (int i = 0; i < PET_UI_MAX_PLUS; ++i) {
        if (ui->plus[i].label && now_ms - ui->plus[i].born_ms >= PLUS_LIFE_MS) {
            lv_obj_delete(ui->plus[i].label);
            ui->plus[i].label = NULL;
        }
    }

    /* --- 每帧更新泡泡位置 + 消失动画 --- */
    for (int i = 0; i < PET_UI_MAX_BUBBLES; ++i) {
        pet_bubble_t *bubble = &ui->bubbles[i];
        if (!bubble->obj) {
            continue;
        }
        if (bubble->used) {
            lv_obj_delete(bubble->obj);
            memset(bubble, 0, sizeof(*bubble));
            continue;
        }
        const uint64_t age_ms = now_ms - bubble->born_ms;
        if (age_ms >= BUBBLE_LIFE_MS) {
            lv_obj_delete(bubble->obj);
            memset(bubble, 0, sizeof(*bubble));
            continue;
        }

        /* 位置更新 */
        int new_x = bubble->x + (int)(bubble->vx * dt);
        int new_y = bubble->y + (int)(bubble->vy * dt);

        /* 左右边界反弹 */
        const int left_bound = BUBBLE_X_BOUNDS;
        const int right_bound = UI_SIZE - BUBBLE_X_BOUNDS - bubble->radius * 2;
        if (new_x < left_bound) {
            new_x = left_bound;
            bubble->vx = -bubble->vx;
        } else if (new_x > right_bound) {
            new_x = right_bound;
            bubble->vx = -bubble->vx;
        }
        /* 上下边界反弹（到顶/到底就折返） */
        const int top_bound = BUBBLE_Y_TOP_FLOOR;
        const int bottom_bound = BUBBLE_Y_BOTTOM_CEIL - bubble->radius * 2;
        if (new_y < top_bound) {
            new_y = top_bound;
            bubble->vy = -bubble->vy;
        } else if (new_y > bottom_bound) {
            new_y = bottom_bound;
            bubble->vy = -bubble->vy;
        }

        bubble->x = new_x;
        bubble->y = new_y;
        lv_obj_set_pos(bubble->obj, new_x, new_y);

        /* 消失预警：最后 BUBBLE_WARN_MS 做 shrink + fade。 */
        if (age_ms >= BUBBLE_LIFE_MS - BUBBLE_WARN_MS) {
            const float warn_t = (float)(age_ms - (BUBBLE_LIFE_MS - BUBBLE_WARN_MS)) / (float)BUBBLE_WARN_MS;
            /* warn_t: 0→1 */
            const float shrink = 1.0f - warn_t * 0.6f; /* 缩到 40% 半径 */
            const int new_r = (int)((float)bubble->radius * shrink);
            const int new_d = new_r * 2;
            lv_obj_set_size(bubble->obj, new_d, new_d);
            const int opa = (int)(LV_OPA_80 * (1.0f - warn_t));
            lv_obj_set_style_bg_opa(bubble->obj, (lv_opa_t)(opa < LV_OPA_MIN ? LV_OPA_MIN : opa), 0);
            lv_obj_set_style_border_width(bubble->obj, (int)(3 * (1.0f - warn_t)), 0);
        }
    }

    if (now_ms >= ui->end_ms) {
        return;
    }

    /* --- 倒计时 --- */
    const uint32_t remaining = (uint32_t)(ui->end_ms - now_ms);
    const uint32_t seconds = (remaining + 999) / 1000;
    if (seconds != ui->last_seconds) {
        ui->last_seconds = seconds;
        lv_label_set_text_fmt(ui->countdown, "%u", (unsigned)seconds);
    }

    /* --- 生成新泡泡 --- */
    if (now_ms >= ui->next_spawn_ms) {
        ui->next_spawn_ms = now_ms + BUBBLE_SPAWN_MS;
        spawn_bubble(ui);
    }
}

void pet_ui_game_score(pet_ui_t *ui, uint32_t score)
{
    lv_label_set_text_fmt(ui->score_label, "SCORE %u", (unsigned)score);
}

void pet_ui_game_end(pet_ui_t *ui, uint32_t total, int gain)
{
    ui->game_active = false;
    for (int i = 0; i < PET_UI_MAX_BUBBLES; ++i) {
        if (ui->bubbles[i].obj) {
            lv_obj_delete(ui->bubbles[i].obj);
        }
        memset(&ui->bubbles[i], 0, sizeof(ui->bubbles[i]));
    }
    for (int i = 0; i < PET_UI_MAX_PLUS; ++i) {
        if (ui->plus[i].label) {
            lv_obj_delete(ui->plus[i].label);
        }
        memset(&ui->plus[i], 0, sizeof(ui->plus[i]));
    }
    lv_obj_add_flag(ui->game_layer, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text_fmt(ui->result_score, "%u", (unsigned)total);
    lv_label_set_text_fmt(ui->result_gain, "ENERGY +%d", gain);
    lv_obj_remove_flag(ui->result_card, LV_OBJ_FLAG_HIDDEN);
}

void pet_ui_settle_end(pet_ui_t *ui)
{
    lv_obj_add_flag(ui->result_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui->game_layer, LV_OBJ_FLAG_HIDDEN);
}

static void apply_sleep_look(pet_ui_t *ui, bool sleeping)
{
    if (sleeping) {
        lv_obj_add_flag(ui->eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui->sleep_eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui->sleep_eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui->zzz, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ui->body, lv_color_hex(0x5B6B86), 0);
        lv_obj_set_style_bg_color(ui->ear_l, lv_color_hex(0x5B6B86), 0);
        lv_obj_set_style_bg_color(ui->ear_r, lv_color_hex(0x5B6B86), 0);
        lv_obj_set_style_shadow_color(ui->body, lv_color_hex(0x5B6B86), 0);
    } else {
        lv_obj_remove_flag(ui->eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui->eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->sleep_eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->sleep_eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->zzz, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ui->body, lv_color_hex(COLOR_ENERGY), 0);
        lv_obj_set_style_bg_color(ui->ear_l, lv_color_hex(COLOR_ENERGY), 0);
        lv_obj_set_style_bg_color(ui->ear_r, lv_color_hex(COLOR_ENERGY), 0);
        lv_obj_set_style_shadow_color(ui->body, lv_color_hex(COLOR_ENERGY), 0);
    }
}
