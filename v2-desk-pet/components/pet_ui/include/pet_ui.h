/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 桌宠界面：纯 LVGL 绘制（占位图形），不包含业务计时。
 * 所有函数必须在持有 BSP 显示锁（bsp_display_lock）的上下文中调用；
 * LVGL 事件回调本身由 LVGL 任务加锁执行。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PET_UI_MAX_BUBBLES 6
#define PET_UI_MAX_PLUS 6

typedef struct pet_ui pet_ui_t;

/** 泡泡被点击时回调（LVGL 任务上下文，只做入队，不调用 pet_core）。 */
typedef void (*pet_ui_bubble_cb_t)(void *ctx, uint32_t bubble_id);

pet_ui_t *pet_ui_create(pet_ui_bubble_cb_t bubble_cb, void *bubble_ctx);

void pet_ui_set_attributes(pet_ui_t *ui, int affection, int energy, int satiety);
void pet_ui_set_state(pet_ui_t *ui, pet_state_t state);
void pet_ui_set_light(pet_ui_t *ui, bool known, bool dark, int level);
void pet_ui_set_hint(pet_ui_t *ui, const char *text);
void pet_ui_feedback(pet_ui_t *ui, const char *text, uint32_t color);
void pet_ui_hide_feedback(pet_ui_t *ui);

void pet_ui_game_begin(pet_ui_t *ui, uint64_t now_ms, uint32_t duration_ms);
void pet_ui_game_tick(pet_ui_t *ui, uint64_t now_ms);
void pet_ui_game_score(pet_ui_t *ui, uint32_t score);
void pet_ui_game_end(pet_ui_t *ui, uint32_t total, int gain);
void pet_ui_settle_end(pet_ui_t *ui);

#ifdef __cplusplus
}
#endif
