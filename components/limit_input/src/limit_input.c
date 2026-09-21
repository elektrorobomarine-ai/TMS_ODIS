#include "limit_input.h"

#include "app_config.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "limit_input";

static const gpio_num_t s_limit_gpio[LIMIT_COUNT] = {
    LIMIT_CARRIAGE_LEFT_GPIO, LIMIT_CARRIAGE_RIGHT_GPIO, LIMIT_SPARE1_GPIO, LIMIT_SPARE2_GPIO
};

static QueueHandle_t s_queue = NULL;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static limit_input_state_t s_state[LIMIT_COUNT] = {0};
static limit_input_callback_t s_callback = NULL;
static void *s_callback_ctx = NULL;

static bool read_active(uint8_t index)
{
    int level = gpio_get_level(s_limit_gpio[index]);
    return level == LIMIT_ACTIVE_LEVEL;
}

static void IRAM_ATTR gpio_isr(void *arg)
{
    uint32_t index = (uint32_t)(uintptr_t)arg;
    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(s_queue, &index, &higher_priority_woken);
    if (higher_priority_woken) {
        portYIELD_FROM_ISR();
    }
}

static void limit_task(void *arg)
{
    (void)arg;
    uint32_t index;

    while (true) {
        if (xQueueReceive(s_queue, &index, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (index >= LIMIT_COUNT) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(LIMIT_DEBOUNCE_MS));
        bool active = read_active((uint8_t)index);
        bool changed = false;

        taskENTER_CRITICAL(&s_state_lock);
        if (s_state[index].active != active) {
            s_state[index].active = active;
            s_state[index].change_count++;
            changed = true;
        }
        taskEXIT_CRITICAL(&s_state_lock);

        if (changed) {
            ESP_LOGI(TAG, "Limit %lu -> %s", (unsigned long)(index + 1), active ? "ACTIVE" : "inactive");
            if (s_callback) {
                limit_input_state_t snapshot;
                taskENTER_CRITICAL(&s_state_lock);
                snapshot = s_state[index];
                taskEXIT_CRITICAL(&s_state_lock);
                s_callback((uint8_t)index, &snapshot, s_callback_ctx);
            }
        }
    }
}

esp_err_t limit_input_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < LIMIT_COUNT; ++i) {
        mask |= (1ULL << s_limit_gpio[i]);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = LIMIT_USE_INTERNAL_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config failed");

    s_queue = xQueueCreate(LIMIT_EVENT_QUEUE_LEN, sizeof(uint32_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    for (int i = 0; i < LIMIT_COUNT; ++i) {
        taskENTER_CRITICAL(&s_state_lock);
        s_state[i].active = read_active(i);
        s_state[i].change_count = 0;
        taskEXIT_CRITICAL(&s_state_lock);

        ESP_RETURN_ON_ERROR(
            gpio_isr_handler_add(s_limit_gpio[i], gpio_isr, (void *)(uintptr_t)i),
            TAG, "gpio_isr_handler_add failed");
    }

    BaseType_t ok = xTaskCreate(limit_task, "limit_input", LIMIT_TASK_STACK_SIZE,
                                NULL, LIMIT_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "TMS limit inputs ready; active level=%d, debounce=%d ms",
             LIMIT_ACTIVE_LEVEL, LIMIT_DEBOUNCE_MS);
    return ESP_OK;
}

esp_err_t limit_input_get(uint8_t index, limit_input_state_t *state)
{
    if (index >= LIMIT_COUNT || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_state_lock);
    *state = s_state[index];
    taskEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

void limit_input_set_callback(limit_input_callback_t callback, void *ctx)
{
    s_callback = callback;
    s_callback_ctx = ctx;
}
