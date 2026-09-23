#include "app_config.h"
#include "ethernet_w5500.h"
#include "limit_input.h"
#include "motor_pwm.h"
#include "status_led.h"
#include "tcp_json_server.h"
#include "tms_settings.h"
#include "tms_controller.h"
#include "web_server.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s starting", APP_NAME, APP_VERSION);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if ((err = status_led_init()) != ESP_OK) goto fail;
    status_led_set_system(STATUS_LED_BOOT);
    if ((err = motor_pwm_init()) != ESP_OK) goto fail;
    if ((err = limit_input_init()) != ESP_OK) goto fail;
    if ((err = tms_settings_init()) != ESP_OK) goto fail;
    if ((err = tms_controller_init()) != ESP_OK) goto fail;

    status_led_set_system(STATUS_LED_NET_WAIT);
    if ((err = ethernet_w5500_init()) != ESP_OK) goto fail;
    if ((err = tcp_json_server_start()) != ESP_OK) goto fail;

    /* Web UI is an additional control surface. Keep TCP control alive even if
     * the HTTP server cannot start, but report the failure clearly. */
    esp_err_t web_err = web_server_start();
    if (web_err != ESP_OK) {
        ESP_LOGE(TAG, "Web UI failed to start: %s", esp_err_to_name(web_err));
    }

    ESP_LOGI(TAG, "TMS controller initialization complete");
    return;

fail:
    ESP_LOGE(TAG, "Initialization failed: %s", esp_err_to_name(err));
    motor_pwm_set_all_safe();
    status_led_set_system(STATUS_LED_ERROR);
}
