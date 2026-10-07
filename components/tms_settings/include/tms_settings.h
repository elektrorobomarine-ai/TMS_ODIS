#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "motor_pwm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ODIS TMS - Persistent directional motor speed settings
 *
 * speed_us is a PWM magnitude/offset from MOTOR_PWM_NEUTRAL_US.
 *
 * Example with neutral = 1500 us:
 *   speed_us = 250
 *   logical positive direction -> 1750 us
 *   logical negative direction -> 1250 us
 *
 * Direction-to-physical-PWM polarity is NOT handled here.
 * That remains the responsibility of tms_controller / board_config.
 */

/* Valid persistent speed range. uint16_t already guarantees >= 0. */
#define TMS_SPEED_MIN_US                    0U
#define TMS_SPEED_MAX_US                    500U

/* Defaults preserve the previous v0.3.0 behavior. */
#define TMS_DEFAULT_DRUM_ULUR_SPEED_US      250U
#define TMS_DEFAULT_DRUM_TARIK_SPEED_US     250U

#define TMS_DEFAULT_CARR_LEFT_SPEED_US      180U
#define TMS_DEFAULT_CARR_RIGHT_SPEED_US     180U

#define TMS_DEFAULT_OUTPUT_ULUR_SPEED_US    250U
#define TMS_DEFAULT_OUTPUT_TARIK_SPEED_US   250U

/*
 * Direction is semantic at the TMS level.
 *
 * Valid combinations:
 *   MOTOR_DRUM      : TMS_SPEED_DIR_ULUR / TMS_SPEED_DIR_TARIK
 *   MOTOR_CARRIAGE  : TMS_SPEED_DIR_LEFT / TMS_SPEED_DIR_RIGHT
 *   MOTOR_OUTPUT    : TMS_SPEED_DIR_ULUR / TMS_SPEED_DIR_TARIK
 */
typedef enum {
    TMS_SPEED_DIR_ULUR = 0,
    TMS_SPEED_DIR_TARIK,
    TMS_SPEED_DIR_LEFT,
    TMS_SPEED_DIR_RIGHT,
} tms_speed_direction_t;

/*
 * All persistent directional speed profiles.
 * Values are PWM offsets in microseconds (0..500).
 */
typedef struct {
    uint16_t drum_ulur_speed_us;
    uint16_t drum_tarik_speed_us;

    uint16_t carriage_left_speed_us;
    uint16_t carriage_right_speed_us;

    uint16_t output_ulur_speed_us;
    uint16_t output_tarik_speed_us;
} tms_settings_t;

/*
 * Initialize settings module and load NVS.
 *
 * This function:
 *   1. Loads defaults.
 *   2. Loads the new directional NVS keys if present.
 *   3. Falls back to legacy v0.3.0 keys if necessary.
 *   4. Automatically migrates legacy values to the new key format.
 *
 * nvs_flash_init() must already have been called by app_main().
 */
esp_err_t tms_settings_init(void);

/*
 * Read-only pointer to current settings.
 *
 * Kept for convenience/backward style compatibility.
 * Do not modify the returned object directly.
 *
 * For code that needs a coherent multi-field snapshot while HTTP/TCP can
 * update settings concurrently, prefer tms_settings_get_snapshot().
 */
const tms_settings_t *tms_settings_get(void);

/*
 * Copy all current settings atomically into caller-owned storage.
 */
esp_err_t tms_settings_get_snapshot(tms_settings_t *out_settings);

/*
 * Read one directional speed profile.
 */
esp_err_t tms_settings_get_speed(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t *speed_us
);

/*
 * Change one directional speed profile.
 *
 * save_to_nvs = true:
 *   Update RAM and persist this profile immediately to NVS.
 *
 * save_to_nvs = false:
 *   Update RAM only.
 *
 * For manual/jog motor commands with temporary speed, DO NOT call this
 * function. Manual speed should go directly through tms_controller.
 */
esp_err_t tms_settings_set_speed(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t speed_us,
    bool save_to_nvs
);

/*
 * Save all six current profiles to NVS.
 */
esp_err_t tms_settings_save(void);

/*
 * Restore all six profiles to compile-time defaults.
 * Optionally persist defaults to NVS.
 */
esp_err_t tms_settings_restore_defaults(bool save_to_nvs);

/*
 * Helpers for command/status layers.
 */
bool tms_settings_is_valid_motor_direction(
    motor_id_t motor,
    tms_speed_direction_t direction
);

const char *tms_settings_direction_name(tms_speed_direction_t direction);

#ifdef __cplusplus
}
#endif
