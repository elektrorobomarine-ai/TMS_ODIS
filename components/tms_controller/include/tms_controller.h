#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "motor_pwm.h"
#include "tms_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ODIS TMS controller operating modes.
 *
 * STOP:
 *   All motor outputs neutral.
 *
 * ULUR / TARIK:
 *   Automatic TMS operation. Drum + output motor follow the selected
 *   operation direction and carriage traverses left/right automatically.
 *
 * MANUAL:
 *   One or more motors can be commanded independently with temporary
 *   speed and direction. Manual speed is NOT stored to NVS.
 */
typedef enum {
    TMS_MODE_STOP = 0,
    TMS_MODE_ULUR,
    TMS_MODE_TARIK,
    TMS_MODE_MANUAL,
} tms_mode_t;


/*
 * Current/preferred carriage travel direction.
 */
typedef enum {
    TMS_CARRIAGE_LEFT = -1,
    TMS_CARRIAGE_STOP = 0,
    TMS_CARRIAGE_RIGHT = 1,
} tms_carriage_dir_t;


/*
 * Manual runtime state for one motor.
 *
 * direction is only meaningful when active == true.
 */
typedef struct {
    bool active;
    tms_speed_direction_t direction;
    uint16_t speed_us;
} tms_manual_motor_state_t;


/*
 * Controller status snapshot.
 */
typedef struct {
    tms_mode_t mode;

    tms_carriage_dir_t carriage_dir;

    bool left_limit;
    bool right_limit;

    /*
     * Actual pulse currently requested from motor_pwm.
     */
    uint16_t motor_pulse_us[MOTOR_COUNT];

    /*
     * Temporary manual state.
     * These fields are useful for TCP/Web status reporting.
     */
    tms_manual_motor_state_t manual[MOTOR_COUNT];

} tms_status_t;


/* ============================================================
 * INITIALIZATION
 * ============================================================ */

esp_err_t tms_controller_init(void);


/* ============================================================
 * AUTOMATIC TMS OPERATION
 * ============================================================ */

/*
 * Global software stop.
 *
 * Stops all automatic and manual motor activity.
 */
esp_err_t tms_controller_stop(void);


/*
 * Automatic feed-out operation.
 *
 * Drum:
 *   uses drum ULUR NVS profile
 *
 * Output:
 *   uses output ULUR NVS profile
 *
 * Carriage:
 *   uses LEFT/RIGHT profile according to current traverse direction
 */
esp_err_t tms_controller_ulur(void);


/*
 * Automatic retrieve operation.
 *
 * Drum:
 *   uses drum TARIK NVS profile
 *
 * Output:
 *   uses output TARIK NVS profile
 *
 * Carriage:
 *   uses LEFT/RIGHT profile according to current traverse direction
 */
esp_err_t tms_controller_tarik(void);


/*
 * Reverse preferred/current carriage direction.
 *
 * In automatic ULUR/TARIK:
 *   immediately changes carriage direction and speed profile.
 *
 * In MANUAL:
 *   if carriage is currently running, reverses that manual carriage
 *   command while keeping the same temporary speed.
 *
 * In STOP:
 *   only changes the preferred direction for the next automatic start.
 *
 * Active endpoint limits are always respected.
 */
esp_err_t tms_controller_reverse_carriage(void);


/*
 * Re-apply outputs after a persistent speed profile is changed.
 *
 * Kept with the original one-argument interface so command_handler can
 * call it after changing any profile belonging to that motor.
 *
 * In MANUAL mode, temporary manual speeds remain authoritative.
 */
esp_err_t tms_controller_apply_speed_change(motor_id_t motor);


/* ============================================================
 * MANUAL MOTOR CONTROL
 * ============================================================ */

/*
 * Run one motor manually.
 *
 * Valid motor / direction combinations:
 *
 * MOTOR_DRUM:
 *   TMS_SPEED_DIR_ULUR
 *   TMS_SPEED_DIR_TARIK
 *
 * MOTOR_CARRIAGE:
 *   TMS_SPEED_DIR_LEFT
 *   TMS_SPEED_DIR_RIGHT
 *
 * MOTOR_OUTPUT:
 *   TMS_SPEED_DIR_ULUR
 *   TMS_SPEED_DIR_TARIK
 *
 * speed_us:
 *   temporary PWM magnitude 0..TMS_SPEED_MAX_US.
 *
 * speed_us == 0 is treated as motor_stop(motor).
 *
 * IMPORTANT:
 *   The first manual command while ULUR/TARIK is active switches the
 *   controller to MANUAL and stops all existing automatic outputs before
 *   applying the requested manual motor command.
 *
 * Carriage limit interlock:
 *   LEFT command is rejected while left limit is active.
 *   RIGHT command is rejected while right limit is active.
 */
esp_err_t tms_controller_motor_run(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t speed_us
);


/*
 * Stop one manually-controlled motor.
 *
 * If this is the final active manual motor, controller mode automatically
 * becomes STOP.
 *
 * Calling this while automatic ULUR/TARIK is active intentionally changes
 * to MANUAL/STOP rather than creating a mixed automatic/manual state.
 */
esp_err_t tms_controller_motor_stop(
    motor_id_t motor
);


/* ============================================================
 * STATUS
 * ============================================================ */

void tms_controller_get_status(
    tms_status_t *status
);

const char *tms_mode_name(
    tms_mode_t mode
);

const char *tms_carriage_dir_name(
    tms_carriage_dir_t dir
);


#ifdef __cplusplus
}
#endif
