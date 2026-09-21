#include "tms_settings.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "tms_settings";
static const char *NS = "tms_cfg";
static tms_settings_t s_cfg;

static uint16_t clamp_speed(uint16_t value)
{
    return value > TMS_SPEED_MAX_US ? TMS_SPEED_MAX_US : value;
}

esp_err_t tms_settings_init(void)
{
    s_cfg.drum_speed_us = TMS_DEFAULT_DRUM_SPEED_US;
    s_cfg.carriage_speed_us = TMS_DEFAULT_CARRIAGE_SPEED_US;
    s_cfg.output_speed_us = TMS_DEFAULT_OUTPUT_SPEED_US;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved settings; using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    uint16_t v;
    if (nvs_get_u16(h, "drum_spd", &v) == ESP_OK) s_cfg.drum_speed_us = clamp_speed(v);
    if (nvs_get_u16(h, "carr_spd", &v) == ESP_OK) s_cfg.carriage_speed_us = clamp_speed(v);
    if (nvs_get_u16(h, "out_spd", &v) == ESP_OK) s_cfg.output_speed_us = clamp_speed(v);
    nvs_close(h);

    ESP_LOGI(TAG, "Loaded speed: drum=%u carriage=%u output=%u us",
             s_cfg.drum_speed_us, s_cfg.carriage_speed_us, s_cfg.output_speed_us);
    return ESP_OK;
}

const tms_settings_t *tms_settings_get(void)
{
    return &s_cfg;
}

esp_err_t tms_settings_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    if ((err = nvs_set_u16(h, "drum_spd", s_cfg.drum_speed_us)) == ESP_OK &&
        (err = nvs_set_u16(h, "carr_spd", s_cfg.carriage_speed_us)) == ESP_OK &&
        (err = nvs_set_u16(h, "out_spd", s_cfg.output_speed_us)) == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t tms_settings_set_speed(motor_id_t motor, uint16_t speed_us, bool save_to_nvs)
{
    if (speed_us < TMS_SPEED_MIN_US || speed_us > TMS_SPEED_MAX_US) return ESP_ERR_INVALID_ARG;

    switch (motor) {
        case MOTOR_DRUM: s_cfg.drum_speed_us = speed_us; break;
        case MOTOR_CARRIAGE: s_cfg.carriage_speed_us = speed_us; break;
        case MOTOR_OUTPUT: s_cfg.output_speed_us = speed_us; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    return save_to_nvs ? tms_settings_save() : ESP_OK;
}
