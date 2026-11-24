/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pet_core.h"

#include <string.h>

#define PET_SNAPSHOT_MAGIC "PTS1"

static void emit_event(pet_t *pet, pet_event_type_t type, int a, int b)
{
    if (pet->emit) {
        const pet_event_t event = {.type = type, .a = a, .b = b};
        pet->emit(pet->emit_ctx, &event);
    }
}

static uint8_t clamp_attr(int value)
{
    if (value < PET_ATTR_MIN) {
        return PET_ATTR_MIN;
    }
    if (value > PET_ATTR_MAX) {
        return PET_ATTR_MAX;
    }
    return (uint8_t)value;
}

static void set_attr(pet_t *pet, uint8_t *attr, int value)
{
    const uint8_t next = clamp_attr(value);
    if (*attr == next) {
        return;
    }
    *attr = next;
    pet->dirty = true;
    emit_event(pet, PET_EVT_ATTRIBUTES, 0, 0);
}

static void add_attr(pet_t *pet, uint8_t *attr, int delta)
{
    set_attr(pet, attr, (int)*attr + delta);
}

static void set_state(pet_t *pet, pet_state_t state)
{
    if (pet->state == state) {
        return;
    }
    pet->state = state;
    emit_event(pet, PET_EVT_STATE, (int)state, 0);
}

static void enter_sleep(pet_t *pet)
{
    pet->sleep_ms = 0;
    pet->pressing = false;
    pet->taps = 0;
    set_state(pet, PET_STATE_SLEEP);
    emit_event(pet, PET_EVT_SLEEP_ENTER, 0, 0);
}

static void wake_up(pet_t *pet, bool manual, uint64_t now_ms)
{
    pet->sleep_ms = 0;
    pet->pressing = false;
    pet->taps = 0;
    /* 黑暗中手动唤醒需要 3 分钟保护期；光线未知时按黑暗处理，避免立刻再入睡。 */
    const bool dark_now = !pet->light_known || pet->light_is_dark;
    if (manual && dark_now) {
        pet->protection_until_ms = now_ms + pet->cfg.wake_protection_ms;
    }
    set_state(pet, PET_STATE_IDLE);
    emit_event(pet, PET_EVT_WAKE, manual ? 1 : 0, 0);
}

static void start_game(pet_t *pet, uint64_t now_ms)
{
    pet->score = 0;
    pet->game_start_ms = now_ms;
    pet->game_elapsed_ms = 0;
    pet->settle_elapsed_ms = 0;
    pet->pressing = false;
    pet->long_fired = false;
    pet->taps = 0;
    set_state(pet, PET_STATE_GAME);
    emit_event(pet, PET_EVT_GAME_START, 0, 0);
}

static void end_game(pet_t *pet)
{
    pet->game_elapsed_ms = pet->cfg.game_duration_ms;
    pet->taps = 0;
    pet->pressing = false;
    const int before = pet->energy;
    add_attr(pet, &pet->energy, (int)pet->score);
    const int gain = (int)pet->energy - before;
    emit_event(pet, PET_EVT_GAME_END, (int)pet->score, gain);
    pet->settle_elapsed_ms = 0;
    set_state(pet, PET_STATE_SETTLE);
}

static void finish_settle(pet_t *pet)
{
    emit_event(pet, PET_EVT_SETTLE_END, (int)pet->score, 0);
    const bool protected_now = pet->now_ms < pet->protection_until_ms;
    if (!protected_now && pet->light_known && pet->light_is_dark) {
        enter_sleep(pet);
    } else {
        set_state(pet, PET_STATE_IDLE);
    }
}

/* 光线状态提交后只影响待机与睡眠，绝不打断游戏/结算。 */
static void evaluate_light(pet_t *pet, uint64_t now_ms)
{
    if (!pet->light_known) {
        return;
    }
    if (pet->state == PET_STATE_IDLE) {
        if (pet->light_is_dark && now_ms >= pet->protection_until_ms) {
            enter_sleep(pet);
        }
    } else if (pet->state == PET_STATE_SLEEP && !pet->light_is_dark) {
        wake_up(pet, false, now_ms);
    }
}

pet_config_t pet_config_default(void)
{
    return (pet_config_t){
        .game_duration_ms = 15000,
        .settle_duration_ms = 3000,
        .triple_tap_window_ms = 800,
        .tap_max_ms = 250,
        .long_press_ms = 3000,
        .energy_decay_ms = 60000,
        .satiety_decay_ms = 300000,
        .sleep_gain_ms = 30000,
        .wake_protection_ms = 180000,
        /* 实机校准项：默认 25/45，先用占位值，按现场光照调整。 */
        .light_dark_threshold = 25,
        .light_bright_threshold = 45,
        .light_confirm_ms = 3000,
        .light_bright_is_high = true,
    };
}

void pet_init(pet_t *pet, const pet_config_t *cfg, pet_event_cb_t emit, void *emit_ctx,
              uint64_t now_ms)
{
    memset(pet, 0, sizeof(*pet));
    pet->cfg = cfg ? *cfg : pet_config_default();
    pet->emit = emit;
    pet->emit_ctx = emit_ctx;
    pet->state = PET_STATE_IDLE;
    pet->affection = PET_ATTR_INIT_AFFECTION;
    pet->energy = PET_ATTR_INIT_ENERGY;
    pet->satiety = PET_ATTR_INIT_SATIETY;
    pet->now_ms = now_ms;
    pet->protection_until_ms = now_ms;
}

void pet_tick(pet_t *pet, uint64_t now_ms)
{
    uint32_t dt = 0;
    if (now_ms >= pet->now_ms) {
        dt = (uint32_t)(now_ms - pet->now_ms);
    }
    pet->now_ms = now_ms;

    /* 长按抚摸：3s 到点立刻触发一次，同一次按压只触发一次。 */
    if (pet->pressing && pet->state == PET_STATE_IDLE && !pet->long_fired &&
        now_ms - pet->press_start_ms >= pet->cfg.long_press_ms) {
        pet->long_fired = true;
        const int before = pet->affection;
        add_attr(pet, &pet->affection, PET_LOVE_STEP);
        emit_event(pet, PET_EVT_LOVE, (int)pet->affection - before, 0);
    }

    switch (pet->state) {
    case PET_STATE_IDLE:
        if (pet->cfg.energy_decay_ms > 0) {
            pet->awake_ms += dt;
            while (pet->awake_ms >= pet->cfg.energy_decay_ms) {
                pet->awake_ms -= pet->cfg.energy_decay_ms;
                add_attr(pet, &pet->energy, -1);
            }
        }
        if (pet->light_known && pet->light_is_dark && now_ms >= pet->protection_until_ms) {
            enter_sleep(pet);
        }
        break;
    case PET_STATE_GAME:
        pet->game_elapsed_ms += dt;
        if (pet->game_elapsed_ms >= pet->cfg.game_duration_ms ||
            now_ms - pet->game_start_ms >= pet->cfg.game_duration_ms) {
            end_game(pet);
        }
        break;
    case PET_STATE_SETTLE:
        pet->settle_elapsed_ms += dt;
        if (pet->settle_elapsed_ms >= pet->cfg.settle_duration_ms) {
            finish_settle(pet);
        }
        break;
    case PET_STATE_SLEEP:
        if (pet->cfg.sleep_gain_ms > 0) {
            pet->sleep_ms += dt;
            while (pet->sleep_ms >= pet->cfg.sleep_gain_ms) {
                pet->sleep_ms -= pet->cfg.sleep_gain_ms;
                add_attr(pet, &pet->energy, 1);
            }
        }
        break;
    default:
        break;
    }

    /* 饱食度在所有运行状态累计消耗。 */
    if (pet->cfg.satiety_decay_ms > 0) {
        pet->satiety_ms += dt;
        while (pet->satiety_ms >= pet->cfg.satiety_decay_ms) {
            pet->satiety_ms -= pet->cfg.satiety_decay_ms;
            add_attr(pet, &pet->satiety, -1);
        }
    }
}

void pet_touch_press(pet_t *pet, uint64_t now_ms)
{
    /* 任何调用者在游戏/结算期间误调也要拦下，防止污染内部按压状态。 */
    if (pet->state == PET_STATE_GAME || pet->state == PET_STATE_SETTLE) {
        return;
    }
    pet->pressing = true;
    pet->long_fired = false;
    pet->press_start_ms = now_ms;
}

void pet_touch_release(pet_t *pet, uint64_t now_ms)
{
    const bool was_pressing = pet->pressing;
    const uint64_t duration = was_pressing && now_ms >= pet->press_start_ms
                                  ? now_ms - pet->press_start_ms
                                  : 0;
    pet->pressing = false;

    /* 游戏/结算期间禁用所有待机手势。 */
    if (pet->state == PET_STATE_GAME || pet->state == PET_STATE_SETTLE) {
        pet->taps = 0;
        return;
    }

    /* 长按/中按都不算 tap，并打断连击序列。 */
    if (!was_pressing || duration > pet->cfg.tap_max_ms) {
        pet->taps = 0;
        return;
    }

    if (pet->taps > 0 && now_ms - pet->first_tap_ms > pet->cfg.triple_tap_window_ms) {
        pet->taps = 0;
    }
    if (pet->taps == 0) {
        pet->first_tap_ms = now_ms;
    }
    pet->taps++;

    if (pet->state == PET_STATE_SLEEP) {
        /* 睡眠中三连击只唤醒，不进入游戏。 */
        if (pet->taps >= 3) {
            pet->taps = 0;
            wake_up(pet, true, now_ms);
        } else {
            emit_event(pet, PET_EVT_TAP_PROGRESS, pet->taps, 0);
        }
        return;
    }

    if (pet->taps >= 3) {
        pet->taps = 0;
        start_game(pet, now_ms);
    } else {
        emit_event(pet, PET_EVT_TAP_PROGRESS, pet->taps, 0);
    }
}

bool pet_feed(pet_t *pet)
{
    if (pet->state != PET_STATE_IDLE) {
        return false;
    }
    const int before = pet->satiety;
    add_attr(pet, &pet->satiety, PET_FEED_STEP);
    emit_event(pet, PET_EVT_FED, (int)pet->satiety - before, 0);
    return true;
}

bool pet_game_hit(pet_t *pet, uint64_t now_ms)
{
    if (pet->state != PET_STATE_GAME) {
        return false;
    }
    if (now_ms < pet->game_start_ms || now_ms - pet->game_start_ms >= pet->cfg.game_duration_ms) {
        return false;
    }
    pet->score++;
    emit_event(pet, PET_EVT_GAME_SCORE, (int)pet->score, 0);
    return true;
}

void pet_light_sample(pet_t *pet, int level, uint64_t now_ms)
{
    if (level < 0) {
        level = 0;
    } else if (level > 100) {
        level = 100;
    }
    const int bright = pet->cfg.light_bright_is_high ? level : 100 - level;
    const int bright_q8 = bright * 256;
    if (!pet->light_have_ema) {
        pet->light_ema_q8 = bright_q8;
        pet->light_have_ema = true;
    } else {
        /* 1/4 EMA，与驱动内部滤波叠加，进一步压掉抖动。 */
        pet->light_ema_q8 = (pet->light_ema_q8 * 3 + bright_q8 + 2) / 4;
    }
    const int filtered = (pet->light_ema_q8 + 128) / 256;

    bool candidate;
    if (filtered <= pet->cfg.light_dark_threshold) {
        candidate = true;
    } else if (filtered >= pet->cfg.light_bright_threshold) {
        candidate = false;
    } else {
        /* 迟滞区保持上一次已确认结果。 */
        candidate = pet->light_known ? pet->light_is_dark : false;
    }

    if (!pet->light_cand_valid || pet->light_cand_dark != candidate) {
        pet->light_cand_valid = true;
        pet->light_cand_dark = candidate;
        pet->light_cand_since_ms = now_ms;
    }

    if ((!pet->light_known || pet->light_is_dark != candidate) &&
        now_ms - pet->light_cand_since_ms >= pet->cfg.light_confirm_ms) {
        pet->light_is_dark = candidate;
        pet->light_known = true;
        emit_event(pet, PET_EVT_LIGHT, candidate ? 1 : 0, filtered);
        evaluate_light(pet, now_ms);
    }
}

pet_state_t pet_state(const pet_t *pet)
{
    return pet->state;
}

uint8_t pet_affection(const pet_t *pet)
{
    return pet->affection;
}

uint8_t pet_energy(const pet_t *pet)
{
    return pet->energy;
}

uint8_t pet_satiety(const pet_t *pet)
{
    return pet->satiety;
}

uint32_t pet_score(const pet_t *pet)
{
    return pet->score;
}

uint32_t pet_game_remaining_ms(const pet_t *pet)
{
    if (pet->state != PET_STATE_GAME) {
        return 0;
    }
    const uint64_t elapsed = pet->now_ms - pet->game_start_ms;
    if (elapsed >= pet->cfg.game_duration_ms) {
        return 0;
    }
    return pet->cfg.game_duration_ms - (uint32_t)elapsed;
}

bool pet_light_is_dark(const pet_t *pet)
{
    return pet->light_is_dark;
}

bool pet_light_is_known(const pet_t *pet)
{
    return pet->light_known;
}

int pet_light_level(const pet_t *pet)
{
    if (!pet->light_have_ema) {
        return 0;
    }
    return (pet->light_ema_q8 + 128) / 256;
}

bool pet_is_dirty(const pet_t *pet)
{
    return pet->dirty;
}

void pet_clear_dirty(pet_t *pet)
{
    pet->dirty = false;
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t pet_snapshot_write(const pet_t *pet, uint8_t *out, size_t cap)
{
    if (!out || cap < PET_SNAPSHOT_SIZE) {
        return 0;
    }
    out[0] = 'P';
    out[1] = 'T';
    out[2] = 'S';
    out[3] = '1';
    out[4] = PET_SNAPSHOT_VERSION;
    out[5] = pet->affection;
    out[6] = pet->energy;
    out[7] = pet->satiety;
    const uint16_t crc = crc16_ccitt(out, 8);
    out[8] = (uint8_t)(crc & 0xFF);
    out[9] = (uint8_t)(crc >> 8);
    return PET_SNAPSHOT_SIZE;
}

bool pet_snapshot_read(pet_t *pet, const uint8_t *data, size_t len)
{
    if (!data || len < PET_SNAPSHOT_SIZE) {
        return false;
    }
    if (memcmp(data, PET_SNAPSHOT_MAGIC, 4) != 0 || data[4] != PET_SNAPSHOT_VERSION) {
        return false;
    }
    const uint16_t crc = (uint16_t)data[8] | ((uint16_t)data[9] << 8);
    if (crc != crc16_ccitt(data, 8)) {
        return false;
    }
    pet->affection = clamp_attr(data[5]);
    pet->energy = clamp_attr(data[6]);
    pet->satiety = clamp_attr(data[7]);
    pet->dirty = false;
    return true;
}
