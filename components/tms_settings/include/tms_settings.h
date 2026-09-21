#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "motor_pwm.h"

/* Speed is PWM delta from neutral in microseconds (0..500). */
typedef struct {
    uint16_t drum_speed_us;
    uint16_t carriage_speed_us;
    uint16_t output_speed_us;
} tms_settings_t;

#define TMS_DEFAULT_DRUM_SPEED_US       250
#define TMS_DEFAULT_CARRIAGE_SPEED_US   180
#define TMS_DEFAULT_OUTPUT_SPEED_US     250
#define TMS_SPEED_MIN_US                0
#define TMS_SPEED_MAX_US                500

esp_err_t tms_settings_init(void);
const tms_settings_t *tms_settings_get(void);
esp_err_t tms_settings_set_speed(motor_id_t motor, uint16_t speed_us, bool save_to_nvs);
esp_err_t tms_settings_save(void);
