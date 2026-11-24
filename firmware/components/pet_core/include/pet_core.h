#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 与 v0.3 方案一致的时间/数值 ============ */
#define PET_STAT_MIN                 0
#define PET_STAT_MAX                 100
#define PET_STAT_INITIAL             60.0f

#define PET_AWAKE_SATIETY_PER_MIN    5.0f
#define PET_AWAKE_ENERGY_PER_MIN     3.0f
#define PET_SLEEP_SATIETY_PER_MIN    2.0f
#define PET_SLEEP_ENERGY_PER_MIN     15.0f

#define PET_FEED_AMOUNT              10.0f
#define PET_FEED_COOLDOWN_MS         3000
#define PET_FEED_FULL_AT             90.0f

#define PET_HUG_HOLD_MS              2000
#define PET_HUG_COOLDOWN_MS          10000
#define PET_HUG_BOND                 5.0f

#define PET_PAD_ARBITRATION_MS       220
#define PET_PAD_TAP_MAX_MS           500
#define PET_POST_ACTION_NEUTRAL_MS   250
#define PET_GAME_INPUT_LOCK_MS       100

#define PET_GAME_DURATION_MS         10000
#define PET_GAME_TARGET_STARS        6
#define PET_GAME_LANES               3
#define PET_GAME_STAR_FALL_MS        850
#define PET_GAME_STAR_GAP_MS         260
#define PET_GAME_ENERGY_COST         10.0f
#define PET_GAME_REWARD_MAX          20

#define PET_IDLE_REMINDER_MS         60000

#define PET_LIGHT_SETTLE_MS          800

#define PET_CARES_PER_FORM           10

typedef enum {
    PET_FORM_BABY = 0,
    PET_FORM_ACTIVE,
    PET_FORM_ROUND,
    PET_FORM_CLINGY,
    PET_FORM_MAX
} pet_form_t;

typedef enum {
    PET_CARE_EXERCISE = 0,
    PET_CARE_FEED,
    PET_CARE_PET,
    PET_CARE_MAX
} pet_care_t;

typedef enum {
    PET_MODE_IDLE = 0,
    PET_MODE_EATING,
    PET_MODE_GAME,
    PET_MODE_GAME_PAUSED,
    PET_MODE_RESULT,
    PET_MODE_HUG,
    PET_MODE_SLEEP,
    PET_MODE_EVOLVE
} pet_mode_t;

typedef enum {
    PET_RESULT_NONE = 0,
    PET_RESULT_SUCCESS,
    PET_RESULT_FAIL
} pet_result_t;

typedef enum {
    PET_PAD_LEFT = 0,
    PET_PAD_RIGHT = 1,
    PET_PAD_COUNT = 2
} pet_pad_t;

typedef enum {
    PET_EV_NONE = 0,
    PET_EV_FEED_OK,
    PET_EV_FEED_BLOCKED,
    PET_EV_FEED_COOLDOWN,
    PET_EV_HUG_DONE,
    PET_EV_HUG_COOLDOWN,
    PET_EV_GAME_START,
    PET_EV_GAME_MOVE,
    PET_EV_GAME_STAR,
    PET_EV_GAME_MISS,
    PET_EV_GAME_WON,
    PET_EV_GAME_LOST,
    PET_EV_GAME_PAUSED,
    PET_EV_GAME_RESUMED,
    PET_EV_GAME_ABORTED,
    PET_EV_LOW_ENERGY,
    PET_EV_SLEEP,
    PET_EV_WAKE,
    PET_EV_EVOLVE,
    PET_EV_IDLE_REMINDER,
    PET_EV_PAD_HINT
} pet_event_t;

typedef struct {
    /* 核心属性 */
    float satiety;
    float energy;
    float intimacy;
    bool sleeping;

    char name[16];
    bool needs_name;
    bool muted;

    pet_form_t form;
    bool unlocked[PET_FORM_MAX];

    /* 照顾计数：本轮 + 最近顺序（并列时取最近） */
    uint16_t care_counts[PET_CARE_MAX];
    uint32_t last_care_order[PET_CARE_MAX];
    uint32_t care_seq;

    /* 昼夜与时间 */
    uint32_t cycle_ms;          /* legacy NVS field, unused */
    uint32_t last_tick_ms;
    uint32_t last_interaction_ms;
    uint32_t last_reminder_ms;
    uint32_t last_feed_ms;
    uint32_t last_hug_ms;
    uint32_t mode_until_ms;     /* 咀嚼结束等 */

    /* 游戏公开状态，供 UI 绘制 */
    pet_mode_t mode;
    pet_result_t result;
    uint8_t game_lane;
    int8_t game_star_lane;     /* -1 = 当前没有星星 */
    uint8_t game_stars;
    uint32_t game_elapsed_ms;
    bool game_star_visible;
    bool game_both_pressed;
    int game_reward;
    int game_net;

    uint32_t rng_state;

    /* 触摸垫公开状态，供 UI 显示按压反馈 */
    bool pad_down[PET_PAD_COUNT];
} pet_state_t;

typedef void (*pet_event_cb_t)(pet_event_t event, void *user_data);

typedef struct {
    pet_state_t state;

    pet_event_cb_t on_event;
    void *user_data;

    /* 单垫/双垫仲裁 */
    bool gesture_active;
    bool gesture_both;
    bool gesture_second_seen;
    uint8_t gesture_first_pad;
    uint32_t gesture_start_ms;
    uint32_t hug_start_ms;
    bool hug_rewarded;
    bool wait_release_all;
    uint32_t post_action_neutral_until_ms;

    /* 游戏内部计时 */
    uint32_t game_star_timer_ms;
    uint32_t game_star_gap_ms;
    uint32_t game_input_lock_until_ms;

    /* 光敏：亮/暗决定默认状态；manual_override 允许手动覆盖 */
    bool light_valid;
    bool light_bright;
    bool manual_override;
    uint32_t light_state_since_ms;

    /* 最近一次操作，用于结果页重试 */
    uint32_t result_shown_ms;
    bool result_right_pending;
} pet_core_t;

void pet_core_init(pet_core_t *core, uint32_t now_ms);
void pet_core_reset_fresh(pet_core_t *core, uint32_t now_ms);
void pet_core_set_callbacks(pet_core_t *core, pet_event_cb_t cb, void *user_data);

/* 100ms 或更快调用；内部会计算 elapsed 并推进属性、昼夜、游戏、咀嚼和提醒 */
void pet_core_tick(pet_core_t *core, uint32_t now_ms);

/* 触摸垫边沿事件。main 从已消抖的 GPIO 读取并调用。 */
void pet_core_pad_event(pet_core_t *core, pet_pad_t pad, bool down, uint32_t now_ms);

/* 屏幕/物理兜底按钮，以及当前屏幕被触摸时重置无操作计时 */
void pet_core_note_screen_touch(pet_core_t *core, uint32_t now_ms);

/* 喂食：可用于左垫仲裁完成，也可用于游戏暂停页的屏幕按钮 */
bool pet_core_feed(pet_core_t *core, uint32_t now_ms);
bool pet_core_hug_fallback(pet_core_t *core, uint32_t now_ms);

/* 按星数计算精力奖励：round(20 * stars / 6)，0..20 */
int pet_core_game_reward(int stars);

/* 游戏控制：开局先扣 10 精力 */
bool pet_core_start_game(pet_core_t *core, uint32_t now_ms);
void pet_core_pause_game(pet_core_t *core, uint32_t now_ms);
void pet_core_resume_game(pet_core_t *core, uint32_t now_ms);
void pet_core_abort_game(pet_core_t *core, uint32_t now_ms);
void pet_core_retry_game(pet_core_t *core, uint32_t now_ms);
void pet_core_exit_result(pet_core_t *core, uint32_t now_ms);

/* 睡眠/唤醒；仅由光敏亮暗驱动 */
bool pet_core_sleep(pet_core_t *core, uint32_t now_ms);
bool pet_core_wake(pet_core_t *core, uint32_t now_ms);

/* 手动睡眠/叫醒：优先于光敏自动状态，直到环境亮暗发生一次变化 */
bool pet_core_manual_sleep(pet_core_t *core, uint32_t now_ms);
bool pet_core_manual_wake(pet_core_t *core, uint32_t now_ms);

/* LDR 采样 0..100（当前驱动输出二值 0/100）：
 * 亮=醒，暗=睡；800ms 去抖，避免抖动反复切换。 */
void pet_core_update_light(pet_core_t *core, float light_pct, uint32_t now_ms);

/* 成长动画结束 */
void pet_core_ack_evolution(pet_core_t *core, uint32_t now_ms);

/* 命名 */
bool pet_core_set_name(pet_core_t *core, const char *name);
void pet_core_complete_naming(pet_core_t *core, uint32_t now_ms);
void pet_core_set_muted(pet_core_t *core, bool muted);

/* 工具 */
int pet_core_round_total(const pet_core_t *core);
int pet_core_round_remaining(const pet_core_t *core);
const char *pet_core_form_name(pet_form_t form);
const char *pet_core_care_name(pet_care_t care);
pet_form_t pet_core_form_from_care(pet_care_t care);
uint32_t pet_core_game_star_time_left(const pet_core_t *core);

#ifdef __cplusplus
}
#endif
