#include "tms_settings.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "tms_settings";

/*
 * Keep the same namespace as v0.3.0 so an already deployed device can
 * migrate its previous settings without losing them.
 */
static const char *NVS_NAMESPACE = "tms_cfg";

/* New directional NVS keys. Keep names <= NVS key length limits. */
#define NVS_KEY_DRUM_ULUR      "drum_ulur"
#define NVS_KEY_DRUM_TARIK     "drum_tarik"

#define NVS_KEY_CARR_LEFT      "carr_left"
#define NVS_KEY_CARR_RIGHT     "carr_right"

#define NVS_KEY_OUT_ULUR       "out_ulur"
#define NVS_KEY_OUT_TARIK      "out_tarik"

/*
 * Legacy v0.3.0 keys.
 * These are read only for migration compatibility.
 */
#define NVS_KEY_OLD_DRUM       "drum_spd"
#define NVS_KEY_OLD_CARR       "carr_spd"
#define NVS_KEY_OLD_OUTPUT     "out_spd"

static tms_settings_t s_cfg;
static SemaphoreHandle_t s_lock = NULL;
static bool s_initialized = false;


/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static uint16_t clamp_speed(uint16_t value)
{
    return (value > TMS_SPEED_MAX_US) ? TMS_SPEED_MAX_US : value;
}


static void load_defaults_locked(void)
{
    s_cfg.drum_ulur_speed_us = TMS_DEFAULT_DRUM_ULUR_SPEED_US;
    s_cfg.drum_tarik_speed_us = TMS_DEFAULT_DRUM_TARIK_SPEED_US;

    s_cfg.carriage_left_speed_us = TMS_DEFAULT_CARR_LEFT_SPEED_US;
    s_cfg.carriage_right_speed_us = TMS_DEFAULT_CARR_RIGHT_SPEED_US;

    s_cfg.output_ulur_speed_us = TMS_DEFAULT_OUTPUT_ULUR_SPEED_US;
    s_cfg.output_tarik_speed_us = TMS_DEFAULT_OUTPUT_TARIK_SPEED_US;
}


bool tms_settings_is_valid_motor_direction(
    motor_id_t motor,
    tms_speed_direction_t direction)
{
    switch (motor) {
        case MOTOR_DRUM:
        case MOTOR_OUTPUT:
            return (direction == TMS_SPEED_DIR_ULUR ||
                    direction == TMS_SPEED_DIR_TARIK);

        case MOTOR_CARRIAGE:
            return (direction == TMS_SPEED_DIR_LEFT ||
                    direction == TMS_SPEED_DIR_RIGHT);

        default:
            return false;
    }
}


static uint16_t *field_ptr_locked(
    motor_id_t motor,
    tms_speed_direction_t direction)
{
    if (!tms_settings_is_valid_motor_direction(motor, direction)) {
        return NULL;
    }

    switch (motor) {
        case MOTOR_DRUM:
            return (direction == TMS_SPEED_DIR_ULUR)
                ? &s_cfg.drum_ulur_speed_us
                : &s_cfg.drum_tarik_speed_us;

        case MOTOR_CARRIAGE:
            return (direction == TMS_SPEED_DIR_LEFT)
                ? &s_cfg.carriage_left_speed_us
                : &s_cfg.carriage_right_speed_us;

        case MOTOR_OUTPUT:
            return (direction == TMS_SPEED_DIR_ULUR)
                ? &s_cfg.output_ulur_speed_us
                : &s_cfg.output_tarik_speed_us;

        default:
            return NULL;
    }
}


static const char *nvs_key_for(
    motor_id_t motor,
    tms_speed_direction_t direction)
{
    if (!tms_settings_is_valid_motor_direction(motor, direction)) {
        return NULL;
    }

    switch (motor) {
        case MOTOR_DRUM:
            return (direction == TMS_SPEED_DIR_ULUR)
                ? NVS_KEY_DRUM_ULUR
                : NVS_KEY_DRUM_TARIK;

        case MOTOR_CARRIAGE:
            return (direction == TMS_SPEED_DIR_LEFT)
                ? NVS_KEY_CARR_LEFT
                : NVS_KEY_CARR_RIGHT;

        case MOTOR_OUTPUT:
            return (direction == TMS_SPEED_DIR_ULUR)
                ? NVS_KEY_OUT_ULUR
                : NVS_KEY_OUT_TARIK;

        default:
            return NULL;
    }
}


static esp_err_t save_one_locked(
    motor_id_t motor,
    tms_speed_direction_t direction)
{
    const char *key = nvs_key_for(motor, direction);
    uint16_t *field = field_ptr_locked(motor, direction);

    if (key == NULL || field == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u16(h, key, *field);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);
    return err;
}


static esp_err_t save_all_locked(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u16(h, NVS_KEY_DRUM_ULUR, s_cfg.drum_ulur_speed_us);
    if (err == ESP_OK) {
        err = nvs_set_u16(h, NVS_KEY_DRUM_TARIK, s_cfg.drum_tarik_speed_us);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, NVS_KEY_CARR_LEFT, s_cfg.carriage_left_speed_us);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, NVS_KEY_CARR_RIGHT, s_cfg.carriage_right_speed_us);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, NVS_KEY_OUT_ULUR, s_cfg.output_ulur_speed_us);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, NVS_KEY_OUT_TARIK, s_cfg.output_tarik_speed_us);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);
    return err;
}


/*
 * Read a new directional key.
 *
 * If it does not exist, optionally try a legacy v0.3.0 key.
 * When a legacy key is used, *migrated is set true so init() can persist
 * the new six-key layout afterward.
 */
static esp_err_t load_profile(
    nvs_handle_t h,
    const char *new_key,
    const char *legacy_key,
    uint16_t default_value,
    uint16_t *destination,
    bool *migrated)
{
    if (new_key == NULL || destination == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t value = default_value;

    esp_err_t err = nvs_get_u16(h, new_key, &value);
    if (err == ESP_OK) {
        *destination = clamp_speed(value);
        return ESP_OK;
    }

    if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    if (legacy_key != NULL) {
        err = nvs_get_u16(h, legacy_key, &value);
        if (err == ESP_OK) {
            *destination = clamp_speed(value);

            if (migrated != NULL) {
                *migrated = true;
            }

            ESP_LOGI(
                TAG,
                "Migrating legacy NVS key '%s' -> '%s' (%u us)",
                legacy_key,
                new_key,
                (unsigned)*destination
            );

            return ESP_OK;
        }

        if (err != ESP_ERR_NVS_NOT_FOUND) {
            return err;
        }
    }

    /* Missing setting is not an error; keep compile-time default. */
    *destination = clamp_speed(default_value);
    return ESP_OK;
}


/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t tms_settings_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    load_defaults_locked();
    xSemaphoreGive(s_lock);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /*
         * Namespace does not exist yet. Defaults remain active.
         * Do not write flash unnecessarily until user changes a setting.
         */
        s_initialized = true;

        ESP_LOGI(
            TAG,
            "No saved TMS settings; using defaults: "
            "drum(U=%u,T=%u) carriage(L=%u,R=%u) output(U=%u,T=%u) us",
            (unsigned)s_cfg.drum_ulur_speed_us,
            (unsigned)s_cfg.drum_tarik_speed_us,
            (unsigned)s_cfg.carriage_left_speed_us,
            (unsigned)s_cfg.carriage_right_speed_us,
            (unsigned)s_cfg.output_ulur_speed_us,
            (unsigned)s_cfg.output_tarik_speed_us
        );

        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(read) failed: %s", esp_err_to_name(err));
        return err;
    }

    bool migrated = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    /*
     * For legacy migration:
     *   old drum_spd -> both drum ULUR and TARIK
     *   old carr_spd -> both carriage LEFT and RIGHT
     *   old out_spd  -> both output ULUR and TARIK
     */
    err = load_profile(
        h,
        NVS_KEY_DRUM_ULUR,
        NVS_KEY_OLD_DRUM,
        TMS_DEFAULT_DRUM_ULUR_SPEED_US,
        &s_cfg.drum_ulur_speed_us,
        &migrated
    );

    if (err == ESP_OK) {
        err = load_profile(
            h,
            NVS_KEY_DRUM_TARIK,
            NVS_KEY_OLD_DRUM,
            TMS_DEFAULT_DRUM_TARIK_SPEED_US,
            &s_cfg.drum_tarik_speed_us,
            &migrated
        );
    }

    if (err == ESP_OK) {
        err = load_profile(
            h,
            NVS_KEY_CARR_LEFT,
            NVS_KEY_OLD_CARR,
            TMS_DEFAULT_CARR_LEFT_SPEED_US,
            &s_cfg.carriage_left_speed_us,
            &migrated
        );
    }

    if (err == ESP_OK) {
        err = load_profile(
            h,
            NVS_KEY_CARR_RIGHT,
            NVS_KEY_OLD_CARR,
            TMS_DEFAULT_CARR_RIGHT_SPEED_US,
            &s_cfg.carriage_right_speed_us,
            &migrated
        );
    }

    if (err == ESP_OK) {
        err = load_profile(
            h,
            NVS_KEY_OUT_ULUR,
            NVS_KEY_OLD_OUTPUT,
            TMS_DEFAULT_OUTPUT_ULUR_SPEED_US,
            &s_cfg.output_ulur_speed_us,
            &migrated
        );
    }

    if (err == ESP_OK) {
        err = load_profile(
            h,
            NVS_KEY_OUT_TARIK,
            NVS_KEY_OLD_OUTPUT,
            TMS_DEFAULT_OUTPUT_TARIK_SPEED_US,
            &s_cfg.output_tarik_speed_us,
            &migrated
        );
    }

    xSemaphoreGive(s_lock);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed loading TMS settings: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "Loaded speed profiles: "
        "drum(U=%u,T=%u) carriage(L=%u,R=%u) output(U=%u,T=%u) us",
        (unsigned)s_cfg.drum_ulur_speed_us,
        (unsigned)s_cfg.drum_tarik_speed_us,
        (unsigned)s_cfg.carriage_left_speed_us,
        (unsigned)s_cfg.carriage_right_speed_us,
        (unsigned)s_cfg.output_ulur_speed_us,
        (unsigned)s_cfg.output_tarik_speed_us
    );

    /*
     * Persist migrated values under the new keys.
     * Old keys are intentionally left untouched; they are harmless and make
     * rollback to older firmware less surprising.
     */
    if (migrated) {
        err = tms_settings_save();
        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Legacy settings loaded but migration save failed: %s",
                esp_err_to_name(err)
            );
            return err;
        }

        ESP_LOGI(TAG, "Legacy TMS settings migrated to directional NVS keys");
    }

    return ESP_OK;
}


const tms_settings_t *tms_settings_get(void)
{
    return &s_cfg;
}


esp_err_t tms_settings_get_snapshot(tms_settings_t *out_settings)
{
    if (out_settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(out_settings, &s_cfg, sizeof(*out_settings));
    xSemaphoreGive(s_lock);

    return ESP_OK;
}


esp_err_t tms_settings_get_speed(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t *speed_us)
{
    if (speed_us == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!tms_settings_is_valid_motor_direction(motor, direction)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    uint16_t *field = field_ptr_locked(motor, direction);
    if (field == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }

    *speed_us = *field;

    xSemaphoreGive(s_lock);
    return ESP_OK;
}


esp_err_t tms_settings_set_speed(
    motor_id_t motor,
    tms_speed_direction_t direction,
    uint16_t speed_us,
    bool save_to_nvs)
{
    /*
     * speed_us is uint16_t, so a "speed_us < 0" test would always be false
     * and ESP-IDF builds with -Werror can reject it.
     */
    if (speed_us > TMS_SPEED_MAX_US) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!tms_settings_is_valid_motor_direction(motor, direction)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    uint16_t *field = field_ptr_locked(motor, direction);
    if (field == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t old_value = *field;
    *field = speed_us;

    esp_err_t err = ESP_OK;

    if (save_to_nvs) {
        err = save_one_locked(motor, direction);

        /*
         * If persistence fails, restore RAM to previous value so RAM and NVS
         * do not silently disagree about the requested persistent setting.
         */
        if (err != ESP_OK) {
            *field = old_value;
        }
    }

    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Set %s motor=%d speed=%u us%s",
            tms_settings_direction_name(direction),
            (int)motor,
            (unsigned)speed_us,
            save_to_nvs ? " [NVS]" : " [RAM]"
        );
    } else {
        ESP_LOGE(
            TAG,
            "Failed saving motor=%d dir=%s speed=%u: %s",
            (int)motor,
            tms_settings_direction_name(direction),
            (unsigned)speed_us,
            esp_err_to_name(err)
        );
    }

    return err;
}


esp_err_t tms_settings_save(void)
{
    if (s_lock == NULL || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = save_all_locked();
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving all TMS settings failed: %s", esp_err_to_name(err));
    }

    return err;
}


esp_err_t tms_settings_restore_defaults(bool save_to_nvs)
{
    if (s_lock == NULL || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    tms_settings_t previous = s_cfg;
    load_defaults_locked();

    esp_err_t err = ESP_OK;

    if (save_to_nvs) {
        err = save_all_locked();

        if (err != ESP_OK) {
            s_cfg = previous;
        }
    }

    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Directional speed profiles restored to defaults%s",
            save_to_nvs ? " [NVS]" : " [RAM]"
        );
    }

    return err;
}


const char *tms_settings_direction_name(tms_speed_direction_t direction)
{
    switch (direction) {
        case TMS_SPEED_DIR_ULUR:
            return "ulur";

        case TMS_SPEED_DIR_TARIK:
            return "tarik";

        case TMS_SPEED_DIR_LEFT:
            return "left";

        case TMS_SPEED_DIR_RIGHT:
            return "right";

        default:
            return "unknown";
    }
}
