#include "tms_controller.h"
#include "app_config.h"
#include "board_config.h"
#include "limit_input.h"
#include "tms_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "tms_controller";

typedef enum { EVT_LIMIT = 1 } event_type_t;
typedef struct { event_type_t type; uint8_t index; bool active; } tms_event_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static tms_mode_t s_mode = TMS_MODE_STOP;
static tms_carriage_dir_t s_carriage_dir = TMS_CARRIAGE_RIGHT;
static bool s_left_limit = false;
static bool s_right_limit = false;

static uint16_t pulse_from_direction(int logical_dir, int polarity, uint16_t speed)
{
    int value = MOTOR_PWM_NEUTRAL_US + (logical_dir * polarity * (int)speed);
    if (value < MOTOR_PWM_MIN_US) value = MOTOR_PWM_MIN_US;
    if (value > MOTOR_PWM_MAX_US) value = MOTOR_PWM_MAX_US;
    return (uint16_t)value;
}

static esp_err_t apply_outputs_locked(void)
{
    const tms_settings_t *cfg = tms_settings_get();
    uint16_t out[MOTOR_COUNT] = {MOTOR_PWM_NEUTRAL_US, MOTOR_PWM_NEUTRAL_US, MOTOR_PWM_NEUTRAL_US};

    if (s_mode != TMS_MODE_STOP) {
        int drum_dir = (s_mode == TMS_MODE_ULUR) ? DRUM_DIR_ULUR : DRUM_DIR_TARIK;
        int output_dir = (s_mode == TMS_MODE_ULUR) ? OUTPUT_DIR_ULUR : OUTPUT_DIR_TARIK;
        int carriage_dir = (s_carriage_dir == TMS_CARRIAGE_LEFT) ? CARRIAGE_DIR_LEFT : CARRIAGE_DIR_RIGHT;

        out[MOTOR_DRUM] = pulse_from_direction(drum_dir, DRUM_DIRECTION_POLARITY, cfg->drum_speed_us);
        out[MOTOR_OUTPUT] = pulse_from_direction(output_dir, OUTPUT_DIRECTION_POLARITY, cfg->output_speed_us);
        out[MOTOR_CARRIAGE] = pulse_from_direction(carriage_dir, CARRIAGE_DIRECTION_POLARITY, cfg->carriage_speed_us);
    }
    return motor_pwm_set_all(out);
}

static void limit_callback(uint8_t index, const limit_input_state_t *state, void *ctx)
{
    (void)ctx;
    if (!s_queue || !state || index > 1) return;
    tms_event_t evt = {.type = EVT_LIMIT, .index = index, .active = state->active};
    xQueueSend(s_queue, &evt, 0);
}

static void controller_task(void *arg)
{
    (void)arg;
    tms_event_t evt;
    while (true) {
        if (xQueueReceive(s_queue, &evt, portMAX_DELAY) != pdTRUE) continue;
        if (evt.type != EVT_LIMIT) continue;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (evt.index == 0) s_left_limit = evt.active;
        if (evt.index == 1) s_right_limit = evt.active;

        if (evt.active && s_mode != TMS_MODE_STOP) {
            if (evt.index == 0 && s_carriage_dir == TMS_CARRIAGE_LEFT) {
                s_carriage_dir = TMS_CARRIAGE_RIGHT;
                ESP_LOGI(TAG, "Left limit active -> carriage RIGHT");
                apply_outputs_locked();
            } else if (evt.index == 1 && s_carriage_dir == TMS_CARRIAGE_RIGHT) {
                s_carriage_dir = TMS_CARRIAGE_LEFT;
                ESP_LOGI(TAG, "Right limit active -> carriage LEFT");
                apply_outputs_locked();
            }
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t tms_controller_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(TMS_CTRL_EVENT_QUEUE_LEN, sizeof(tms_event_t));
    if (!s_lock || !s_queue) return ESP_ERR_NO_MEM;

    limit_input_state_t st = {0};
    if (limit_input_get(0, &st) == ESP_OK) s_left_limit = st.active;
    if (limit_input_get(1, &st) == ESP_OK) s_right_limit = st.active;

    /* Start by moving away from an already-active endpoint. */
    if (s_left_limit && !s_right_limit) s_carriage_dir = TMS_CARRIAGE_RIGHT;
    else if (s_right_limit && !s_left_limit) s_carriage_dir = TMS_CARRIAGE_LEFT;

    limit_input_set_callback(limit_callback, NULL);
    if (xTaskCreate(controller_task, "tms_ctrl", TMS_CTRL_TASK_STACK_SIZE, NULL,
                    TMS_CTRL_TASK_PRIORITY, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return motor_pwm_set_all_safe();
}

static esp_err_t set_mode(tms_mode_t mode)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_mode = mode;
    if (s_left_limit && s_carriage_dir == TMS_CARRIAGE_LEFT) s_carriage_dir = TMS_CARRIAGE_RIGHT;
    if (s_right_limit && s_carriage_dir == TMS_CARRIAGE_RIGHT) s_carriage_dir = TMS_CARRIAGE_LEFT;
    esp_err_t err = apply_outputs_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t tms_controller_stop(void) { return set_mode(TMS_MODE_STOP); }
esp_err_t tms_controller_ulur(void) { return set_mode(TMS_MODE_ULUR); }
esp_err_t tms_controller_tarik(void) { return set_mode(TMS_MODE_TARIK); }

esp_err_t tms_controller_reverse_carriage(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_carriage_dir == TMS_CARRIAGE_LEFT) s_carriage_dir = TMS_CARRIAGE_RIGHT;
    else s_carriage_dir = TMS_CARRIAGE_LEFT;

    /* Never command travel farther into an active limit. */
    if (s_left_limit && s_carriage_dir == TMS_CARRIAGE_LEFT) s_carriage_dir = TMS_CARRIAGE_RIGHT;
    if (s_right_limit && s_carriage_dir == TMS_CARRIAGE_RIGHT) s_carriage_dir = TMS_CARRIAGE_LEFT;
    esp_err_t err = apply_outputs_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t tms_controller_apply_speed_change(motor_id_t motor)
{
    (void)motor;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = apply_outputs_locked();
    xSemaphoreGive(s_lock);
    return err;
}

void tms_controller_get_status(tms_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    status->mode = s_mode;
    status->carriage_dir = s_carriage_dir;
    status->left_limit = s_left_limit;
    status->right_limit = s_right_limit;
    for (int i = 0; i < MOTOR_COUNT; ++i) status->motor_pulse_us[i] = motor_pwm_get_us(i);
    if (s_lock) xSemaphoreGive(s_lock);
}

const char *tms_mode_name(tms_mode_t mode)
{
    switch (mode) {
        case TMS_MODE_ULUR: return "ulur";
        case TMS_MODE_TARIK: return "tarik";
        default: return "stop";
    }
}

const char *tms_carriage_dir_name(tms_carriage_dir_t dir)
{
    switch (dir) {
        case TMS_CARRIAGE_LEFT: return "left";
        case TMS_CARRIAGE_RIGHT: return "right";
        default: return "stop";
    }
}
