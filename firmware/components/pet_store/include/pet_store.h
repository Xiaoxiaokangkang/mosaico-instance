#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pet_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load a saved pet into core. Returns false when no compatible save exists. */
bool pet_store_load(pet_core_t *core, uint32_t now_ms);

/** Persist the durable fields of the pet. Safe to call periodically. */
void pet_store_save(const pet_core_t *core);

/** Erase all pet save data (factory reset / debug). */
void pet_store_erase(void);

#ifdef __cplusplus
}
#endif
