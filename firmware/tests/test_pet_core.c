#include "pet_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int feed_ok;
    int feed_blocked;
    int game_start;
    int game_won;
    int game_lost;
    int low_energy;
    int hug_done;
    int hug_cooldown;
    int sleep;
    int wake;
    int evolve;
    int idle;
} ev_counters_t;

static void on_event(pet_event_t ev, void *user)
{
    ev_counters_t *c = (ev_counters_t *)user;
    switch (ev) {
        case PET_EV_FEED_OK: c->feed_ok++; break;
        case PET_EV_FEED_BLOCKED: c->feed_blocked++; break;
        case PET_EV_GAME_START: c->game_start++; break;
        case PET_EV_GAME_WON: c->game_won++; break;
        case PET_EV_GAME_LOST: c->game_lost++; break;
        case PET_EV_LOW_ENERGY: c->low_energy++; break;
        case PET_EV_HUG_DONE: c->hug_done++; break;
        case PET_EV_HUG_COOLDOWN: c->hug_cooldown++; break;
        case PET_EV_SLEEP: c->sleep++; break;
        case PET_EV_WAKE: c->wake++; break;
        case PET_EV_EVOLVE: c->evolve++; break;
        case PET_EV_IDLE_REMINDER: c->idle++; break;
        default: break;
    }
}

static void tick_ms(pet_core_t *c, uint32_t *now, uint32_t ms)
{
    uint32_t end = *now + ms;
    while (*now < end) {
        uint32_t step = (end - *now) > 100 ? 100 : (end - *now);
        *now += step;
        pet_core_tick(c, *now);
    }
}

static void tap_left(pet_core_t *c, uint32_t *now)
{
    pet_core_pad_event(c, PET_PAD_LEFT, true, *now);
    tick_ms(c, now, 80);
    pet_core_pad_event(c, PET_PAD_LEFT, false, *now);
    tick_ms(c, now, 20);
}

static void tap_right(pet_core_t *c, uint32_t *now)
{
    pet_core_pad_event(c, PET_PAD_RIGHT, true, *now);
    tick_ms(c, now, 80);
    pet_core_pad_event(c, PET_PAD_RIGHT, false, *now);
    tick_ms(c, now, 20);
}

static void move_to_lane(pet_core_t *c, uint32_t *now, uint8_t target)
{
    while (c->state.game_lane < target) {
        pet_core_pad_event(c, PET_PAD_RIGHT, true, *now);
        tick_ms(c, now, 30);
        pet_core_pad_event(c, PET_PAD_RIGHT, false, *now);
        tick_ms(c, now, 50);
    }
    while (c->state.game_lane > target) {
        pet_core_pad_event(c, PET_PAD_LEFT, true, *now);
        tick_ms(c, now, 30);
        pet_core_pad_event(c, PET_PAD_LEFT, false, *now);
        tick_ms(c, now, 50);
    }
}

static void play_star_game(pet_core_t *c, uint32_t *now)
{
    assert(c->state.mode == PET_MODE_GAME);
    tick_ms(c, now, 300); /* input lock */
    while (c->state.mode == PET_MODE_GAME) {
        if (c->state.game_star_visible) {
            move_to_lane(c, now, (uint8_t)c->state.game_star_lane);
            /* let the star fall and register */
            tick_ms(c, now, 900);
        } else {
            tick_ms(c, now, 100);
        }
        if (c->state.game_elapsed_ms > PET_GAME_DURATION_MS + 1000) break;
    }
}

static bool wait_for_hug(pet_core_t *c, uint32_t *now, bool first_left, bool release)
{
    pet_pad_t first = first_left ? PET_PAD_LEFT : PET_PAD_RIGHT;
    pet_pad_t second = first_left ? PET_PAD_RIGHT : PET_PAD_LEFT;
    pet_core_pad_event(c, first, true, *now);
    tick_ms(c, now, 90);
    pet_core_pad_event(c, second, true, *now);
    tick_ms(c, now, 2100);
    bool is_hug = (c->state.mode == PET_MODE_HUG);
    if (release) {
        pet_core_pad_event(c, first, false, *now);
        pet_core_pad_event(c, second, false, *now);
        tick_ms(c, now, 50);
    }
    return is_hug;
}

int main(void)
{
    pet_core_t c;
    ev_counters_t ev = {0};
    uint32_t now = 1000;
    pet_core_init(&c, now);
    pet_core_set_callbacks(&c, on_event, &ev);

    assert(c.state.satiety == 60 && c.state.energy == 60 && c.state.intimacy == 60);
    assert(c.state.form == PET_FORM_BABY && c.state.needs_name);
    assert(pet_core_set_name(&c, "啵啵"));
    pet_core_complete_naming(&c, now);

    /* 奖励 = round(20 * stars / 6) */
    assert(pet_core_game_reward(0) == 0);
    assert(pet_core_game_reward(1) == 3);
    assert(pet_core_game_reward(2) == 7);
    assert(pet_core_game_reward(3) == 10);
    assert(pet_core_game_reward(4) == 13);
    assert(pet_core_game_reward(5) == 17);
    assert(pet_core_game_reward(6) == 20);

    /* 60 秒清醒消耗 */
    tick_ms(&c, &now, 60000);
    assert(c.state.satiety > 54.7f && c.state.satiety < 55.1f);
    assert(c.state.energy > 56.7f && c.state.energy < 57.1f);

    /* 单垫左键短触：喂食 */
    tap_left(&c, &now);
    assert(ev.feed_ok == 1);
    assert(c.state.satiety > 64.5f);
    assert(c.state.care_counts[PET_CARE_FEED] == 1);
    tick_ms(&c, &now, 1200);
    assert(c.state.mode == PET_MODE_IDLE);

    /* 喂食冷却 */
    tap_left(&c, &now);
    assert(ev.feed_ok == 1);
    tick_ms(&c, &now, 3200);
    tap_left(&c, &now);
    assert(ev.feed_ok == 2);
    tick_ms(&c, &now, 1200);

    /* 双垫抱抱：取消单垫动作，并在 2 秒后加亲密度 */
    float before_bond = c.state.intimacy;
    bool hugged = wait_for_hug(&c, &now, true, true);
    assert(hugged);
    assert(ev.feed_ok == 2); /* 抱抱没有喂食 */
    assert(c.state.intimacy > before_bond + 4.0f);
    assert(c.state.care_counts[PET_CARE_PET] == 1);
    assert(c.state.mode == PET_MODE_IDLE);

    /* 10 秒抱抱冷却：仍然可以抱，但不加数值 */
    tick_ms(&c, &now, 300);
    float cooled_bond = c.state.intimacy;
    bool hugged2 = wait_for_hug(&c, &now, false, true);
    assert(hugged2);
    assert(c.state.intimacy == cooled_bond);
    assert(ev.hug_cooldown >= 1);
    tick_ms(&c, &now, 10500);

    /* 右垫先按、左垫追随，也必须进入抱抱而不是开始游戏 */
    int game_starts = ev.game_start;
    bool hugged3 = wait_for_hug(&c, &now, false, true);
    assert(hugged3);
    assert(ev.game_start == game_starts);
    tick_ms(&c, &now, 300);

    /* 屏幕兜底抱抱 */
    tick_ms(&c, &now, 10500);
    assert(pet_core_hug_fallback(&c, now));
    tick_ms(&c, &now, 200);
    assert(c.state.mode == PET_MODE_HUG);
    tick_ms(&c, &now, 1000);
    assert(c.state.mode == PET_MODE_IDLE);

    /* 右垫开始游戏，接 6 颗星：开局 -10，满星 +20，净 +10 */
    float before_energy = c.state.energy;
    tap_right(&c, &now);
    assert(ev.game_start == 1);
    assert(c.state.mode == PET_MODE_GAME);
    assert(c.state.energy < before_energy - 9.0f);
    play_star_game(&c, &now);
    assert(c.state.mode == PET_MODE_RESULT);
    assert(c.state.result == PET_RESULT_SUCCESS);
    assert(c.state.game_stars == 6);
    assert(ev.game_won == 1);
    assert(c.state.care_counts[PET_CARE_EXERCISE] == 1);
    assert(c.state.game_reward == 20);
    assert(c.state.game_net == 10);
    assert(c.state.energy > before_energy + 8.0f);

    /* 结算页右垫重试 */
    tick_ms(&c, &now, 300);
    pet_core_pad_event(&c, PET_PAD_RIGHT, true, now);
    tick_ms(&c, &now, 60);
    pet_core_pad_event(&c, PET_PAD_RIGHT, false, now);
    tick_ms(&c, &now, 30);
    assert(c.state.mode == PET_MODE_GAME);
    pet_core_abort_game(&c, now);
    tick_ms(&c, &now, 300);
    assert(c.state.mode == PET_MODE_IDLE);

    /* 游戏内双垫忽略，不触发抱抱 */
    tap_right(&c, &now);
    assert(c.state.mode == PET_MODE_GAME);
    float game_bond = c.state.intimacy;
    pet_core_pad_event(&c, PET_PAD_LEFT, true, now);
    pet_core_pad_event(&c, PET_PAD_RIGHT, true, now);
    tick_ms(&c, &now, 2200);
    assert(c.state.mode == PET_MODE_GAME);
    assert(c.state.intimacy == game_bond);
    pet_core_pad_event(&c, PET_PAD_LEFT, false, now);
    pet_core_pad_event(&c, PET_PAD_RIGHT, false, now);
    pet_core_abort_game(&c, now);
    tick_ms(&c, &now, 300);

    /* 光敏：亮=醒，暗=睡（800ms 去抖） */
    pet_core_update_light(&c, 100.0f, now);
    tick_ms(&c, &now, 300);
    pet_core_update_light(&c, 0.0f, now);
    tick_ms(&c, &now, 900);
    pet_core_update_light(&c, 0.0f, now);
    assert(c.state.mode == PET_MODE_SLEEP);
    assert(c.state.sleeping);
    assert(ev.sleep == 1);

    /* 睡眠属性恢复 */
    float sleep_energy = c.state.energy;
    for (int i = 0; i < 60; i++) {
        tick_ms(&c, &now, 1000);
        pet_core_update_light(&c, 20.0f, now);
    }
    assert(c.state.energy > sleep_energy + 10.0f);

    /* 光敏变亮 → 自动醒 */
    pet_core_update_light(&c, 100.0f, now);
    tick_ms(&c, &now, 1000);
    pet_core_update_light(&c, 100.0f, now);
    assert(!c.state.sleeping);
    assert(c.state.mode == PET_MODE_IDLE);
    assert(ev.wake >= 1);

    /* 手动覆盖：亮环境手动睡，光敏不应把它叫醒 */
    assert(pet_core_manual_sleep(&c, now));
    assert(c.state.sleeping);
    pet_core_update_light(&c, 100.0f, now);
    tick_ms(&c, &now, 1000);
    pet_core_update_light(&c, 100.0f, now);
    assert(c.state.sleeping);

    /* 环境变暗：覆盖结束，保持睡眠 */
    pet_core_update_light(&c, 0.0f, now);
    tick_ms(&c, &now, 1000);
    pet_core_update_light(&c, 0.0f, now);
    assert(c.state.sleeping);

    /* 暗环境手动叫醒，光敏不应把它睡回去 */
    assert(pet_core_manual_wake(&c, now));
    assert(!c.state.sleeping);
    pet_core_update_light(&c, 0.0f, now);
    tick_ms(&c, &now, 1000);
    pet_core_update_light(&c, 0.0f, now);
    assert(!c.state.sleeping);

    /* 环境变亮：覆盖结束，保持醒着 */
    pet_core_update_light(&c, 100.0f, now);
    tick_ms(&c, &now, 1000);
    pet_core_update_light(&c, 100.0f, now);
    assert(!c.state.sleeping);

    /* 醒来后等待状态清空，再验证左垫喂食仍然可用 */
    int feed_before = ev.feed_ok;
    tick_ms(&c, &now, 300);
    tap_left(&c, &now);
    assert(ev.feed_ok == feed_before + 1);
    tick_ms(&c, &now, 1200);

    /* 0 星：开局 -10，奖励 0，净 -10 */
    float fail_energy = c.state.energy;
    int exercise_before = c.state.care_counts[PET_CARE_EXERCISE];
    tap_right(&c, &now);
    assert(c.state.mode == PET_MODE_GAME);
    tick_ms(&c, &now, 10500);
    assert(c.state.mode == PET_MODE_RESULT);
    assert(c.state.result == PET_RESULT_FAIL);
    assert(c.state.game_stars == 0);
    assert(c.state.game_reward == 0);
    assert(c.state.game_net == -10);
    assert(c.state.care_counts[PET_CARE_EXERCISE] == exercise_before);
    assert(c.state.energy < fail_energy - 8.0f);
    pet_core_exit_result(&c, now);
    tick_ms(&c, &now, 300);

    /* 精力不足 10 不能开局 */
    c.state.mode = PET_MODE_IDLE;
    c.state.sleeping = false;
    c.state.energy = 5.0f;
    int low_before = ev.low_energy;
    assert(!pet_core_start_game(&c, now));
    assert(ev.low_energy == low_before + 1);
    c.state.energy = 60.0f;

    /* 60 秒无操作提醒 */
    int idle_before = ev.idle;
    tick_ms(&c, &now, 61000);
    assert(ev.idle >= idle_before + 1);

    /* 成长：直接构造 10 次喂食，验证清空与形态 */
    memset(c.state.care_counts, 0, sizeof(c.state.care_counts));
    memset(c.state.last_care_order, 0, sizeof(c.state.last_care_order));
    c.state.care_seq = 0;
    c.state.satiety = 20;
    c.state.last_feed_ms = 0;
    for (int i = 0; i < 10; i++) {
        c.state.satiety = 20;
        pet_core_feed(&c, now);
        tick_ms(&c, &now, 3100);
    }
    assert(c.state.form == PET_FORM_ROUND);
    assert(c.state.unlocked[PET_FORM_ROUND]);
    assert(pet_core_round_total(&c) == 0);
    assert(c.state.mode == PET_MODE_EVOLVE);
    pet_core_ack_evolution(&c, now);
    tick_ms(&c, &now, 300);
    assert(c.state.mode == PET_MODE_IDLE);

    printf("PASS: pet_core v0.3 tests\n");
    printf("  feeds=%d hugs=%d game_won=%d game_lost=%d sleep=%d wake=%d evolve=%d idle=%d\n",
           ev.feed_ok, ev.hug_done, ev.game_won, ev.game_lost, ev.sleep, ev.wake, ev.evolve, ev.idle);
    return 0;
}
