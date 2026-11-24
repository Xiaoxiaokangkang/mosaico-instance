/*
 * 宿主侧单元测试：只依赖 pet_core.c，不依赖 ESP-IDF。
 * 运行：cc -std=c11 -Wall -Wextra -I ../components/pet_core/include \
 *          ../components/pet_core/pet_core.c test_pet_core.c -o /tmp/pet_test && /tmp/pet_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet_core.h"

static pet_t s_pet;
static int s_state_events;
static int s_love_events;
static int s_fed_events;
static int s_game_end_events;
static int s_wake_events;
static int s_sleep_events;
static int s_game_end_gain;
static pet_state_t s_last_state;

static void on_event(void *ctx, const pet_event_t *event)
{
    (void)ctx;
    switch (event->type) {
    case PET_EVT_STATE:
        s_state_events++;
        s_last_state = (pet_state_t)event->a;
        break;
    case PET_EVT_LOVE:
        s_love_events++;
        break;
    case PET_EVT_FED:
        s_fed_events++;
        break;
    case PET_EVT_GAME_END:
        s_game_end_events++;
        s_game_end_gain = event->b;
        break;
    case PET_EVT_WAKE:
        s_wake_events++;
        break;
    case PET_EVT_SLEEP_ENTER:
        s_sleep_events++;
        break;
    default:
        break;
    }
}

static void reset_log(void)
{
    s_state_events = 0;
    s_love_events = 0;
    s_fed_events = 0;
    s_game_end_events = 0;
    s_wake_events = 0;
    s_sleep_events = 0;
    s_game_end_gain = 0;
    s_last_state = PET_STATE_IDLE;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

static void tick_to(uint64_t target)
{
    while (s_pet.now_ms + 50 <= target) {
        pet_tick(&s_pet, s_pet.now_ms + 50);
    }
    pet_tick(&s_pet, target);
}

static void light_samples(uint64_t duration_ms, int level)
{
    pet_light_sample(&s_pet, level, s_pet.now_ms);
    const uint64_t end = s_pet.now_ms + duration_ms;
    while (s_pet.now_ms < end) {
        const uint64_t next = s_pet.now_ms + 100 <= end ? s_pet.now_ms + 100 : end;
        pet_tick(&s_pet, next);
        pet_light_sample(&s_pet, level, next);
    }
}

static void tap(uint64_t at_ms)
{
    pet_touch_press(&s_pet, at_ms);
    pet_tick(&s_pet, at_ms + 30);
    pet_touch_release(&s_pet, at_ms + 60);
    pet_tick(&s_pet, at_ms + 60);
}

static void start_game(uint64_t at_ms)
{
    tap(at_ms);
    tap(at_ms + 300);
    tap(at_ms + 600);
    CHECK(pet_state(&s_pet) == PET_STATE_GAME);
}

static void test_init(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    CHECK(pet_affection(&s_pet) == 50);
    CHECK(pet_energy(&s_pet) == 50);
    CHECK(pet_satiety(&s_pet) == 0);
    CHECK(!pet_light_is_known(&s_pet));
}

static void test_energy_decay(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    tick_to(59999);
    CHECK(pet_energy(&s_pet) == 50);
    tick_to(60000);
    CHECK(pet_energy(&s_pet) == 49);
    tick_to(120000);
    CHECK(pet_energy(&s_pet) == 48);
}

static void test_satiety_decay(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    for (int i = 0; i < 10; ++i) {
        CHECK(pet_feed(&s_pet));
        pet_tick(&s_pet, (uint64_t)(i + 1) * 10);
    }
    CHECK(pet_satiety(&s_pet) == 50);
    tick_to(300000);
    CHECK(pet_satiety(&s_pet) == 49);
}

static void test_long_press_once(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    pet_touch_press(&s_pet, 1000);
    pet_tick(&s_pet, 3999);
    CHECK(pet_affection(&s_pet) == 50);
    pet_tick(&s_pet, 4000);
    CHECK(pet_affection(&s_pet) == 55);
    CHECK(s_love_events == 1);
    pet_tick(&s_pet, 4500);
    pet_tick(&s_pet, 5000);
    CHECK(pet_affection(&s_pet) == 55);
    CHECK(s_love_events == 1);
    pet_touch_release(&s_pet, 5200);
    pet_tick(&s_pet, 6000);
    CHECK(pet_affection(&s_pet) == 55);

    /* 第二次按压可以再次触发 */
    pet_touch_press(&s_pet, 7000);
    pet_tick(&s_pet, 10000);
    CHECK(pet_affection(&s_pet) == 60);
    CHECK(s_love_events == 2);
    pet_touch_release(&s_pet, 10100);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
}

static void test_triple_tap_game(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    start_game(1000);
    CHECK(pet_score(&s_pet) == 0);
    CHECK(s_state_events == 1);

    const uint64_t game_start = 1000 + 600 + 60;
    CHECK(pet_game_hit(&s_pet, game_start + 500));
    CHECK(pet_game_hit(&s_pet, game_start + 1000));
    CHECK(pet_score(&s_pet) == 2);

    /* 时间到：立即停止计分，只结算一次 */
    pet_tick(&s_pet, game_start + 15000);
    CHECK(pet_state(&s_pet) == PET_STATE_SETTLE);
    CHECK(!pet_game_hit(&s_pet, game_start + 15001));
    CHECK(pet_energy(&s_pet) == 52);
    CHECK(s_game_end_events == 1);
    CHECK(s_game_end_gain == 2);

    /* 结算 3 秒后回到待机 */
    pet_tick(&s_pet, game_start + 17999);
    CHECK(pet_state(&s_pet) == PET_STATE_SETTLE);
    pet_tick(&s_pet, game_start + 18000);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    CHECK(s_game_end_events == 1);
    CHECK(pet_energy(&s_pet) == 52);

    /* 结算期间喂食/三连击禁用 */
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    reset_log();
    start_game(1000);
    const uint64_t gs = 1660;
    pet_tick(&s_pet, gs + 15000);
    CHECK(pet_state(&s_pet) == PET_STATE_SETTLE);
    CHECK(!pet_feed(&s_pet));
    tap(gs + 15100);
    tap(gs + 15200);
    tap(gs + 15300);
    CHECK(pet_state(&s_pet) == PET_STATE_SETTLE);
}

static void test_tap_window(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    tap(0);
    tap(400);
    tap(900); /* 距第一次 900ms > 800ms，序列重置，不进入游戏 */
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    tap(1200);
    tap(1500);
    CHECK(pet_state(&s_pet) == PET_STATE_GAME);
}

static void test_slow_press_not_tap(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    pet_touch_press(&s_pet, 0);
    pet_tick(&s_pet, 500);
    pet_touch_release(&s_pet, 500); /* 500ms 中按：既不算 tap 也不抚摸 */
    pet_tick(&s_pet, 600);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    CHECK(pet_affection(&s_pet) == 50);
    CHECK(s_love_events == 0);
}

static void test_sleep_and_energy(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    light_samples(3000, 5);
    CHECK(pet_state(&s_pet) == PET_STATE_SLEEP);
    CHECK(s_sleep_events == 1);

    /* 每连续 30 秒 +1 */
    tick_to(s_pet.now_ms + 30000);
    CHECK(pet_energy(&s_pet) == 51);

    /* 不足 30 秒的部分醒来后清除 */
    tick_to(s_pet.now_ms + 20000);
    light_samples(4000, 90);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    CHECK(s_wake_events == 1);
    tick_to(s_pet.now_ms + 20000);
    CHECK(pet_energy(&s_pet) == 51);
}

static void test_wake_protection(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    light_samples(3000, 5);
    CHECK(pet_state(&s_pet) == PET_STATE_SLEEP);

    const uint64_t wake_at = s_pet.now_ms + 100;
    tap(wake_at);
    tap(wake_at + 200);
    tap(wake_at + 400);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    CHECK(s_wake_events == 1);

    /* 黑暗中 3 分钟内不入睡 */
    tick_to(wake_at + 60000);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    tick_to(wake_at + 179000);
    CHECK(pet_state(&s_pet) == PET_STATE_IDLE);
    tick_to(wake_at + 181000);
    CHECK(pet_state(&s_pet) == PET_STATE_SLEEP);
}

static void test_game_not_interrupted_by_light(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    start_game(1000);
    const uint64_t gs = 1660;
    light_samples(4000, 2); /* 游戏中持续黑暗，只记录，不中断 */
    CHECK(pet_state(&s_pet) == PET_STATE_GAME);
    CHECK(pet_light_is_dark(&s_pet));

    pet_tick(&s_pet, gs + 15000);
    CHECK(pet_state(&s_pet) == PET_STATE_SETTLE);
    pet_tick(&s_pet, gs + 18000);
    CHECK(pet_state(&s_pet) == PET_STATE_SLEEP);
}

static void test_persistence(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    s_pet.affection = 12;
    s_pet.energy = 34;
    s_pet.satiety = 56;

    uint8_t buf[PET_SNAPSHOT_SIZE];
    CHECK(pet_snapshot_write(&s_pet, buf, sizeof(buf)) == PET_SNAPSHOT_SIZE);

    pet_t other;
    pet_init(&other, NULL, on_event, NULL, 0);
    CHECK(pet_snapshot_read(&other, buf, sizeof(buf)));
    CHECK(pet_affection(&other) == 12);
    CHECK(pet_energy(&other) == 34);
    CHECK(pet_satiety(&other) == 56);

    uint8_t bad[PET_SNAPSHOT_SIZE];
    memcpy(bad, buf, sizeof(bad));
    bad[5] ^= 0xFF;
    CHECK(!pet_snapshot_read(&other, bad, sizeof(bad)));
    CHECK(pet_affection(&other) == 12);

    CHECK(!pet_snapshot_read(&other, buf, 4));
}

static void test_attribute_clamps(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    for (int i = 0; i < 20; ++i) {
        const uint64_t at = (uint64_t)i * 4000;
        pet_touch_press(&s_pet, at);
        pet_tick(&s_pet, at + 3000);
        pet_touch_release(&s_pet, at + 3100);
    }
    CHECK(pet_affection(&s_pet) == 100);

    for (int i = 0; i < 30; ++i) {
        pet_feed(&s_pet);
    }
    CHECK(pet_satiety(&s_pet) == 100);

    s_pet.energy = 0;
    tick_to(s_pet.now_ms + 120000);
    CHECK(pet_energy(&s_pet) == 0);
}

static void test_game_energy_clamp(void)
{
    reset_log();
    pet_init(&s_pet, NULL, on_event, NULL, 0);
    s_pet.energy = 99;
    start_game(1000);
    const uint64_t gs = 1660;
    for (int i = 0; i < 5; ++i) {
        CHECK(pet_game_hit(&s_pet, gs + 100 + (uint64_t)i * 100));
    }
    pet_tick(&s_pet, gs + 15000);
    CHECK(pet_energy(&s_pet) == 100);
    CHECK(s_game_end_gain == 1);
}

int main(void)
{
    test_init();
    test_energy_decay();
    test_satiety_decay();
    test_long_press_once();
    test_triple_tap_game();
    test_tap_window();
    test_slow_press_not_tap();
    test_sleep_and_energy();
    test_wake_protection();
    test_game_not_interrupted_by_light();
    test_persistence();
    test_attribute_clamps();
    test_game_energy_clamp();
    printf("pet_core host tests: all passed\n");
    return 0;
}
