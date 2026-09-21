#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "app_config.h"

typedef enum {
    MOTOR_DRUM = 0,
    MOTOR_CARRIAGE = 1,
    MOTOR_OUTPUT = 2,
} motor_id_t;

esp_err_t motor_pwm_init(void);
esp_err_t motor_pwm_set_us(uint8_t motor_index, uint16_t pulse_us);
esp_err_t motor_pwm_set_all(const uint16_t pulse_us[MOTOR_COUNT]);
esp_err_t motor_pwm_set_safe(uint8_t motor_index);
esp_err_t motor_pwm_set_all_safe(void);
uint16_t motor_pwm_get_us(uint8_t motor_index);
