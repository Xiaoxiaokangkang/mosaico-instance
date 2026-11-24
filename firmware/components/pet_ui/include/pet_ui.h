#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_dev.h"
#include "pet_core.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pet_ui_init(esp_lcd_panel_handle_t panel, pet_core_t *core);
void pet_ui_refresh(void);
void pet_ui_show_toast(const char *text);
bool pet_ui_setup_active(void);

#ifdef __cplusplus
}
#endif
