/**
 * pet_core.c — 星芽小屋 v0.3 核心状态机
 *
 * 纯逻辑组件：不依赖 ESP-IDF，便于在主机上测试。
 * 负责：
 *   - 属性/昼夜
 *   - 触摸垫单键/双键仲裁
 *   - 三轨道接星星小游戏
 *   - 喂食、抱抱、睡眠、成长
 *   - 60 秒陪伴提醒
 */

#include "pet_core.h"

#include <string.h>
#include <stdio.h>

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


static void emit(pet_core_t *core, pet_event_t event)
{
    if (core && core->on_event) {
        core->on_event(event, core->user_data);
    }
}

static uint32_t rng_next(pet_core_t *core)
{
    /* 简单的 32 位 LCG，只用于星星轨道选择。 */
    uint32_t x = core->state.rng_state;
    if (x == 0) x = 0x1A2B3C4D;
    x = x * 1664525u + 1013904223u;
    core->state.rng_state = x;
    return x;
}

static uint8_t random_lane_except(pet_core_t *core, uint8_t except)
{
    /* 0..2 中选一个不等于 except 的轨道，保证需要移动才接得到。 */
    uint8_t candidates[PET_GAME_LANES - 1];
    uint8_t n = 0;
    for (uint8_t i = 0; i < PET_GAME_LANES; i++) {
        if (i != except) candidates[n++] = i;
    }
    return candidates[rng_next(core) % n];
}

static void clear_pad_gesture(pet_core_t *core)
{
    core->gesture_active = false;
    core->gesture_both = false;
    core->gesture_second_seen = false;
    core->gesture_first_pad = 0;
    core->gesture_start_ms = 0;
    core->hug_start_ms = 0;
    core->hug_rewarded = false;
}

static void clear_game(pet_core_t *core)
{
    core->state.game_star_visible = false;
    core->state.game_star_lane = -1;
    core->state.game_stars = 0;
    core->state.game_elapsed_ms = 0;
    core->state.game_lane = 1;
    core->state.game_both_pressed = false;
    core->game_star_timer_ms = 0;
    core->game_star_gap_ms = 0;
    core->game_input_lock_until_ms = 0;
    core->state.result = PET_RESULT_NONE;
}

static void enter_result(pet_core_t *core, pet_result_t result, uint32_t now_ms)
{
    core->state.result = result;
    core->state.mode = PET_MODE_RESULT;
    core->state.mode_until_ms = 0;
    core->result_shown_ms = now_ms;
    core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
    core->state.game_star_visible = false;
    core->state.game_both_pressed = false;
    clear_pad_gesture(core);
    core->state.last_interaction_ms = now_ms;
}

static void spawn_star(pet_core_t *core)
{
    core->state.game_star_lane = (int8_t)random_lane_except(core, core->state.game_lane);
    core->state.game_star_visible = true;
    core->game_star_timer_ms = PET_GAME_STAR_FALL_MS;
    core->game_star_gap_ms = 0;
}

static void record_care(pet_core_t *core, pet_care_t care, uint32_t now_ms)
{
    pet_state_t *s = &core->state;
    if (care >= PET_CARE_MAX) return;
    if (s->care_counts[care] < UINT16_MAX) s->care_counts[care]++;
    s->care_seq++;
    s->last_care_order[care] = s->care_seq;
    s->last_interaction_ms = now_ms;

    if (pet_core_round_total(core) < PET_CARES_PER_FORM) return;

    /* 次数最多；并列时取最近发生的类型。 */
    uint16_t max_count = 0;
    for (int i = 0; i < PET_CARE_MAX; i++) {
        if (s->care_counts[i] > max_count) max_count = s->care_counts[i];
    }

    int winner = -1;
    uint32_t best_order = 0;
    for (int i = 0; i < PET_CARE_MAX; i++) {
        if (s->care_counts[i] != max_count) continue;
        if (s->last_care_order[i] >= best_order) {
            best_order = s->last_care_order[i];
            winner = i;
        }
    }
    if (winner < 0) winner = care;

    pet_form_t next = pet_core_form_from_care((pet_care_t)winner);
    s->unlocked[next] = true;
    s->form = next;

    memset(s->care_counts, 0, sizeof(s->care_counts));
    memset(s->last_care_order, 0, sizeof(s->last_care_order));
    s->care_seq = 0;

    if (s->mode == PET_MODE_GAME || s->mode == PET_MODE_GAME_PAUSED) {
        clear_game(core);
        s->result = PET_RESULT_NONE;
    }
    s->mode = PET_MODE_EVOLVE;
    s->mode_until_ms = 0;
    clear_pad_gesture(core);
    core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
    emit(core, PET_EV_EVOLVE);
}



void pet_core_reset_fresh(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    memset(core, 0, sizeof(*core));
    pet_state_t *s = &core->state;
    s->satiety = PET_STAT_INITIAL;
    s->energy = PET_STAT_INITIAL;
    s->intimacy = PET_STAT_INITIAL;
    s->sleeping = false;
    s->name[0] = '\0';
    s->needs_name = true;
    s->muted = false;
    s->form = PET_FORM_BABY;
    s->unlocked[PET_FORM_BABY] = true;
    s->cycle_ms = 0;
    s->last_tick_ms = now_ms;
    s->last_interaction_ms = now_ms;
    s->last_reminder_ms = now_ms;
    s->mode = PET_MODE_IDLE;
    s->result = PET_RESULT_NONE;
    s->game_lane = 1;
    s->game_star_lane = -1;
    s->rng_state = now_ms ^ 0x9E3779B9u;
    core->result_shown_ms = now_ms;
    core->post_action_neutral_until_ms = now_ms;
    core->light_state_since_ms = now_ms;
    core->light_bright = true;
    core->manual_override = false;
}

void pet_core_init(pet_core_t *core, uint32_t now_ms)
{
    pet_core_reset_fresh(core, now_ms);
}

void pet_core_set_callbacks(pet_core_t *core, pet_event_cb_t cb, void *user_data)
{
    if (!core) return;
    core->on_event = cb;
    core->user_data = user_data;
}

void pet_core_note_screen_touch(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    core->state.last_interaction_ms = now_ms;
}

uint32_t pet_core_game_star_time_left(const pet_core_t *core)
{
    if (!core || !core->state.game_star_visible) return 0;
    return core->game_star_timer_ms;
}

int pet_core_round_total(const pet_core_t *core)
{
    if (!core) return 0;
    int total = 0;
    for (int i = 0; i < PET_CARE_MAX; i++) total += core->state.care_counts[i];
    return total;
}

int pet_core_round_remaining(const pet_core_t *core)
{
    return PET_CARES_PER_FORM - pet_core_round_total(core);
}

pet_form_t pet_core_form_from_care(pet_care_t care)
{
    switch (care) {
        case PET_CARE_EXERCISE: return PET_FORM_ACTIVE;
        case PET_CARE_FEED:     return PET_FORM_ROUND;
        case PET_CARE_PET:      return PET_FORM_CLINGY;
        default:                return PET_FORM_BABY;
    }
}

const char *pet_core_form_name(pet_form_t form)
{
    switch (form) {
        case PET_FORM_ACTIVE: return "活力型 · 跃跃";
        case PET_FORM_ROUND:  return "圆滚型 · 团团";
        case PET_FORM_CLINGY: return "黏人型 · 贴贴";
        default:              return "星芽宝宝";
    }
}

const char *pet_core_care_name(pet_care_t care)
{
    switch (care) {
        case PET_CARE_EXERCISE: return "运动";
        case PET_CARE_FEED:     return "喂食";
        case PET_CARE_PET:      return "摸摸";
        default:                return "照顾";
    }
}

bool pet_core_set_name(pet_core_t *core, const char *name)
{
    if (!core || !name) return false;
    while (*name == ' ' || *name == '\t') name++;
    size_t n = strlen(name);
    if (n == 0) return false;
    if (n >= sizeof(core->state.name)) n = sizeof(core->state.name) - 1;
    memcpy(core->state.name, name, n);
    core->state.name[n] = '\0';
    return true;
}

void pet_core_complete_naming(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    if (core->state.name[0] == '\0') {
        pet_core_set_name(core, "啵啵");
    }
    core->state.needs_name = false;
    core->state.last_interaction_ms = now_ms;
}

void pet_core_set_muted(pet_core_t *core, bool muted)
{
    if (core) core->state.muted = muted;
}

bool pet_core_feed(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return false;
    pet_state_t *s = &core->state;

    if (s->sleeping) return false;
    if (s->mode != PET_MODE_IDLE && s->mode != PET_MODE_GAME_PAUSED) return false;

    if (s->satiety >= PET_FEED_FULL_AT) {
        emit(core, PET_EV_FEED_BLOCKED);
        return false;
    }
    if (now_ms - s->last_feed_ms < PET_FEED_COOLDOWN_MS) {
        emit(core, PET_EV_FEED_COOLDOWN);
        return false;
    }

    float before = s->satiety;
    s->satiety = clampf(s->satiety + PET_FEED_AMOUNT, PET_STAT_MIN, PET_STAT_MAX);
    if (s->satiety <= before) {
        emit(core, PET_EV_FEED_BLOCKED);
        return false;
    }

    s->last_feed_ms = now_ms;
    s->last_interaction_ms = now_ms;
    if (s->mode == PET_MODE_IDLE) {
        s->mode = PET_MODE_EATING;
        s->mode_until_ms = now_ms + 1100;
    }
    record_care(core, PET_CARE_FEED, now_ms);
    emit(core, PET_EV_FEED_OK);
    return true;
}

bool pet_core_hug_fallback(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return false;
    pet_state_t *s = &core->state;
    if (s->sleeping || s->mode != PET_MODE_IDLE) return false;
    if (now_ms - s->last_hug_ms < PET_HUG_COOLDOWN_MS) {
        emit(core, PET_EV_HUG_COOLDOWN);
        return false;
    }
    s->last_hug_ms = now_ms;
    s->intimacy = clampf(s->intimacy + PET_HUG_BOND, PET_STAT_MIN, PET_STAT_MAX);
    s->mode = PET_MODE_HUG;
    s->mode_until_ms = now_ms + 900;
    s->last_interaction_ms = now_ms;
    record_care(core, PET_CARE_PET, now_ms);
    if (s->mode != PET_MODE_EVOLVE) emit(core, PET_EV_HUG_DONE);
    return true;
}

static void move_lane(pet_core_t *core, pet_pad_t pad, uint32_t now_ms)
{
    pet_state_t *s = &core->state;
    if (now_ms < core->game_input_lock_until_ms) return;

    uint8_t before = s->game_lane;
    if (pad == PET_PAD_LEFT && s->game_lane > 0) s->game_lane--;
    if (pad == PET_PAD_RIGHT && s->game_lane < PET_GAME_LANES - 1) s->game_lane++;
    if (s->game_lane != before) {
        s->last_interaction_ms = now_ms;
        emit(core, PET_EV_GAME_MOVE);
    }
}

void pet_core_pad_event(pet_core_t *core, pet_pad_t pad, bool down, uint32_t now_ms)
{
    if (!core || pad >= PET_PAD_COUNT) return;
    pet_state_t *s = &core->state;
    if (s->pad_down[pad] == down) return;

    s->pad_down[pad] = down;
    if (down) s->last_interaction_ms = now_ms;

    if (down) {
        if (s->sleeping) return;

        if (s->mode == PET_MODE_GAME) {
            pet_pad_t other = (pad == PET_PAD_LEFT) ? PET_PAD_RIGHT : PET_PAD_LEFT;
            /* 只忽略“另一块垫当前也按着”的这一次边沿，不做永久 latch，
             * 否则一块垫被读成常按会让整局都动不了。 */
            s->game_both_pressed = s->pad_down[other];
            if (!s->pad_down[other]) move_lane(core, pad, now_ms);
            return;
        }

        if (s->mode == PET_MODE_RESULT) {
            if (pad == PET_PAD_RIGHT &&
                now_ms >= core->post_action_neutral_until_ms &&
                !core->wait_release_all) {
                core->wait_release_all = true;
                core->result_right_pending = true;
            }
            return;
        }

        if (s->mode != PET_MODE_IDLE) return;
        if (now_ms < core->post_action_neutral_until_ms) return;
        if (core->wait_release_all) return;

        if (core->gesture_active) {
            if (core->gesture_both) return;
            if (now_ms - core->gesture_start_ms <= PET_PAD_ARBITRATION_MS) {
                core->gesture_both = true;
                core->gesture_second_seen = true;
                core->hug_start_ms = now_ms;
                core->hug_rewarded = false;
            } else {
                /* 第一块垫已经按太久，不把第二块当成同一次抱抱。 */
                core->wait_release_all = true;
            }
            return;
        }

        core->gesture_active = true;
        core->gesture_both = false;
        core->gesture_second_seen = false;
        core->gesture_first_pad = (uint8_t)pad;
        core->gesture_start_ms = now_ms;
        core->hug_start_ms = 0;
        core->hug_rewarded = false;
        return;
    }

    /* ---- 松开 ---- */
    if (s->mode == PET_MODE_GAME) {
        s->game_both_pressed = s->pad_down[PET_PAD_LEFT] || s->pad_down[PET_PAD_RIGHT];
        return;
    }

    if (s->mode == PET_MODE_HUG) {
        if (!s->pad_down[PET_PAD_LEFT] && !s->pad_down[PET_PAD_RIGHT]) {
            s->mode = PET_MODE_IDLE;
            clear_pad_gesture(core);
            core->wait_release_all = false;
            core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
        }
        return;
    }

    if (s->mode == PET_MODE_RESULT) {
        if (!s->pad_down[PET_PAD_LEFT] && !s->pad_down[PET_PAD_RIGHT]) {
            core->wait_release_all = false;
            if (core->result_right_pending) {
                core->result_right_pending = false;
                pet_core_retry_game(core, now_ms);
            }
        }
        return;
    }

    if (s->mode == PET_MODE_IDLE && core->gesture_active) {
        if (core->gesture_both) {
            if (s->pad_down[PET_PAD_LEFT] || s->pad_down[PET_PAD_RIGHT]) {
                /* 双垫抱抱还没按满就松开了一块：取消，等全部松开。 */
                if (s->mode == PET_MODE_IDLE) {
                    clear_pad_gesture(core);
                    core->wait_release_all = true;
                    core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
                }
                return;
            }
            /* 两垫都松开：2 秒未到或已抱过，结束。 */
            if (!core->hug_rewarded) {
                emit(core, PET_EV_PAD_HINT);
            }
            clear_pad_gesture(core);
            core->wait_release_all = false;
            core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
            return;
        }

        if ((uint8_t)pad != core->gesture_first_pad) {
            clear_pad_gesture(core);
            core->wait_release_all = s->pad_down[PET_PAD_LEFT] || s->pad_down[PET_PAD_RIGHT];
            core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
            return;
        }

        uint32_t held = now_ms - core->gesture_start_ms;
        uint8_t first = core->gesture_first_pad;
        bool second_seen = core->gesture_second_seen;
        clear_pad_gesture(core);
        core->wait_release_all = false;
        core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;

        if (held <= PET_PAD_TAP_MAX_MS && !second_seen) {
            if (first == PET_PAD_LEFT) {
                pet_core_feed(core, now_ms);
            } else {
                pet_core_start_game(core, now_ms);
            }
        } else if (!second_seen) {
            emit(core, PET_EV_PAD_HINT);
        }
        return;
    }

    if (!s->pad_down[PET_PAD_LEFT] && !s->pad_down[PET_PAD_RIGHT]) {
        core->wait_release_all = false;
        if (s->mode == PET_MODE_IDLE) {
            core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
        }
    }
}

void pet_core_tick(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    pet_state_t *s = &core->state;

    if (s->last_tick_ms == 0) s->last_tick_ms = now_ms;
    uint32_t elapsed = now_ms - s->last_tick_ms;
    s->last_tick_ms = now_ms;
    if (elapsed > 1000) elapsed = 1000; /* 不做断电/后台追扣 */

    if (elapsed > 0) {
        float minutes = (float)elapsed / 60000.0f;
        if (s->sleeping) {
            s->satiety = clampf(s->satiety - PET_SLEEP_SATIETY_PER_MIN * minutes, PET_STAT_MIN, PET_STAT_MAX);
            s->energy = clampf(s->energy + PET_SLEEP_ENERGY_PER_MIN * minutes, PET_STAT_MIN, PET_STAT_MAX);
        } else {
            s->satiety = clampf(s->satiety - PET_AWAKE_SATIETY_PER_MIN * minutes, PET_STAT_MIN, PET_STAT_MAX);
            s->energy = clampf(s->energy - PET_AWAKE_ENERGY_PER_MIN * minutes, PET_STAT_MIN, PET_STAT_MAX);
        }

        if (s->mode == PET_MODE_GAME) {
            s->game_elapsed_ms += elapsed;

            if (s->game_star_visible) {
                if (core->game_star_timer_ms > elapsed) {
                    core->game_star_timer_ms -= elapsed;
                } else {
                    bool caught = (s->game_star_lane == (int8_t)s->game_lane);
                    s->game_star_visible = false;
                    s->game_star_lane = -1;
                    core->game_star_timer_ms = 0;
                    if (caught) {
                        if (s->game_stars < UINT8_MAX) s->game_stars++;
                        emit(core, PET_EV_GAME_STAR);
                        if (s->game_stars >= PET_GAME_TARGET_STARS) {
                            const int reward = pet_core_game_reward(s->game_stars);
                            s->game_reward = reward;
                            s->game_net = reward - (int)PET_GAME_ENERGY_COST;
                            s->energy = clampf(s->energy + (float)reward, PET_STAT_MIN, PET_STAT_MAX);
                            s->game_star_visible = false;
                            s->result = PET_RESULT_SUCCESS;
                            record_care(core, PET_CARE_EXERCISE, now_ms);
                            if (s->mode != PET_MODE_EVOLVE) {
                                enter_result(core, PET_RESULT_SUCCESS, now_ms);
                                emit(core, PET_EV_GAME_WON);
                            }
                            return;
                        }
                    } else {
                        emit(core, PET_EV_GAME_MISS);
                    }
                    core->game_star_gap_ms = PET_GAME_STAR_GAP_MS;
                }
            } else if (core->game_star_gap_ms > 0) {
                if (core->game_star_gap_ms > elapsed) {
                    core->game_star_gap_ms -= elapsed;
                } else {
                    core->game_star_gap_ms = 0;
                    spawn_star(core);
                }
            } else {
                spawn_star(core);
            }

            if (s->mode == PET_MODE_GAME && s->game_elapsed_ms >= PET_GAME_DURATION_MS) {
                const int reward = pet_core_game_reward(s->game_stars);
                s->game_reward = reward;
                s->game_net = reward - (int)PET_GAME_ENERGY_COST;
                s->energy = clampf(s->energy + (float)reward, PET_STAT_MIN, PET_STAT_MAX);
                enter_result(core, PET_RESULT_FAIL, now_ms);
                emit(core, PET_EV_GAME_LOST);
                return;
            }
        }

        if (s->mode == PET_MODE_EATING && now_ms >= s->mode_until_ms) {
            s->mode = PET_MODE_IDLE;
            s->mode_until_ms = 0;
        }
        if (s->mode == PET_MODE_HUG && s->mode_until_ms != 0 && now_ms >= s->mode_until_ms) {
            s->mode = PET_MODE_IDLE;
            s->mode_until_ms = 0;
            clear_pad_gesture(core);
            core->wait_release_all = s->pad_down[PET_PAD_LEFT] || s->pad_down[PET_PAD_RIGHT];
            core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
        }
    }

    if (s->mode == PET_MODE_IDLE && core->gesture_active && core->gesture_both &&
        s->pad_down[PET_PAD_LEFT] && s->pad_down[PET_PAD_RIGHT]) {
        if (!core->hug_rewarded &&
            now_ms - core->hug_start_ms >= PET_HUG_HOLD_MS) {
            core->hug_rewarded = true;
            s->mode = PET_MODE_HUG;
            if (now_ms - s->last_hug_ms >= PET_HUG_COOLDOWN_MS) {
                s->last_hug_ms = now_ms;
                s->intimacy = clampf(s->intimacy + PET_HUG_BOND, PET_STAT_MIN, PET_STAT_MAX);
                record_care(core, PET_CARE_PET, now_ms);
                if (s->mode != PET_MODE_EVOLVE) emit(core, PET_EV_HUG_DONE);
            } else {
                emit(core, PET_EV_HUG_COOLDOWN);
            }
        }
    }

    if (!s->sleeping && s->mode == PET_MODE_IDLE && !core->gesture_active &&
        now_ms - s->last_interaction_ms >= PET_IDLE_REMINDER_MS &&
        now_ms - s->last_reminder_ms >= PET_IDLE_REMINDER_MS) {
        s->last_reminder_ms = now_ms;
        emit(core, PET_EV_IDLE_REMINDER);
    }
}

int pet_core_game_reward(int stars)
{
    if (stars < 0) stars = 0;
    if (stars > PET_GAME_TARGET_STARS) stars = PET_GAME_TARGET_STARS;
    return (PET_GAME_REWARD_MAX * stars + PET_GAME_TARGET_STARS / 2) / PET_GAME_TARGET_STARS;
}

bool pet_core_start_game(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return false;
    pet_state_t *s = &core->state;
    if (s->sleeping || s->mode != PET_MODE_IDLE) return false;
    if (s->energy < PET_GAME_ENERGY_COST) {
        emit(core, PET_EV_LOW_ENERGY);
        return false;
    }
    /* 每局先扣 10，结算再按星数加奖励 */
    s->energy = clampf(s->energy - PET_GAME_ENERGY_COST, PET_STAT_MIN, PET_STAT_MAX);
    s->mode = PET_MODE_GAME;
    s->result = PET_RESULT_NONE;
    s->game_lane = 1;
    s->game_stars = 0;
    s->game_elapsed_ms = 0;
    s->game_star_visible = false;
    s->game_star_lane = -1;
    s->game_both_pressed = false;
    s->game_reward = 0;
    s->game_net = -(int)PET_GAME_ENERGY_COST;
    core->game_star_gap_ms = 0;
    core->game_star_timer_ms = 0;
    core->game_input_lock_until_ms = now_ms + PET_GAME_INPUT_LOCK_MS;
    /* 单垫启动后不要被上一动作的 250ms 中性期拖慢 */
    core->post_action_neutral_until_ms = now_ms;
    core->wait_release_all = false;
    s->last_interaction_ms = now_ms;
    spawn_star(core);
    emit(core, PET_EV_GAME_START);
    return true;
}

void pet_core_pause_game(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    if (core->state.mode != PET_MODE_GAME) return;
    core->state.mode = PET_MODE_GAME_PAUSED;
    core->state.game_both_pressed = false;
    core->state.last_interaction_ms = now_ms;
    emit(core, PET_EV_GAME_PAUSED);
}

void pet_core_resume_game(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    if (core->state.mode != PET_MODE_GAME_PAUSED) return;
    core->state.mode = PET_MODE_GAME;
    core->game_input_lock_until_ms = now_ms + 300;
    core->state.last_interaction_ms = now_ms;
    emit(core, PET_EV_GAME_RESUMED);
}

void pet_core_abort_game(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    if (core->state.mode != PET_MODE_GAME && core->state.mode != PET_MODE_GAME_PAUSED) return;
    clear_game(core);
    core->state.mode = PET_MODE_IDLE;
    core->state.result = PET_RESULT_NONE;
    core->state.last_interaction_ms = now_ms;
    core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
    emit(core, PET_EV_GAME_ABORTED);
}

void pet_core_retry_game(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    if (core->state.mode != PET_MODE_RESULT) return;
    if (core->state.energy < PET_GAME_ENERGY_COST) {
        emit(core, PET_EV_LOW_ENERGY);
        return;
    }
    core->state.mode = PET_MODE_IDLE;
    core->state.result = PET_RESULT_NONE;
    pet_core_start_game(core, now_ms);
}

void pet_core_exit_result(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    if (core->state.mode != PET_MODE_RESULT) return;
    clear_game(core);
    core->state.mode = PET_MODE_IDLE;
    core->state.result = PET_RESULT_NONE;
    core->state.last_interaction_ms = now_ms;
    core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
}

bool pet_core_sleep(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return false;
    pet_state_t *s = &core->state;
    if (s->sleeping) return true;
    if (s->mode != PET_MODE_IDLE) return false;
    clear_pad_gesture(core);
    s->mode = PET_MODE_SLEEP;
    s->sleeping = true;
    s->last_interaction_ms = now_ms;
    core->wait_release_all = s->pad_down[PET_PAD_LEFT] || s->pad_down[PET_PAD_RIGHT];
    core->light_state_since_ms = now_ms;
    emit(core, PET_EV_SLEEP);
    return true;
}

bool pet_core_wake(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return false;
    pet_state_t *s = &core->state;
    if (!s->sleeping) return false;
    s->sleeping = false;
    s->mode = PET_MODE_IDLE;
    s->last_interaction_ms = now_ms;
    core->wait_release_all = s->pad_down[PET_PAD_LEFT] || s->pad_down[PET_PAD_RIGHT];
    core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
    core->light_state_since_ms = now_ms;
    emit(core, PET_EV_WAKE);
    return true;
}

void pet_core_update_light(pet_core_t *core, float light_pct, uint32_t now_ms)
{
    if (!core) return;
    const bool bright = light_pct >= 50.0f;

    if (!core->light_valid) {
        core->light_valid = true;
        core->light_bright = bright;
        core->light_state_since_ms = now_ms;
        core->manual_override = false;
        /* 首帧立即对齐环境状态 */
        if (bright) {
            if (core->state.sleeping) pet_core_wake(core, now_ms);
        } else if (!core->state.sleeping) {
            pet_core_sleep(core, now_ms);
        }
        return;
    }

    /* 环境亮暗发生一次变化：手动覆盖结束，交还自动逻辑 */
    if (bright != core->light_bright) {
        core->light_bright = bright;
        core->manual_override = false;
        core->light_state_since_ms = now_ms;
    }

    /* 手动睡眠/叫醒期间，不被光敏自动状态改回 */
    if (core->manual_override) {
        return;
    }

    if (bright == !core->state.sleeping) {
        core->light_state_since_ms = now_ms;
        return;
    }

    if (now_ms - core->light_state_since_ms >= PET_LIGHT_SETTLE_MS) {
        core->light_state_since_ms = now_ms;
        if (bright) pet_core_wake(core, now_ms);
        else pet_core_sleep(core, now_ms);
    }
}

bool pet_core_manual_sleep(pet_core_t *core, uint32_t now_ms)
{
    if (!core || core->state.sleeping) return false;
    if (!pet_core_sleep(core, now_ms)) return false;
    core->manual_override = true;
    return true;
}

bool pet_core_manual_wake(pet_core_t *core, uint32_t now_ms)
{
    if (!core || !core->state.sleeping) return false;
    if (!pet_core_wake(core, now_ms)) return false;
    core->manual_override = true;
    return true;
}

void pet_core_ack_evolution(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return;
    if (core->state.mode != PET_MODE_EVOLVE) return;
    core->state.mode = PET_MODE_IDLE;
    core->state.last_interaction_ms = now_ms;
    core->post_action_neutral_until_ms = now_ms + PET_POST_ACTION_NEUTRAL_MS;
    clear_pad_gesture(core);
    core->wait_release_all = core->state.pad_down[PET_PAD_LEFT] || core->state.pad_down[PET_PAD_RIGHT];
}
