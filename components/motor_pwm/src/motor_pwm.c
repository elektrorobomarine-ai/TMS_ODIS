#include "motor_pwm.h"

#include "app_config.h"
#include "board_config.h"
#include "driver/mcpwm_timer.h"
#include "driver/mcpwm_oper.h"
#include "driver/mcpwm_cmpr.h"
#include "driver/mcpwm_gen.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdbool.h>

static const char *TAG = "motor_pwm";

static const int s_pwm_gpio[MOTOR_COUNT] = {
    MOTOR_DRUM_PWM_GPIO, MOTOR_CARRIAGE_PWM_GPIO, MOTOR_OUTPUT_PWM_GPIO
};

static mcpwm_timer_handle_t s_timer = NULL;
static mcpwm_oper_handle_t s_operator[2] = {NULL, NULL};
static mcpwm_cmpr_handle_t s_comparator[MOTOR_COUNT] = {0};
static mcpwm_gen_handle_t s_generator[MOTOR_COUNT] = {0};
static uint16_t s_pulse_us[MOTOR_COUNT] = {0};
static SemaphoreHandle_t s_mutex = NULL;

static bool pulse_valid(uint16_t pulse_us)
{
    return pulse_us >= MOTOR_PWM_MIN_US && pulse_us <= MOTOR_PWM_MAX_US;
}

esp_err_t motor_pwm_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MOTOR_PWM_RESOLUTION_HZ,
        .period_ticks = MOTOR_PWM_PERIOD_US,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_timer(&timer_config, &s_timer), TAG, "new timer failed");

    for (int op = 0; op < 2; ++op) {
        mcpwm_operator_config_t operator_config = {
            .group_id = 0,
        };
        ESP_RETURN_ON_ERROR(mcpwm_new_operator(&operator_config, &s_operator[op]), TAG, "new operator failed");
        ESP_RETURN_ON_ERROR(mcpwm_operator_connect_timer(s_operator[op], s_timer), TAG, "connect timer failed");
    }

    for (int i = 0; i < MOTOR_COUNT; ++i) {
        mcpwm_comparator_config_t cmp_config = {
            .flags.update_cmp_on_tez = true,
        };
        ESP_RETURN_ON_ERROR(mcpwm_new_comparator(s_operator[i / 2], &cmp_config, &s_comparator[i]), TAG, "new comparator failed");

        mcpwm_generator_config_t gen_config = {
            .gen_gpio_num = s_pwm_gpio[i],
        };
        ESP_RETURN_ON_ERROR(mcpwm_new_generator(s_operator[i / 2], &gen_config, &s_generator[i]), TAG, "new generator failed");

        ESP_RETURN_ON_ERROR(mcpwm_comparator_set_compare_value(s_comparator[i], MOTOR_PWM_SAFE_US), TAG, "set compare failed");
        ESP_RETURN_ON_ERROR(
            mcpwm_generator_set_action_on_timer_event(
                s_generator[i],
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                             MCPWM_TIMER_EVENT_EMPTY,
                                             MCPWM_GEN_ACTION_HIGH)),
            TAG, "timer action failed");
        ESP_RETURN_ON_ERROR(
            mcpwm_generator_set_action_on_compare_event(
                s_generator[i],
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                               s_comparator[i],
                                               MCPWM_GEN_ACTION_LOW)),
            TAG, "compare action failed");

        s_pulse_us[i] = MOTOR_PWM_SAFE_US;
    }

    ESP_RETURN_ON_ERROR(mcpwm_timer_enable(s_timer), TAG, "timer enable failed");
    ESP_RETURN_ON_ERROR(mcpwm_timer_start_stop(s_timer, MCPWM_TIMER_START_NO_STOP), TAG, "timer start failed");

    ESP_LOGI(TAG, "3-channel TMS RC PWM ready: %d..%d us, neutral=%d us",
             MOTOR_PWM_MIN_US, MOTOR_PWM_MAX_US, MOTOR_PWM_NEUTRAL_US);
    return ESP_OK;
}

esp_err_t motor_pwm_set_us(uint8_t motor_index, uint16_t pulse_us)
{
    if (motor_index >= MOTOR_COUNT || !pulse_valid(pulse_us)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mutex || !s_comparator[motor_index]) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = mcpwm_comparator_set_compare_value(s_comparator[motor_index], pulse_us);
    if (err == ESP_OK) {
        s_pulse_us[motor_index] = pulse_us;
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t motor_pwm_set_all(const uint16_t pulse_us[MOTOR_COUNT])
{
    if (!pulse_us) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < MOTOR_COUNT; ++i) {
        if (!pulse_valid(pulse_us[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (int i = 0; i < MOTOR_COUNT; ++i) {
        esp_err_t err = motor_pwm_set_us(i, pulse_us[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t motor_pwm_set_safe(uint8_t motor_index)
{
    return motor_pwm_set_us(motor_index, MOTOR_PWM_SAFE_US);
}

esp_err_t motor_pwm_set_all_safe(void)
{
    const uint16_t safe[MOTOR_COUNT] = {
        MOTOR_PWM_SAFE_US, MOTOR_PWM_SAFE_US, MOTOR_PWM_SAFE_US
    };
    return motor_pwm_set_all(safe);
}

uint16_t motor_pwm_get_us(uint8_t motor_index)
{
    if (motor_index >= MOTOR_COUNT) {
        return 0;
    }
    uint16_t value = 0;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        value = s_pulse_us[motor_index];
        xSemaphoreGive(s_mutex);
    }
    return value;
}
