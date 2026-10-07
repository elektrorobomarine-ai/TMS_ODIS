#include "command_handler.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "ethernet_w5500_config.h"
#include "motor_pwm.h"
#include "tms_controller.h"
#include "tms_settings.h"

#include "cJSON.h"

#include "esp_err.h"
#include "esp_log.h"


static const char *TAG = "command_handler";


/* ============================================================
 * STRING / ENUM HELPERS
 * ============================================================ */

static const char *motor_name(
    motor_id_t motor)
{
    switch (motor) {

        case MOTOR_DRUM:
            return "drum";

        case MOTOR_CARRIAGE:
            return "carriage";

        case MOTOR_OUTPUT:
            return "output";

        default:
            return "unknown";
    }
}


static bool parse_motor(
    const cJSON *item,
    motor_id_t *motor)
{
    if (
        item == NULL ||
        motor == NULL ||
        !cJSON_IsString(item) ||
        item->valuestring == NULL
    ) {

        return false;
    }


    const char *value =
        item->valuestring;


    if (
        strcmp(value, "drum") == 0 ||
        strcmp(value, "drum_main") == 0
    ) {

        *motor =
            MOTOR_DRUM;

        return true;
    }


    if (
        strcmp(value, "carriage") == 0 ||
        strcmp(value, "level_wind") == 0
    ) {

        *motor =
            MOTOR_CARRIAGE;

        return true;
    }


    if (
        strcmp(value, "output") == 0 ||
        strcmp(value, "cable_output") == 0
    ) {

        *motor =
            MOTOR_OUTPUT;

        return true;
    }


    return false;
}


static bool parse_direction(
    const cJSON *item,
    tms_speed_direction_t *direction)
{
    if (
        item == NULL ||
        direction == NULL ||
        !cJSON_IsString(item) ||
        item->valuestring == NULL
    ) {

        return false;
    }


    const char *value =
        item->valuestring;


    if (
        strcmp(value, "ulur") == 0 ||
        strcmp(value, "feed_out") == 0
    ) {

        *direction =
            TMS_SPEED_DIR_ULUR;

        return true;
    }


    if (
        strcmp(value, "tarik") == 0 ||
        strcmp(value, "retrieve") == 0
    ) {

        *direction =
            TMS_SPEED_DIR_TARIK;

        return true;
    }


    if (
        strcmp(value, "left") == 0 ||
        strcmp(value, "kiri") == 0
    ) {

        *direction =
            TMS_SPEED_DIR_LEFT;

        return true;
    }


    if (
        strcmp(value, "right") == 0 ||
        strcmp(value, "kanan") == 0
    ) {

        *direction =
            TMS_SPEED_DIR_RIGHT;

        return true;
    }


    return false;
}


/*
 * Validate a JSON integer and convert to uint16_t.
 *
 * cJSON stores numbers as double, so both range and integer-ness are checked
 * before conversion.
 */
static bool parse_speed_us(
    const cJSON *item,
    uint16_t *speed_us)
{
    if (
        item == NULL ||
        speed_us == NULL ||
        !cJSON_IsNumber(item)
    ) {

        return false;
    }


    const double raw =
        item->valuedouble;


    if (
        raw < (double)TMS_SPEED_MIN_US ||
        raw > (double)TMS_SPEED_MAX_US
    ) {

        return false;
    }


    const int integer_value =
        item->valueint;


    if (
        raw != (double)integer_value
    ) {

        return false;
    }


    *speed_us =
        (uint16_t)integer_value;


    return true;
}


/* ============================================================
 * JSON RESPONSE HELPERS
 * ============================================================ */

static char *copy_text(
    const char *text)
{
    if (text == NULL) {

        return NULL;
    }


    const size_t len =
        strlen(text);


    char *copy =
        malloc(len + 1U);


    if (copy == NULL) {

        return NULL;
    }


    memcpy(
        copy,
        text,
        len + 1U
    );


    return copy;
}


static void copy_request_id(
    cJSON *response,
    const cJSON *request_id)
{
    if (
        response == NULL ||
        request_id == NULL
    ) {

        return;
    }


    cJSON *copy =
        cJSON_Duplicate(
            request_id,
            true
        );


    if (copy != NULL) {

        cJSON_AddItemToObject(
            response,
            "id",
            copy
        );
    }
}


static char *print_and_delete(
    cJSON *root)
{
    if (root == NULL) {

        return copy_text(
            "{\"ok\":false,\"error\":{\"code\":\"out_of_memory\",\"message\":\"JSON allocation failed\"}}"
        );
    }


    char *text =
        cJSON_PrintUnformatted(
            root
        );


    cJSON_Delete(
        root
    );


    if (text == NULL) {

        return copy_text(
            "{\"ok\":false,\"error\":{\"code\":\"out_of_memory\",\"message\":\"JSON serialization failed\"}}"
        );
    }


    return text;
}


static char *make_error_response(
    const char *cmd,
    const cJSON *request_id,
    const char *code,
    const char *message)
{
    cJSON *root =
        cJSON_CreateObject();


    if (root == NULL) {

        return print_and_delete(NULL);
    }


    cJSON_AddBoolToObject(
        root,
        "ok",
        false
    );


    if (cmd != NULL) {

        cJSON_AddStringToObject(
            root,
            "cmd",
            cmd
        );
    }


    copy_request_id(
        root,
        request_id
    );


    cJSON *error =
        cJSON_AddObjectToObject(
            root,
            "error"
        );


    if (error != NULL) {

        cJSON_AddStringToObject(
            error,
            "code",
            code != NULL
                ? code
                : "error"
        );


        cJSON_AddStringToObject(
            error,
            "message",
            message != NULL
                ? message
                : "Command failed"
        );
    }


    return print_and_delete(
        root
    );
}


static const char *esp_error_code(
    esp_err_t err)
{
    switch (err) {

        case ESP_ERR_INVALID_ARG:
            return "invalid_argument";

        case ESP_ERR_INVALID_STATE:
            return "invalid_state";

        case ESP_ERR_NO_MEM:
            return "out_of_memory";

        case ESP_OK:
            return "ok";

        default:
            return "controller_error";
    }
}


static char *make_esp_error_response(
    const char *cmd,
    const cJSON *request_id,
    esp_err_t err,
    const char *context)
{
    char message[160];


    if (context != NULL) {

        snprintf(
            message,
            sizeof(message),
            "%s: %s",
            context,
            esp_err_to_name(err)
        );

    } else {

        snprintf(
            message,
            sizeof(message),
            "%s",
            esp_err_to_name(err)
        );
    }


    return make_error_response(
        cmd,
        request_id,
        esp_error_code(err),
        message
    );
}


/* ============================================================
 * STATUS BUILDING
 * ============================================================ */

static tms_speed_direction_t carriage_profile_direction(
    tms_carriage_dir_t dir)
{
    if (dir == TMS_CARRIAGE_LEFT) {

        return TMS_SPEED_DIR_LEFT;
    }


    return TMS_SPEED_DIR_RIGHT;
}


static uint16_t configured_speed_for(
    const tms_settings_t *settings,
    motor_id_t motor,
    tms_speed_direction_t direction)
{
    if (settings == NULL) {

        return 0U;
    }


    switch (motor) {

        case MOTOR_DRUM:

            return
                (direction == TMS_SPEED_DIR_TARIK)
                ? settings->drum_tarik_speed_us
                : settings->drum_ulur_speed_us;


        case MOTOR_CARRIAGE:

            return
                (direction == TMS_SPEED_DIR_LEFT)
                ? settings->carriage_left_speed_us
                : settings->carriage_right_speed_us;


        case MOTOR_OUTPUT:

            return
                (direction == TMS_SPEED_DIR_TARIK)
                ? settings->output_tarik_speed_us
                : settings->output_ulur_speed_us;


        default:

            return 0U;
    }
}


static bool motor_runtime_view(
    const tms_status_t *status,
    const tms_settings_t *settings,
    motor_id_t motor,
    bool *active,
    const char **direction_name,
    uint16_t *speed_us)
{
    if (
        status == NULL ||
        settings == NULL ||
        active == NULL ||
        direction_name == NULL ||
        speed_us == NULL
    ) {

        return false;
    }


    *active =
        false;


    *direction_name =
        "stop";


    *speed_us =
        0U;


    /* --------------------------------------------------------
     * MANUAL
     * -------------------------------------------------------- */

    if (status->mode == TMS_MODE_MANUAL) {

        if (status->manual[motor].active) {

            *active =
                true;


            *direction_name =
                tms_settings_direction_name(
                    status->manual[motor].direction
                );


            *speed_us =
                status->manual[motor].speed_us;
        }


        return true;
    }


    /* --------------------------------------------------------
     * AUTOMATIC ULUR / TARIK
     * -------------------------------------------------------- */

    if (
        status->mode == TMS_MODE_ULUR ||
        status->mode == TMS_MODE_TARIK
    ) {

        if (motor == MOTOR_CARRIAGE) {

            if (
                status->carriage_dir ==
                TMS_CARRIAGE_STOP
            ) {

                return true;
            }


            tms_speed_direction_t dir =
                carriage_profile_direction(
                    status->carriage_dir
                );


            *active =
                true;


            *direction_name =
                tms_settings_direction_name(
                    dir
                );


            *speed_us =
                configured_speed_for(
                    settings,
                    motor,
                    dir
                );


            return true;
        }


        const tms_speed_direction_t dir =
            (status->mode == TMS_MODE_TARIK)
            ? TMS_SPEED_DIR_TARIK
            : TMS_SPEED_DIR_ULUR;


        *active =
            true;


        *direction_name =
            tms_settings_direction_name(
                dir
            );


        *speed_us =
            configured_speed_for(
                settings,
                motor,
                dir
            );


        return true;
    }


    return true;
}


static cJSON *build_motor_runtime_json(
    const tms_status_t *status,
    const tms_settings_t *settings,
    motor_id_t motor)
{
    cJSON *obj =
        cJSON_CreateObject();


    if (obj == NULL) {

        return NULL;
    }


    bool active = false;
    const char *direction = "stop";
    uint16_t speed_us = 0U;


    (void)motor_runtime_view(
        status,
        settings,
        motor,
        &active,
        &direction,
        &speed_us
    );


    cJSON_AddBoolToObject(
        obj,
        "active",
        active
    );


    cJSON_AddStringToObject(
        obj,
        "direction",
        direction
    );


    cJSON_AddNumberToObject(
        obj,
        "speed_us",
        speed_us
    );


    cJSON_AddNumberToObject(
        obj,
        "pwm_us",
        status->motor_pulse_us[motor]
    );


    return obj;
}


static cJSON *build_status_data(void)
{
    tms_status_t status;
    tms_settings_t settings;


    tms_controller_get_status(
        &status
    );


    if (
        tms_settings_get_snapshot(
            &settings
        ) != ESP_OK
    ) {

        return NULL;
    }


    cJSON *data =
        cJSON_CreateObject();


    if (data == NULL) {

        return NULL;
    }


    /* --------------------------------------------------------
     * CONTROLLER IDENTITY
     * -------------------------------------------------------- */

    cJSON_AddStringToObject(
        data,
        "app",
        APP_NAME
    );


    cJSON_AddStringToObject(
        data,
        "version",
        APP_VERSION
    );


#ifdef TMS_NET_IPV4_ADDR
    cJSON_AddStringToObject(
        data,
        "ip",
        TMS_NET_IPV4_ADDR
    );
#endif


#ifdef TMS_TCP_SERVER_PORT
    cJSON_AddNumberToObject(
        data,
        "tcp_port",
        TMS_TCP_SERVER_PORT
    );
#endif


    /* --------------------------------------------------------
     * OPERATION STATUS
     * -------------------------------------------------------- */

    cJSON_AddStringToObject(
        data,
        "mode",
        tms_mode_name(status.mode)
    );


    cJSON_AddStringToObject(
        data,
        "carriage_direction",
        tms_carriage_dir_name(
            status.carriage_dir
        )
    );


    cJSON *limits =
        cJSON_AddObjectToObject(
            data,
            "limits"
        );


    if (limits != NULL) {

        cJSON_AddBoolToObject(
            limits,
            "left",
            status.left_limit
        );


        cJSON_AddBoolToObject(
            limits,
            "right",
            status.right_limit
        );
    }


    /* --------------------------------------------------------
     * PERSISTENT DIRECTIONAL SETTINGS
     * -------------------------------------------------------- */

    cJSON *settings_json =
        cJSON_AddObjectToObject(
            data,
            "settings"
        );


    if (settings_json != NULL) {

        cJSON *drum =
            cJSON_AddObjectToObject(
                settings_json,
                "drum"
            );


        if (drum != NULL) {

            cJSON_AddNumberToObject(
                drum,
                "ulur",
                settings.drum_ulur_speed_us
            );


            cJSON_AddNumberToObject(
                drum,
                "tarik",
                settings.drum_tarik_speed_us
            );
        }


        cJSON *carriage =
            cJSON_AddObjectToObject(
                settings_json,
                "carriage"
            );


        if (carriage != NULL) {

            cJSON_AddNumberToObject(
                carriage,
                "left",
                settings.carriage_left_speed_us
            );


            cJSON_AddNumberToObject(
                carriage,
                "right",
                settings.carriage_right_speed_us
            );
        }


        cJSON *output =
            cJSON_AddObjectToObject(
                settings_json,
                "output"
            );


        if (output != NULL) {

            cJSON_AddNumberToObject(
                output,
                "ulur",
                settings.output_ulur_speed_us
            );


            cJSON_AddNumberToObject(
                output,
                "tarik",
                settings.output_tarik_speed_us
            );
        }
    }


    /* --------------------------------------------------------
     * ACTUAL RUNTIME MOTOR STATE
     * -------------------------------------------------------- */

    cJSON *motors =
        cJSON_AddObjectToObject(
            data,
            "motor"
        );


    if (motors != NULL) {

        cJSON *drum =
            build_motor_runtime_json(
                &status,
                &settings,
                MOTOR_DRUM
            );


        if (drum != NULL) {

            cJSON_AddItemToObject(
                motors,
                "drum",
                drum
            );
        }


        cJSON *carriage =
            build_motor_runtime_json(
                &status,
                &settings,
                MOTOR_CARRIAGE
            );


        if (carriage != NULL) {

            cJSON_AddItemToObject(
                motors,
                "carriage",
                carriage
            );
        }


        cJSON *output =
            build_motor_runtime_json(
                &status,
                &settings,
                MOTOR_OUTPUT
            );


        if (output != NULL) {

            cJSON_AddItemToObject(
                motors,
                "output",
                output
            );
        }
    }


    /* --------------------------------------------------------
     * LEGACY COMPATIBILITY FOR v0.4 WEB/PYTHON GUI
     *
     * Old UI expects:
     *   speed_us.drum
     *   speed_us.carriage
     *   speed_us.output
     *   pwm_us.<motor>
     *
     * For drum/output we expose the profile matching the current
     * operation mode. In STOP/MANUAL fallback is ULUR.
     *
     * For carriage we expose the profile matching current/preferred
     * carriage direction.
     * -------------------------------------------------------- */

    tms_speed_direction_t op_profile =
        (status.mode == TMS_MODE_TARIK)
        ? TMS_SPEED_DIR_TARIK
        : TMS_SPEED_DIR_ULUR;


    tms_speed_direction_t carr_profile =
        carriage_profile_direction(
            status.carriage_dir
        );


    cJSON *legacy_speed =
        cJSON_AddObjectToObject(
            data,
            "speed_us"
        );


    if (legacy_speed != NULL) {

        cJSON_AddNumberToObject(
            legacy_speed,
            "drum",
            configured_speed_for(
                &settings,
                MOTOR_DRUM,
                op_profile
            )
        );


        cJSON_AddNumberToObject(
            legacy_speed,
            "carriage",
            configured_speed_for(
                &settings,
                MOTOR_CARRIAGE,
                carr_profile
            )
        );


        cJSON_AddNumberToObject(
            legacy_speed,
            "output",
            configured_speed_for(
                &settings,
                MOTOR_OUTPUT,
                op_profile
            )
        );
    }


    cJSON *legacy_pwm =
        cJSON_AddObjectToObject(
            data,
            "pwm_us"
        );


    if (legacy_pwm != NULL) {

        cJSON_AddNumberToObject(
            legacy_pwm,
            "drum",
            status.motor_pulse_us[MOTOR_DRUM]
        );


        cJSON_AddNumberToObject(
            legacy_pwm,
            "carriage",
            status.motor_pulse_us[MOTOR_CARRIAGE]
        );


        cJSON_AddNumberToObject(
            legacy_pwm,
            "output",
            status.motor_pulse_us[MOTOR_OUTPUT]
        );
    }


    return data;
}


static char *make_success_with_status(
    const char *cmd,
    const cJSON *request_id)
{
    cJSON *root =
        cJSON_CreateObject();


    if (root == NULL) {

        return print_and_delete(NULL);
    }


    cJSON_AddBoolToObject(
        root,
        "ok",
        true
    );


    if (cmd != NULL) {

        cJSON_AddStringToObject(
            root,
            "cmd",
            cmd
        );
    }


    copy_request_id(
        root,
        request_id
    );


    cJSON *data =
        build_status_data();


    if (data == NULL) {

        cJSON_Delete(
            root
        );


        return make_error_response(
            cmd,
            request_id,
            "status_error",
            "Failed to build controller status"
        );
    }


    cJSON_AddItemToObject(
        root,
        "data",
        data
    );


    return print_and_delete(
        root
    );
}


/* ============================================================
 * SET SPEED COMMAND
 * ============================================================ */

static esp_err_t set_one_profile(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t speed_us)
{
    esp_err_t err =
        tms_settings_set_speed(
            motor,
            direction,
            speed_us,
            true
        );


    if (err != ESP_OK) {

        return err;
    }


    /*
     * If this profile is currently used by automatic mode,
     * immediately refresh PWM outputs.
     */
    return tms_controller_apply_speed_change(
        motor
    );
}


static char *handle_set_speed(
    const char *cmd,
    const cJSON *request_id,
    const cJSON *root)
{
    motor_id_t motor;


    if (
        !parse_motor(
            cJSON_GetObjectItemCaseSensitive(
                root,
                "motor"
            ),
            &motor
        )
    ) {

        return make_error_response(
            cmd,
            request_id,
            "invalid_motor",
            "motor must be drum, carriage, or output"
        );
    }


    uint16_t speed_us;


    if (
        !parse_speed_us(
            cJSON_GetObjectItemCaseSensitive(
                root,
                "speed_us"
            ),
            &speed_us
        )
    ) {

        return make_error_response(
            cmd,
            request_id,
            "invalid_speed",
            "speed_us must be an integer from 0 to 500"
        );
    }


    const cJSON *direction_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "direction"
        );


    /* --------------------------------------------------------
     * NEW DIRECTIONAL FORMAT
     * -------------------------------------------------------- */

    if (direction_item != NULL) {

        tms_speed_direction_t direction;


        if (
            !parse_direction(
                direction_item,
                &direction
            )
        ) {

            return make_error_response(
                cmd,
                request_id,
                "invalid_direction",
                "direction must be ulur/tarik for drum/output or left/right for carriage"
            );
        }


        if (
            !tms_settings_is_valid_motor_direction(
                motor,
                direction
            )
        ) {

            return make_error_response(
                cmd,
                request_id,
                "invalid_motor_direction",
                "invalid motor/direction combination"
            );
        }


        esp_err_t err =
            set_one_profile(
                motor,
                direction,
                speed_us
            );


        if (err != ESP_OK) {

            return make_esp_error_response(
                cmd,
                request_id,
                err,
                "Failed to save directional speed"
            );
        }


        ESP_LOGI(
            TAG,
            "Persistent speed motor=%s dir=%s speed=%u us",
            motor_name(motor),
            tms_settings_direction_name(direction),
            (unsigned)speed_us
        );


        return make_success_with_status(
            cmd,
            request_id
        );
    }


    /* --------------------------------------------------------
     * LEGACY v0.3/v0.4 FORMAT
     *
     * {"cmd":"set_speed","motor":"drum","speed_us":250}
     *
     * To keep the old Web/Python GUI usable during migration,
     * apply the same value to both valid directions.
     * -------------------------------------------------------- */

    esp_err_t err;


    switch (motor) {

        case MOTOR_DRUM:

            err =
                tms_settings_set_speed(
                    MOTOR_DRUM,
                    TMS_SPEED_DIR_ULUR,
                    speed_us,
                    true
                );


            if (err == ESP_OK) {

                err =
                    tms_settings_set_speed(
                        MOTOR_DRUM,
                        TMS_SPEED_DIR_TARIK,
                        speed_us,
                        true
                    );
            }

            break;


        case MOTOR_CARRIAGE:

            err =
                tms_settings_set_speed(
                    MOTOR_CARRIAGE,
                    TMS_SPEED_DIR_LEFT,
                    speed_us,
                    true
                );


            if (err == ESP_OK) {

                err =
                    tms_settings_set_speed(
                        MOTOR_CARRIAGE,
                        TMS_SPEED_DIR_RIGHT,
                        speed_us,
                        true
                    );
            }

            break;


        case MOTOR_OUTPUT:

            err =
                tms_settings_set_speed(
                    MOTOR_OUTPUT,
                    TMS_SPEED_DIR_ULUR,
                    speed_us,
                    true
                );


            if (err == ESP_OK) {

                err =
                    tms_settings_set_speed(
                        MOTOR_OUTPUT,
                        TMS_SPEED_DIR_TARIK,
                        speed_us,
                        true
                    );
            }

            break;


        default:

            err =
                ESP_ERR_INVALID_ARG;

            break;
    }


    if (err == ESP_OK) {

        err =
            tms_controller_apply_speed_change(
                motor
            );
    }


    if (err != ESP_OK) {

        return make_esp_error_response(
            cmd,
            request_id,
            err,
            "Failed to save legacy speed"
        );
    }


    ESP_LOGW(
        TAG,
        "Legacy set_speed used for %s; applied %u us to both directions",
        motor_name(motor),
        (unsigned)speed_us
    );


    return make_success_with_status(
        cmd,
        request_id
    );
}


/* ============================================================
 * MANUAL RUN / STOP COMMANDS
 * ============================================================ */

static char *handle_motor_run(
    const char *cmd,
    const cJSON *request_id,
    const cJSON *root)
{
    motor_id_t motor;


    if (
        !parse_motor(
            cJSON_GetObjectItemCaseSensitive(
                root,
                "motor"
            ),
            &motor
        )
    ) {

        return make_error_response(
            cmd,
            request_id,
            "invalid_motor",
            "motor must be drum, carriage, or output"
        );
    }


    tms_speed_direction_t direction;


    if (
        !parse_direction(
            cJSON_GetObjectItemCaseSensitive(
                root,
                "direction"
            ),
            &direction
        )
    ) {

        return make_error_response(
            cmd,
            request_id,
            "invalid_direction",
            "direction must be ulur/tarik for drum/output or left/right for carriage"
        );
    }


    if (
        !tms_settings_is_valid_motor_direction(
            motor,
            direction
        )
    ) {

        return make_error_response(
            cmd,
            request_id,
            "invalid_motor_direction",
            "invalid motor/direction combination"
        );
    }


    uint16_t speed_us;


    if (
        !parse_speed_us(
            cJSON_GetObjectItemCaseSensitive(
                root,
                "speed_us"
            ),
            &speed_us
        )
    ) {

        return make_error_response(
            cmd,
            request_id,
            "invalid_speed",
            "speed_us must be an integer from 0 to 500"
        );
    }


    esp_err_t err =
        tms_controller_motor_run(
            motor,
            direction,
            speed_us
        );


    if (err != ESP_OK) {

        const char *context =
            (
                motor == MOTOR_CARRIAGE &&
                err == ESP_ERR_INVALID_STATE
            )
            ? "Manual carriage blocked by active limit/interlock"
            : "Manual motor run failed";


        return make_esp_error_response(
            cmd,
            request_id,
            err,
            context
        );
    }


    ESP_LOGI(
        TAG,
        "Manual command RUN motor=%s dir=%s speed=%u us",
        motor_name(motor),
        tms_settings_direction_name(direction),
        (unsigned)speed_us
    );


    return make_success_with_status(
        cmd,
        request_id
    );
}


static char *handle_motor_stop(
    const char *cmd,
    const cJSON *request_id,
    const cJSON *root)
{
    motor_id_t motor;


    if (
        !parse_motor(
            cJSON_GetObjectItemCaseSensitive(
                root,
                "motor"
            ),
            &motor
        )
    ) {

        return make_error_response(
            cmd,
            request_id,
            "invalid_motor",
            "motor must be drum, carriage, or output"
        );
    }


    esp_err_t err =
        tms_controller_motor_stop(
            motor
        );


    if (err != ESP_OK) {

        return make_esp_error_response(
            cmd,
            request_id,
            err,
            "Manual motor stop failed"
        );
    }


    ESP_LOGI(
        TAG,
        "Manual command STOP motor=%s",
        motor_name(motor)
    );


    return make_success_with_status(
        cmd,
        request_id
    );
}


/* ============================================================
 * COMMAND DISPATCH
 * ============================================================ */

char *command_handler_process(
    const char *json_text)
{
    if (json_text == NULL) {

        return make_error_response(
            NULL,
            NULL,
            "invalid_json",
            "JSON input is null"
        );
    }


    cJSON *root =
        cJSON_Parse(
            json_text
        );


    if (root == NULL) {

        return make_error_response(
            NULL,
            NULL,
            "invalid_json",
            "Malformed JSON"
        );
    }


    if (!cJSON_IsObject(root)) {

        cJSON_Delete(
            root
        );


        return make_error_response(
            NULL,
            NULL,
            "invalid_json",
            "Top-level JSON value must be an object"
        );
    }


    const cJSON *request_id =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "id"
        );


    const cJSON *cmd_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "cmd"
        );


    if (
        !cJSON_IsString(cmd_item) ||
        cmd_item->valuestring == NULL ||
        cmd_item->valuestring[0] == '\0'
    ) {

        char *response =
            make_error_response(
                NULL,
                request_id,
                "missing_command",
                "cmd must be a non-empty string"
            );


        cJSON_Delete(
            root
        );


        return response;
    }


    const char *cmd =
        cmd_item->valuestring;


    char *response = NULL;


    /* --------------------------------------------------------
     * PING
     * -------------------------------------------------------- */

    if (strcmp(cmd, "ping") == 0) {

        cJSON *reply =
            cJSON_CreateObject();


        if (reply != NULL) {

            cJSON_AddBoolToObject(
                reply,
                "ok",
                true
            );


            cJSON_AddStringToObject(
                reply,
                "cmd",
                "ping"
            );


            copy_request_id(
                reply,
                request_id
            );


            cJSON *data =
                cJSON_AddObjectToObject(
                    reply,
                    "data"
                );


            if (data != NULL) {

                cJSON_AddStringToObject(
                    data,
                    "reply",
                    "pong"
                );


                cJSON_AddStringToObject(
                    data,
                    "app",
                    APP_NAME
                );


                cJSON_AddStringToObject(
                    data,
                    "version",
                    APP_VERSION
                );
            }
        }


        response =
            print_and_delete(
                reply
            );
    }


    /* --------------------------------------------------------
     * STATUS
     * -------------------------------------------------------- */

    else if (
        strcmp(cmd, "status") == 0
    ) {

        response =
            make_success_with_status(
                cmd,
                request_id
            );
    }


    /* --------------------------------------------------------
     * AUTOMATIC CONTROL
     * -------------------------------------------------------- */

    else if (
        strcmp(cmd, "ulur") == 0
    ) {

        esp_err_t err =
            tms_controller_ulur();


        response =
            (err == ESP_OK)
            ? make_success_with_status(
                  cmd,
                  request_id
              )
            : make_esp_error_response(
                  cmd,
                  request_id,
                  err,
                  "ULUR command failed"
              );
    }


    else if (
        strcmp(cmd, "tarik") == 0
    ) {

        esp_err_t err =
            tms_controller_tarik();


        response =
            (err == ESP_OK)
            ? make_success_with_status(
                  cmd,
                  request_id
              )
            : make_esp_error_response(
                  cmd,
                  request_id,
                  err,
                  "TARIK command failed"
              );
    }


    else if (
        strcmp(cmd, "stop") == 0
    ) {

        esp_err_t err =
            tms_controller_stop();


        response =
            (err == ESP_OK)
            ? make_success_with_status(
                  cmd,
                  request_id
              )
            : make_esp_error_response(
                  cmd,
                  request_id,
                  err,
                  "STOP command failed"
              );
    }


    else if (
        strcmp(cmd, "carriage_reverse") == 0
    ) {

        esp_err_t err =
            tms_controller_reverse_carriage();


        response =
            (err == ESP_OK)
            ? make_success_with_status(
                  cmd,
                  request_id
              )
            : make_esp_error_response(
                  cmd,
                  request_id,
                  err,
                  "Carriage reverse failed"
              );
    }


    /* --------------------------------------------------------
     * PERSISTENT NVS SETTINGS
     * -------------------------------------------------------- */

    else if (
        strcmp(cmd, "set_speed") == 0
    ) {

        response =
            handle_set_speed(
                cmd,
                request_id,
                root
            );
    }


    else if (
        strcmp(cmd, "restore_defaults") == 0
    ) {

        esp_err_t err =
            tms_settings_restore_defaults(
                true
            );


        if (err == ESP_OK) {

            /*
             * Refresh whichever automatic profile is currently active.
             */
            err =
                tms_controller_apply_speed_change(
                    MOTOR_DRUM
                );
        }


        response =
            (err == ESP_OK)
            ? make_success_with_status(
                  cmd,
                  request_id
              )
            : make_esp_error_response(
                  cmd,
                  request_id,
                  err,
                  "Restore defaults failed"
              );
    }


    /* --------------------------------------------------------
     * MANUAL CONTROL
     * -------------------------------------------------------- */

    else if (
        strcmp(cmd, "motor_run") == 0
    ) {

        response =
            handle_motor_run(
                cmd,
                request_id,
                root
            );
    }


    else if (
        strcmp(cmd, "motor_stop") == 0
    ) {

        response =
            handle_motor_stop(
                cmd,
                request_id,
                root
            );
    }


    /* --------------------------------------------------------
     * UNKNOWN
     * -------------------------------------------------------- */

    else {

        response =
            make_error_response(
                cmd,
                request_id,
                "unknown_command",
                "Unknown command"
            );
    }


    cJSON_Delete(
        root
    );


    if (response == NULL) {

        response =
            copy_text(
                "{\"ok\":false,\"error\":{\"code\":\"out_of_memory\",\"message\":\"Response allocation failed\"}}"
            );
    }


    return response;
}
