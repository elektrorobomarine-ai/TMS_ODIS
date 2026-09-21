#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool active;
    uint32_t change_count;
} limit_input_state_t;

typedef void (*limit_input_callback_t)(uint8_t index, const limit_input_state_t *state, void *ctx);

esp_err_t limit_input_init(void);
esp_err_t limit_input_get(uint8_t index, limit_input_state_t *state);
void limit_input_set_callback(limit_input_callback_t callback, void *ctx);
