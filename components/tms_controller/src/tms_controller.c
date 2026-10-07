#include "tms_controller.h"

#include <string.h>

#include "app_config.h"
#include "board_config.h"
#include "limit_input.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"


static const char *TAG = "tms_controller";


/* ============================================================
 * EVENT QUEUE
 * ============================================================ */

typedef enum {
    EVT_LIMIT = 1,
} event_type_t;


typedef struct {
    event_type_t type;
    uint8_t index;
    bool active;
} tms_event_t;


/* ============================================================
 * MODULE STATE
 * ============================================================ */

static QueueHandle_t s_queue = NULL;
static SemaphoreHandle_t s_lock = NULL;

static tms_mode_t s_mode = TMS_MODE_STOP;

static tms_carriage_dir_t s_carriage_dir =
    TMS_CARRIAGE_RIGHT;

static bool s_left_limit = false;
static bool s_right_limit = false;

static tms_manual_motor_state_t s_manual[MOTOR_COUNT];


/* ============================================================
 * LOW LEVEL DIRECTION -> PWM
 * ============================================================ */

static uint16_t pulse_from_direction(
    int logical_dir,
    int polarity,
    uint16_t speed_us)
{
    int value =
        MOTOR_PWM_NEUTRAL_US +
        (
            logical_dir *
            polarity *
            (int)speed_us
        );


    if (value < MOTOR_PWM_MIN_US) {

        value = MOTOR_PWM_MIN_US;
    }


    if (value > MOTOR_PWM_MAX_US) {

        value = MOTOR_PWM_MAX_US;
    }


    return (uint16_t)value;
}


/*
 * Convert semantic TMS direction to one physical PWM pulse.
 *
 * Direction polarity and mechanical inversion remain configurable through
 * board_config.h.
 */
static esp_err_t motor_direction_to_pulse(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t speed_us,
    uint16_t *pulse_us)
{
    if (pulse_us == NULL) {

        return ESP_ERR_INVALID_ARG;
    }


    if (speed_us > TMS_SPEED_MAX_US) {

        return ESP_ERR_INVALID_ARG;
    }


    if (
        !tms_settings_is_valid_motor_direction(
            motor,
            direction
        )
    ) {

        return ESP_ERR_INVALID_ARG;
    }


    int logical_dir = 0;
    int polarity = 1;


    switch (motor) {

        case MOTOR_DRUM:

            polarity =
                DRUM_DIRECTION_POLARITY;


            logical_dir =
                (direction == TMS_SPEED_DIR_ULUR)
                ? DRUM_DIR_ULUR
                : DRUM_DIR_TARIK;

            break;


        case MOTOR_CARRIAGE:

            polarity =
                CARRIAGE_DIRECTION_POLARITY;


            logical_dir =
                (direction == TMS_SPEED_DIR_LEFT)
                ? CARRIAGE_DIR_LEFT
                : CARRIAGE_DIR_RIGHT;

            break;


        case MOTOR_OUTPUT:

            polarity =
                OUTPUT_DIRECTION_POLARITY;


            logical_dir =
                (direction == TMS_SPEED_DIR_ULUR)
                ? OUTPUT_DIR_ULUR
                : OUTPUT_DIR_TARIK;

            break;


        default:

            return ESP_ERR_INVALID_ARG;
    }


    *pulse_us =
        pulse_from_direction(
            logical_dir,
            polarity,
            speed_us
        );


    return ESP_OK;
}


/* ============================================================
 * MANUAL STATE HELPERS
 * ============================================================ */

static void clear_manual_locked(void)
{
    memset(
        s_manual,
        0,
        sizeof(s_manual)
    );
}


static bool any_manual_active_locked(void)
{
    for (
        int i = 0;
        i < MOTOR_COUNT;
        ++i
    ) {

        if (s_manual[i].active) {

            return true;
        }
    }


    return false;
}


/* ============================================================
 * CARRIAGE INTERLOCK HELPERS
 * ============================================================ */

/*
 * Return true if requested carriage direction would drive farther
 * into an already-active endpoint.
 */
static bool carriage_direction_blocked_locked(
    tms_speed_direction_t direction)
{
    if (
        direction == TMS_SPEED_DIR_LEFT &&
        s_left_limit
    ) {

        return true;
    }


    if (
        direction == TMS_SPEED_DIR_RIGHT &&
        s_right_limit
    ) {

        return true;
    }


    return false;
}


/*
 * Normalize automatic carriage direction after mode changes or limit
 * transitions.
 *
 * If both limits are active simultaneously, carriage is stopped because
 * there is no safe automatic direction.
 */
static void normalize_auto_carriage_direction_locked(void)
{
    if (
        s_left_limit &&
        s_right_limit
    ) {

        s_carriage_dir =
            TMS_CARRIAGE_STOP;


        ESP_LOGW(
            TAG,
            "Both carriage limits active -> carriage STOP"
        );


        return;
    }


    if (s_left_limit) {

        s_carriage_dir =
            TMS_CARRIAGE_RIGHT;


        return;
    }


    if (s_right_limit) {

        s_carriage_dir =
            TMS_CARRIAGE_LEFT;


        return;
    }


    /*
     * If there is no active endpoint but previous state was STOP,
     * choose RIGHT as deterministic startup direction.
     */
    if (s_carriage_dir == TMS_CARRIAGE_STOP) {

        s_carriage_dir =
            TMS_CARRIAGE_RIGHT;
    }
}


/* ============================================================
 * OUTPUT CALCULATION
 * ============================================================ */

static esp_err_t apply_automatic_outputs_locked(
    uint16_t out[MOTOR_COUNT])
{
    tms_settings_t cfg;


    esp_err_t err =
        tms_settings_get_snapshot(
            &cfg
        );


    if (err != ESP_OK) {

        return err;
    }


    tms_speed_direction_t operation_dir;


    if (s_mode == TMS_MODE_ULUR) {

        operation_dir =
            TMS_SPEED_DIR_ULUR;

    } else if (s_mode == TMS_MODE_TARIK) {

        operation_dir =
            TMS_SPEED_DIR_TARIK;

    } else {

        return ESP_ERR_INVALID_STATE;
    }


    /* --------------------------------------------------------
     * DRUM
     * -------------------------------------------------------- */

    uint16_t drum_speed =
        (operation_dir == TMS_SPEED_DIR_ULUR)
        ? cfg.drum_ulur_speed_us
        : cfg.drum_tarik_speed_us;


    err =
        motor_direction_to_pulse(
            MOTOR_DRUM,
            operation_dir,
            drum_speed,
            &out[MOTOR_DRUM]
        );


    if (err != ESP_OK) {

        return err;
    }


    /* --------------------------------------------------------
     * OUTPUT / TRACTION MOTOR
     * -------------------------------------------------------- */

    uint16_t output_speed =
        (operation_dir == TMS_SPEED_DIR_ULUR)
        ? cfg.output_ulur_speed_us
        : cfg.output_tarik_speed_us;


    err =
        motor_direction_to_pulse(
            MOTOR_OUTPUT,
            operation_dir,
            output_speed,
            &out[MOTOR_OUTPUT]
        );


    if (err != ESP_OK) {

        return err;
    }


    /* --------------------------------------------------------
     * CARRIAGE
     * -------------------------------------------------------- */

    if (s_carriage_dir == TMS_CARRIAGE_LEFT) {

        /*
         * Secondary safety check:
         * even if state logic failed to normalize direction,
         * never command farther into active limit.
         */
        if (!s_left_limit) {

            err =
                motor_direction_to_pulse(
                    MOTOR_CARRIAGE,
                    TMS_SPEED_DIR_LEFT,
                    cfg.carriage_left_speed_us,
                    &out[MOTOR_CARRIAGE]
                );


            if (err != ESP_OK) {

                return err;
            }
        }


    } else if (
        s_carriage_dir ==
        TMS_CARRIAGE_RIGHT
    ) {

        if (!s_right_limit) {

            err =
                motor_direction_to_pulse(
                    MOTOR_CARRIAGE,
                    TMS_SPEED_DIR_RIGHT,
                    cfg.carriage_right_speed_us,
                    &out[MOTOR_CARRIAGE]
                );


            if (err != ESP_OK) {

                return err;
            }
        }
    }


    return ESP_OK;
}


static esp_err_t apply_manual_outputs_locked(
    uint16_t out[MOTOR_COUNT])
{
    for (
        int i = 0;
        i < MOTOR_COUNT;
        ++i
    ) {

        if (!s_manual[i].active) {

            continue;
        }


        motor_id_t motor =
            (motor_id_t)i;


        tms_speed_direction_t direction =
            s_manual[i].direction;


        /*
         * Hard carriage interlock.
         *
         * If a limit became active after the command had already started,
         * force carriage neutral here as a second layer of protection.
         */
        if (
            motor == MOTOR_CARRIAGE &&
            carriage_direction_blocked_locked(direction)
        ) {

            continue;
        }


        esp_err_t err =
            motor_direction_to_pulse(
                motor,
                direction,
                s_manual[i].speed_us,
                &out[i]
            );


        if (err != ESP_OK) {

            return err;
        }
    }


    return ESP_OK;
}


/*
 * Apply current controller state to all motor PWM outputs.
 *
 * Must be called with s_lock held.
 */
static esp_err_t apply_outputs_locked(void)
{
    uint16_t out[MOTOR_COUNT] = {
        MOTOR_PWM_NEUTRAL_US,
        MOTOR_PWM_NEUTRAL_US,
        MOTOR_PWM_NEUTRAL_US
    };


    esp_err_t err = ESP_OK;


    switch (s_mode) {

        case TMS_MODE_STOP:

            /* Neutral defaults already prepared. */
            break;


        case TMS_MODE_ULUR:
        case TMS_MODE_TARIK:

            err =
                apply_automatic_outputs_locked(
                    out
                );

            break;


        case TMS_MODE_MANUAL:

            err =
                apply_manual_outputs_locked(
                    out
                );

            break;


        default:

            return ESP_ERR_INVALID_STATE;
    }


    if (err != ESP_OK) {

        return err;
    }


    return motor_pwm_set_all(
        out
    );
}


/* ============================================================
 * LIMIT INPUT CALLBACK
 * ============================================================ */

/*
 * limit_input callback must remain lightweight.
 *
 * Actual controller logic runs in controller_task().
 */
static void limit_callback(
    uint8_t index,
    const limit_input_state_t *state,
    void *ctx)
{
    (void)ctx;


    if (
        s_queue == NULL ||
        state == NULL ||
        index > 1
    ) {

        return;
    }


    tms_event_t evt = {
        .type = EVT_LIMIT,
        .index = index,
        .active = state->active,
    };


    if (
        xQueueSend(
            s_queue,
            &evt,
            0
        ) != pdTRUE
    ) {

        ESP_LOGW(
            TAG,
            "Controller event queue full; limit event dropped"
        );
    }
}


/* ============================================================
 * LIMIT EVENT PROCESSING
 * ============================================================ */

static void process_limit_event_locked(
    uint8_t index,
    bool active)
{
    if (index == 0) {

        s_left_limit =
            active;

    } else if (index == 1) {

        s_right_limit =
            active;

    } else {

        return;
    }


    /*
     * We only need motion intervention on ACTIVE transition.
     */
    if (!active) {

        return;
    }


    /* --------------------------------------------------------
     * AUTOMATIC MODE
     *
     * At an endpoint, carriage automatically reverses.
     * The newly-selected direction automatically picks its own
     * left/right NVS speed profile.
     * -------------------------------------------------------- */

    if (
        s_mode == TMS_MODE_ULUR ||
        s_mode == TMS_MODE_TARIK
    ) {

        if (
            s_left_limit &&
            s_right_limit
        ) {

            s_carriage_dir =
                TMS_CARRIAGE_STOP;


            ESP_LOGE(
                TAG,
                "Both carriage limits active -> automatic carriage stopped"
            );


            (void)apply_outputs_locked();

            return;
        }


        if (
            index == 0 &&
            s_carriage_dir == TMS_CARRIAGE_LEFT
        ) {

            s_carriage_dir =
                TMS_CARRIAGE_RIGHT;


            ESP_LOGI(
                TAG,
                "LEFT limit active -> automatic carriage RIGHT"
            );


            (void)apply_outputs_locked();

            return;
        }


        if (
            index == 1 &&
            s_carriage_dir == TMS_CARRIAGE_RIGHT
        ) {

            s_carriage_dir =
                TMS_CARRIAGE_LEFT;


            ESP_LOGI(
                TAG,
                "RIGHT limit active -> automatic carriage LEFT"
            );


            (void)apply_outputs_locked();

            return;
        }
    }


    /* --------------------------------------------------------
     * MANUAL MODE
     *
     * Manual carriage does NOT auto-reverse.
     * It stops at the endpoint while other manually-running motors
     * are left unchanged.
     * -------------------------------------------------------- */

    if (
        s_mode == TMS_MODE_MANUAL &&
        s_manual[MOTOR_CARRIAGE].active
    ) {

        const tms_speed_direction_t direction =
            s_manual[MOTOR_CARRIAGE].direction;


        if (
            carriage_direction_blocked_locked(
                direction
            )
        ) {

            s_manual[MOTOR_CARRIAGE].active =
                false;

            s_manual[MOTOR_CARRIAGE].speed_us =
                0;


            s_carriage_dir =
                TMS_CARRIAGE_STOP;


            ESP_LOGW(
                TAG,
                "Manual carriage stopped by %s limit interlock",
                (index == 0) ? "LEFT" : "RIGHT"
            );


            if (!any_manual_active_locked()) {

                s_mode =
                    TMS_MODE_STOP;
            }


            (void)apply_outputs_locked();
        }
    }
}


/* ============================================================
 * CONTROLLER TASK
 * ============================================================ */

static void controller_task(
    void *arg)
{
    (void)arg;


    tms_event_t evt;


    while (true) {

        if (
            xQueueReceive(
                s_queue,
                &evt,
                portMAX_DELAY
            ) != pdTRUE
        ) {

            continue;
        }


        if (evt.type != EVT_LIMIT) {

            continue;
        }


        xSemaphoreTake(
            s_lock,
            portMAX_DELAY
        );


        process_limit_event_locked(
            evt.index,
            evt.active
        );


        xSemaphoreGive(
            s_lock
        );
    }
}


/* ============================================================
 * INITIALIZATION
 * ============================================================ */

esp_err_t tms_controller_init(void)
{
    if (
        s_lock != NULL ||
        s_queue != NULL
    ) {

        return ESP_ERR_INVALID_STATE;
    }


    s_lock =
        xSemaphoreCreateMutex();


    s_queue =
        xQueueCreate(
            TMS_CTRL_EVENT_QUEUE_LEN,
            sizeof(tms_event_t)
        );


    if (
        s_lock == NULL ||
        s_queue == NULL
    ) {

        return ESP_ERR_NO_MEM;
    }


    clear_manual_locked();


    /*
     * Read initial endpoint states.
     */

    limit_input_state_t st = {0};


    if (
        limit_input_get(
            0,
            &st
        ) == ESP_OK
    ) {

        s_left_limit =
            st.active;
    }


    if (
        limit_input_get(
            1,
            &st
        ) == ESP_OK
    ) {

        s_right_limit =
            st.active;
    }


    /*
     * Select safe initial carriage direction.
     */

    normalize_auto_carriage_direction_locked();


    limit_input_set_callback(
        limit_callback,
        NULL
    );


    if (
        xTaskCreate(
            controller_task,
            "tms_ctrl",
            TMS_CTRL_TASK_STACK_SIZE,
            NULL,
            TMS_CTRL_TASK_PRIORITY,
            NULL
        ) != pdPASS
    ) {

        return ESP_ERR_NO_MEM;
    }


    s_mode =
        TMS_MODE_STOP;


    ESP_LOGI(
        TAG,
        "Controller initialized; limits L=%d R=%d, preferred carriage=%s",
        s_left_limit,
        s_right_limit,
        tms_carriage_dir_name(s_carriage_dir)
    );


    return motor_pwm_set_all_safe();
}


/* ============================================================
 * AUTOMATIC MODE
 * ============================================================ */

static esp_err_t set_automatic_mode(
    tms_mode_t mode)
{
    if (
        mode != TMS_MODE_ULUR &&
        mode != TMS_MODE_TARIK
    ) {

        return ESP_ERR_INVALID_ARG;
    }


    if (s_lock == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    xSemaphoreTake(
        s_lock,
        portMAX_DELAY
    );


    /*
     * Automatic command exits manual mode completely.
     */
    clear_manual_locked();


    s_mode =
        mode;


    normalize_auto_carriage_direction_locked();


    esp_err_t err =
        apply_outputs_locked();


    xSemaphoreGive(
        s_lock
    );


    if (err == ESP_OK) {

        ESP_LOGI(
            TAG,
            "Automatic mode -> %s",
            tms_mode_name(mode)
        );
    }


    return err;
}


esp_err_t tms_controller_stop(void)
{
    if (s_lock == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    xSemaphoreTake(
        s_lock,
        portMAX_DELAY
    );


    s_mode =
        TMS_MODE_STOP;


    clear_manual_locked();


    esp_err_t err =
        apply_outputs_locked();


    xSemaphoreGive(
        s_lock
    );


    if (err == ESP_OK) {

        ESP_LOGI(
            TAG,
            "Global software STOP"
        );
    }


    return err;
}


esp_err_t tms_controller_ulur(void)
{
    return set_automatic_mode(
        TMS_MODE_ULUR
    );
}


esp_err_t tms_controller_tarik(void)
{
    return set_automatic_mode(
        TMS_MODE_TARIK
    );
}


/* ============================================================
 * REVERSE CARRIAGE
 * ============================================================ */

static tms_carriage_dir_t opposite_carriage_dir(
    tms_carriage_dir_t current)
{
    if (current == TMS_CARRIAGE_LEFT) {

        return TMS_CARRIAGE_RIGHT;
    }


    if (current == TMS_CARRIAGE_RIGHT) {

        return TMS_CARRIAGE_LEFT;
    }


    /*
     * Deterministic direction if previous state was STOP.
     */
    return TMS_CARRIAGE_RIGHT;
}


static tms_speed_direction_t carriage_speed_dir_from_state(
    tms_carriage_dir_t dir)
{
    return
        (dir == TMS_CARRIAGE_LEFT)
        ? TMS_SPEED_DIR_LEFT
        : TMS_SPEED_DIR_RIGHT;
}


esp_err_t tms_controller_reverse_carriage(void)
{
    if (s_lock == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    xSemaphoreTake(
        s_lock,
        portMAX_DELAY
    );


    /* --------------------------------------------------------
     * MANUAL CARRIAGE RUNNING
     * -------------------------------------------------------- */

    if (
        s_mode == TMS_MODE_MANUAL &&
        s_manual[MOTOR_CARRIAGE].active
    ) {

        tms_speed_direction_t requested =
            (
                s_manual[MOTOR_CARRIAGE].direction ==
                TMS_SPEED_DIR_LEFT
            )
            ? TMS_SPEED_DIR_RIGHT
            : TMS_SPEED_DIR_LEFT;


        if (
            carriage_direction_blocked_locked(
                requested
            )
        ) {

            xSemaphoreGive(
                s_lock
            );


            ESP_LOGW(
                TAG,
                "Manual carriage reverse rejected by active endpoint"
            );


            return ESP_ERR_INVALID_STATE;
        }


        s_manual[MOTOR_CARRIAGE].direction =
            requested;


        s_carriage_dir =
            (requested == TMS_SPEED_DIR_LEFT)
            ? TMS_CARRIAGE_LEFT
            : TMS_CARRIAGE_RIGHT;


        esp_err_t err =
            apply_outputs_locked();


        xSemaphoreGive(
            s_lock
        );


        return err;
    }


    /* --------------------------------------------------------
     * AUTO / STOP
     * -------------------------------------------------------- */

    tms_carriage_dir_t requested =
        opposite_carriage_dir(
            s_carriage_dir
        );


    if (
        requested == TMS_CARRIAGE_LEFT &&
        s_left_limit
    ) {

        requested =
            TMS_CARRIAGE_RIGHT;
    }


    if (
        requested == TMS_CARRIAGE_RIGHT &&
        s_right_limit
    ) {

        requested =
            TMS_CARRIAGE_LEFT;
    }


    /*
     * If both limits are active there is no safe direction.
     */
    if (
        s_left_limit &&
        s_right_limit
    ) {

        requested =
            TMS_CARRIAGE_STOP;
    }


    s_carriage_dir =
        requested;


    esp_err_t err =
        apply_outputs_locked();


    xSemaphoreGive(
        s_lock
    );


    return err;
}


/* ============================================================
 * SPEED PROFILE REFRESH
 * ============================================================ */

esp_err_t tms_controller_apply_speed_change(
    motor_id_t motor)
{
    /*
     * Keep original API signature.
     *
     * Recomputing all outputs is inexpensive and guarantees that whichever
     * direction is currently active picks up its newly-saved NVS profile.
     */
    (void)motor;


    if (s_lock == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    xSemaphoreTake(
        s_lock,
        portMAX_DELAY
    );


    esp_err_t err =
        apply_outputs_locked();


    xSemaphoreGive(
        s_lock
    );


    return err;
}


/* ============================================================
 * MANUAL MOTOR RUN
 * ============================================================ */

esp_err_t tms_controller_motor_run(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t speed_us)
{
    if (s_lock == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    if (
        motor < MOTOR_DRUM ||
        motor > MOTOR_OUTPUT
    ) {

        return ESP_ERR_INVALID_ARG;
    }


    if (speed_us > TMS_SPEED_MAX_US) {

        return ESP_ERR_INVALID_ARG;
    }


    if (
        !tms_settings_is_valid_motor_direction(
            motor,
            direction
        )
    ) {

        return ESP_ERR_INVALID_ARG;
    }


    /*
     * Explicit zero speed behaves as individual stop.
     */
    if (speed_us == 0U) {

        return tms_controller_motor_stop(
            motor
        );
    }


    xSemaphoreTake(
        s_lock,
        portMAX_DELAY
    );


    /* --------------------------------------------------------
     * CARRIAGE HARD INTERLOCK
     * -------------------------------------------------------- */

    if (
        motor == MOTOR_CARRIAGE &&
        carriage_direction_blocked_locked(
            direction
        )
    ) {

        xSemaphoreGive(
            s_lock
        );


        ESP_LOGW(
            TAG,
            "Manual carriage %s rejected: endpoint limit active",
            tms_settings_direction_name(direction)
        );


        return ESP_ERR_INVALID_STATE;
    }


    /*
     * First manual command exits automatic mode.
     *
     * All previous automatic outputs are conceptually cleared because
     * apply_outputs_locked() below will only use s_manual[] in MANUAL mode.
     */
    if (s_mode != TMS_MODE_MANUAL) {

        clear_manual_locked();

        s_mode =
            TMS_MODE_MANUAL;
    }


    s_manual[motor].active =
        true;


    s_manual[motor].direction =
        direction;


    s_manual[motor].speed_us =
        speed_us;


    if (motor == MOTOR_CARRIAGE) {

        s_carriage_dir =
            (direction == TMS_SPEED_DIR_LEFT)
            ? TMS_CARRIAGE_LEFT
            : TMS_CARRIAGE_RIGHT;
    }


    esp_err_t err =
        apply_outputs_locked();


    xSemaphoreGive(
        s_lock
    );


    if (err == ESP_OK) {

        ESP_LOGI(
            TAG,
            "Manual RUN motor=%d dir=%s speed=%u us",
            (int)motor,
            tms_settings_direction_name(direction),
            (unsigned)speed_us
        );
    }


    return err;
}


/* ============================================================
 * MANUAL MOTOR STOP
 * ============================================================ */

esp_err_t tms_controller_motor_stop(
    motor_id_t motor)
{
    if (s_lock == NULL) {

        return ESP_ERR_INVALID_STATE;
    }


    if (
        motor < MOTOR_DRUM ||
        motor > MOTOR_OUTPUT
    ) {

        return ESP_ERR_INVALID_ARG;
    }


    xSemaphoreTake(
        s_lock,
        portMAX_DELAY
    );


    /*
     * Individual motor control and automatic mode are intentionally not
     * mixed. A motor_stop command during ULUR/TARIK safely exits automatic
     * operation and leaves all motors stopped.
     */
    if (
        s_mode == TMS_MODE_ULUR ||
        s_mode == TMS_MODE_TARIK
    ) {

        clear_manual_locked();

        s_mode =
            TMS_MODE_STOP;


        esp_err_t err =
            apply_outputs_locked();


        xSemaphoreGive(
            s_lock
        );


        ESP_LOGW(
            TAG,
            "Individual motor STOP requested during automatic mode -> global STOP"
        );


        return err;
    }


    if (s_mode == TMS_MODE_MANUAL) {

        s_manual[motor].active =
            false;

        s_manual[motor].speed_us =
            0;


        if (motor == MOTOR_CARRIAGE) {

            s_carriage_dir =
                TMS_CARRIAGE_STOP;
        }


        if (!any_manual_active_locked()) {

            s_mode =
                TMS_MODE_STOP;
        }
    }


    esp_err_t err =
        apply_outputs_locked();


    xSemaphoreGive(
        s_lock
    );


    if (err == ESP_OK) {

        ESP_LOGI(
            TAG,
            "Manual STOP motor=%d",
            (int)motor
        );
    }


    return err;
}


/* ============================================================
 * STATUS
 * ============================================================ */

void tms_controller_get_status(
    tms_status_t *status)
{
    if (status == NULL) {

        return;
    }


    memset(
        status,
        0,
        sizeof(*status)
    );


    if (s_lock != NULL) {

        xSemaphoreTake(
            s_lock,
            portMAX_DELAY
        );
    }


    status->mode =
        s_mode;


    status->carriage_dir =
        s_carriage_dir;


    status->left_limit =
        s_left_limit;


    status->right_limit =
        s_right_limit;


    for (
        int i = 0;
        i < MOTOR_COUNT;
        ++i
    ) {

        status->motor_pulse_us[i] =
            motor_pwm_get_us(
                i
            );


        status->manual[i] =
            s_manual[i];
    }


    if (s_lock != NULL) {

        xSemaphoreGive(
            s_lock
        );
    }
}


/* ============================================================
 * STRING HELPERS
 * ============================================================ */

const char *tms_mode_name(
    tms_mode_t mode)
{
    switch (mode) {

        case TMS_MODE_ULUR:

            return "ulur";


        case TMS_MODE_TARIK:

            return "tarik";


        case TMS_MODE_MANUAL:

            return "manual";


        case TMS_MODE_STOP:
        default:

            return "stop";
    }
}


const char *tms_carriage_dir_name(
    tms_carriage_dir_t dir)
{
    switch (dir) {

        case TMS_CARRIAGE_LEFT:

            return "left";


        case TMS_CARRIAGE_RIGHT:

            return "right";


        case TMS_CARRIAGE_STOP:
        default:

            return "stop";
    }
}
