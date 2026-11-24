#include "pet_store.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "PET_STORE";
static const char *NS = "star_pet";
static const char *KEY = "state";

#define PET_SAVE_MAGIC 0x53505254u /* SPRT */
#define PET_SAVE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;

    float satiety;
    float energy;
    float intimacy;
    uint8_t sleeping;
    uint8_t form;
    uint8_t needs_name;
    uint8_t muted;
    char name[16];

    uint8_t unlocked[PET_FORM_MAX];
    uint16_t care_counts[PET_CARE_MAX];
    uint32_t last_care_order[PET_CARE_MAX];
    uint32_t care_seq;

    uint32_t cycle_ms;
} pet_save_t;

static float clampf(float v)
{
    if (v < PET_STAT_MIN) return PET_STAT_MIN;
    if (v > PET_STAT_MAX) return PET_STAT_MAX;
    return v;
}

static void fill_save(const pet_core_t *core, pet_save_t *out)
{
    const pet_state_t *s = &core->state;
    memset(out, 0, sizeof(*out));
    out->magic = PET_SAVE_MAGIC;
    out->version = PET_SAVE_VERSION;
    out->satiety = s->satiety;
    out->energy = s->energy;
    out->intimacy = s->intimacy;
    out->sleeping = s->sleeping ? 1 : 0;
    out->form = (uint8_t)s->form;
    out->needs_name = s->needs_name ? 1 : 0;
    out->muted = s->muted ? 1 : 0;
    memcpy(out->name, s->name, sizeof(out->name));
    for (int i = 0; i < PET_FORM_MAX; i++) out->unlocked[i] = s->unlocked[i] ? 1 : 0;
    for (int i = 0; i < PET_CARE_MAX; i++) out->care_counts[i] = s->care_counts[i];
    for (int i = 0; i < PET_CARE_MAX; i++) out->last_care_order[i] = s->last_care_order[i];
    out->care_seq = s->care_seq;
    out->cycle_ms = s->cycle_ms;
}

void pet_store_save(const pet_core_t *core)
{
    if (!core) return;

    pet_save_t save;
    fill_save(core, &save);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(h, KEY, &save, sizeof(save));
    if (err == ESP_OK) err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

bool pet_store_load(pet_core_t *core, uint32_t now_ms)
{
    if (!core) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return false;

    pet_save_t save;
    size_t len = sizeof(save);
    err = nvs_get_blob(h, KEY, &save, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(save) || save.magic != PET_SAVE_MAGIC ||
        save.version != PET_SAVE_VERSION) {
        return false;
    }

    pet_core_init(core, now_ms);
    pet_state_t *s = &core->state;
    s->satiety = clampf(save.satiety);
    s->energy = clampf(save.energy);
    s->intimacy = clampf(save.intimacy);
    s->sleeping = save.sleeping != 0;
    s->needs_name = save.needs_name != 0;
    s->muted = save.muted != 0;
    memset(s->name, 0, sizeof(s->name));
    memcpy(s->name, save.name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    if (s->name[0] == '\0') s->needs_name = true;

    s->form = (save.form < PET_FORM_MAX) ? (pet_form_t)save.form : PET_FORM_BABY;
    for (int i = 0; i < PET_FORM_MAX; i++) s->unlocked[i] = save.unlocked[i] != 0;
    s->unlocked[PET_FORM_BABY] = true;
    for (int i = 0; i < PET_CARE_MAX; i++) s->care_counts[i] = save.care_counts[i];
    for (int i = 0; i < PET_CARE_MAX; i++) s->last_care_order[i] = save.last_care_order[i];
    s->care_seq = save.care_seq;
    s->cycle_ms = 0; /* legacy field, kept for NVS layout */
    s->mode = s->sleeping ? PET_MODE_SLEEP : PET_MODE_IDLE;
    s->result = PET_RESULT_NONE;
    s->game_lane = 1;
    s->game_star_lane = -1;
    s->last_tick_ms = now_ms;
    s->last_interaction_ms = now_ms;
    s->last_reminder_ms = now_ms;
    core->post_action_neutral_until_ms = now_ms;
    core->wait_release_all = false;

    ESP_LOGI(TAG, "loaded %s: sat=%.1f energy=%.1f bond=%.1f cycle=%lu sleeping=%d",
             s->name, s->satiety, s->energy, s->intimacy,
             (unsigned long)s->cycle_ms, s->sleeping);
    return true;
}

void pet_store_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, KEY);
        nvs_commit(h);
        nvs_close(h);
    }
}
