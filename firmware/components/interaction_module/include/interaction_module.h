#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IM_LED_COUNT 6

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} im_rgb_t;

typedef struct {
    bool left;
    bool down;
    uint32_t timestamp_ms;
} im_pad_event_t;

typedef struct {
    bool module_present;
    bool slot_right;
    char slot_name[8];
    float light_pct;
    bool light_valid;
    int ldr_raw;
    int ldr_channel;
    int ldr_raw_alt;
    int ldr_channel_alt;
    bool pad_l;
    bool pad_r;
} im_state_t;

esp_err_t im_init_all(void);
void im_tick(uint32_t now_ms);

const im_state_t *im_get_state(void);
bool im_module_present(void);

/** Pop one debounced pad edge (non-blocking). */
bool im_pad_event_pop(im_pad_event_t *out);

/** Write all six WS2812 pixels in one frame (GRB generated internally). */
void im_leds_write(const im_rgb_t colors[IM_LED_COUNT]);
void im_leds_fill(uint8_t r, uint8_t g, uint8_t b);
void im_leds_clear(void);
void im_leds_show_stage(int stage, bool ok);

/** Last sampled ambient light 0..100. */
float im_light_pct(void);

#ifdef __cplusplus
}
#endif
