/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 桌宠核心逻辑：与平台无关（不依赖 ESP-IDF / LVGL），时间由调用者注入，
 * 便于在宿主机上做时序与边界单元测试。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PET_ATTR_MIN 0
#define PET_ATTR_MAX 100
#define PET_ATTR_INIT_AFFECTION 50
#define PET_ATTR_INIT_ENERGY 50
#define PET_ATTR_INIT_SATIETY 0

/** 属性变化步长 */
#define PET_LOVE_STEP 5
#define PET_FEED_STEP 5

/** 快照固定长度：magic(4) + version(1) + 三项属性(3) + crc16(2) */
#define PET_SNAPSHOT_SIZE 10
#define PET_SNAPSHOT_VERSION 1

typedef enum {
    PET_STATE_IDLE = 0, /*!< 清醒待机 */
    PET_STATE_GAME,     /*!< 泡泡游戏 */
    PET_STATE_SETTLE,   /*!< 结算展示 */
    PET_STATE_SLEEP,    /*!< 睡眠 */
} pet_state_t;

typedef enum {
    PET_EVT_STATE,        /*!< a = 新状态 */
    PET_EVT_ATTRIBUTES,   /*!< 任一属性变化；a/b 未使用 */
    PET_EVT_LOVE,         /*!< 长按抚摸成功；a = 实际增加值 */
    PET_EVT_FED,          /*!< 喂食成功；a = 实际增加值 */
    PET_EVT_GAME_START,   /*!< 游戏开始 */
    PET_EVT_GAME_SCORE,   /*!< a = 当前得分 */
    PET_EVT_GAME_END,     /*!< a = 总分, b = 实际精力增量 */
    PET_EVT_SETTLE_END,   /*!< 结算展示结束 */
    PET_EVT_SLEEP_ENTER,  /*!< 因黑暗入睡 */
    PET_EVT_WAKE,         /*!< a = 1 手动唤醒(三连击), 0 光线自动唤醒 */
    PET_EVT_TAP_PROGRESS, /*!< a = 已累计点击次数(1/2) */
    PET_EVT_LIGHT,        /*!< a = 1 当前判定为暗, b = 滤波后亮度 0-100 */
} pet_event_type_t;

typedef struct {
    pet_event_type_t type;
    int a;
    int b;
} pet_event_t;

typedef void (*pet_event_cb_t)(void *ctx, const pet_event_t *event);

typedef struct {
    uint32_t game_duration_ms;     /*!< 每轮 15s */
    uint32_t settle_duration_ms;   /*!< 结算展示 3s */
    uint32_t triple_tap_window_ms; /*!< 三连击窗口 800ms，可配置 */
    uint32_t tap_max_ms;           /*!< 短按上限，超过不算 tap */
    uint32_t long_press_ms;        /*!< 抚摸长按 3s */
    uint32_t energy_decay_ms;      /*!< 清醒待机 60s -1 */
    uint32_t satiety_decay_ms;     /*!< 所有状态 5min -1 */
    uint32_t sleep_gain_ms;        /*!< 睡眠 30s +1 */
    uint32_t wake_protection_ms;   /*!< 黑暗手动唤醒保护 3min */
    uint8_t light_dark_threshold;  /*!< 低于此亮度判暗 */
    uint8_t light_bright_threshold;/*!< 高于此亮度判亮 */
    uint32_t light_confirm_ms;     /*!< 明暗持续确认时间 */
    bool light_bright_is_high;     /*!< light_level 越高是否越亮（实机校准） */
} pet_config_t;

/** 测试与实机均可覆盖的默认配置。 */
pet_config_t pet_config_default(void);

typedef struct pet {
    pet_config_t cfg;
    pet_event_cb_t emit;
    void *emit_ctx;

    pet_state_t state;
    uint8_t affection;
    uint8_t energy;
    uint8_t satiety;
    bool dirty;

    uint64_t now_ms;

    /* 属性计时（暂停时只停止累加，不清零） */
    uint32_t awake_ms;
    uint32_t satiety_ms;
    uint32_t sleep_ms;

    /* 游戏/结算 */
    uint64_t game_start_ms;
    uint32_t game_elapsed_ms;
    uint32_t settle_elapsed_ms;
    uint32_t score;

    /* 长按/连击 */
    uint64_t press_start_ms;
    bool pressing;
    bool long_fired;
    uint8_t taps;
    uint64_t first_tap_ms;

    /* 黑暗手动唤醒保护截止时间（绝对时间） */
    uint64_t protection_until_ms;

    /* 光敏滤波（亮度域 0-100，q8 定点） */
    bool light_have_ema;
    int light_ema_q8;
    bool light_known;
    bool light_is_dark;
    bool light_cand_valid;
    bool light_cand_dark;
    uint64_t light_cand_since_ms;
} pet_t;

/** 初始化默认属性 50/50/0 并进入 IDLE。 */
void pet_init(pet_t *pet, const pet_config_t *cfg, pet_event_cb_t emit, void *emit_ctx,
              uint64_t now_ms);

/** 推进时间与状态机；非阻塞，建议 20~50ms 调用一次。 */
void pet_tick(pet_t *pet, uint64_t now_ms);

/** 触摸按下/抬起。时间由 LVGL 事件回调传入。 */
void pet_touch_press(pet_t *pet, uint64_t now_ms);
void pet_touch_release(pet_t *pet, uint64_t now_ms);

/** AI 实体键喂食（仅 IDLE 生效）。返回是否实际生效。 */
bool pet_feed(pet_t *pet);

/** 泡泡命中（仅 GAME 且未到结束时间生效）。每个泡泡由 UI 保证只调用一次。 */
bool pet_game_hit(pet_t *pet, uint64_t now_ms);

/** 光敏采样，level 为 0-100 相对亮度。内部做极性归一、EMA、双阈值与持续确认。 */
void pet_light_sample(pet_t *pet, int level, uint64_t now_ms);

/** 只读访问器 */
pet_state_t pet_state(const pet_t *pet);
uint8_t pet_affection(const pet_t *pet);
uint8_t pet_energy(const pet_t *pet);
uint8_t pet_satiety(const pet_t *pet);
uint32_t pet_score(const pet_t *pet);
uint32_t pet_game_remaining_ms(const pet_t *pet);
bool pet_light_is_dark(const pet_t *pet);
bool pet_light_is_known(const pet_t *pet);
int pet_light_level(const pet_t *pet);

/** 属性是否变化过（调用 clear 后复位）。 */
bool pet_is_dirty(const pet_t *pet);
void pet_clear_dirty(pet_t *pet);

/** 属性快照序列化（断电保存）。返回写入字节数，失败返回 0。 */
size_t pet_snapshot_write(const pet_t *pet, uint8_t *out, size_t cap);

/** 反序列化；magic/version/crc 校验失败返回 false，且不修改属性。 */
bool pet_snapshot_read(pet_t *pet, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
