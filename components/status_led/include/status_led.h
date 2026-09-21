#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    STATUS_LED_BOOT = 0,
    STATUS_LED_NET_WAIT,
    STATUS_LED_READY,
    STATUS_LED_CLIENT,
    STATUS_LED_ERROR,
} status_led_state_t;

esp_err_t status_led_init(void);
void status_led_set_system(status_led_state_t state);
void status_led_set_manual(uint8_t red, uint8_t green, uint8_t blue);
void status_led_set_auto(bool enable);
bool status_led_is_auto(void);
