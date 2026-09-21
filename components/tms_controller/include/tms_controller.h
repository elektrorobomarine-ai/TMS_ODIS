#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "motor_pwm.h"

typedef enum {
    TMS_MODE_STOP = 0,
    TMS_MODE_ULUR,
    TMS_MODE_TARIK,
} tms_mode_t;

typedef enum {
    TMS_CARRIAGE_LEFT = -1,
    TMS_CARRIAGE_STOP = 0,
    TMS_CARRIAGE_RIGHT = 1,
} tms_carriage_dir_t;

typedef struct {
    tms_mode_t mode;
    tms_carriage_dir_t carriage_dir;
    bool left_limit;
    bool right_limit;
    uint16_t motor_pulse_us[3];
} tms_status_t;

esp_err_t tms_controller_init(void);
esp_err_t tms_controller_stop(void);
esp_err_t tms_controller_ulur(void);
esp_err_t tms_controller_tarik(void);
esp_err_t tms_controller_reverse_carriage(void);
esp_err_t tms_controller_apply_speed_change(motor_id_t motor);
void tms_controller_get_status(tms_status_t *status);
const char *tms_mode_name(tms_mode_t mode);
const char *tms_carriage_dir_name(tms_carriage_dir_t dir);
