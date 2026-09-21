#include "status_led.h"

#include "app_config.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_log.h"

static const char *TAG = "status_led";
static led_strip_handle_t s_strip = NULL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static status_led_state_t s_system_state = STATUS_LED_BOOT;
static bool s_auto_mode = true;
static uint8_t s_manual_r = 0;
static uint8_t s_manual_g = 0;
static uint8_t s_manual_b = 0;

static void get_color(status_led_state_t state, bool phase, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = *g = *b = 0;
    switch (state) {
    case STATUS_LED_BOOT:
        *r = 32; *g = 10; *b = 0;
        break;
    case STATUS_LED_NET_WAIT:
        if (phase) { *b = 32; }
        break;
    case STATUS_LED_READY:
        *g = 32;
        break;
    case STATUS_LED_CLIENT:
        *g = 18; *b = 32;
        break;
    case STATUS_LED_ERROR:
        if (phase) { *r = 48; }
        break;
    default:
        break;
    }
}

static void led_task(void *arg)
{
    (void)arg;
    bool phase = false;

    while (true) {
        bool auto_mode;
        status_led_state_t state;
        uint8_t r, g, b;

        taskENTER_CRITICAL(&s_lock);
        auto_mode = s_auto_mode;
        state = s_system_state;
        r = s_manual_r;
        g = s_manual_g;
        b = s_manual_b;
        taskEXIT_CRITICAL(&s_lock);

        phase = !phase;
        if (auto_mode) {
            get_color(state, phase, &r, &g, &b);
        }

        if (s_strip) {
            led_strip_set_pixel(s_strip, 0, r, g, b);
            led_strip_refresh(s_strip);
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_LED_UPDATE_MS));
    }
}

esp_err_t status_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_COUNT,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        return err;
    }

    led_strip_clear(s_strip);
    BaseType_t ok = xTaskCreate(led_task, "status_led", STATUS_LED_TASK_STACK_SIZE,
                                NULL, STATUS_LED_TASK_PRIORITY, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void status_led_set_system(status_led_state_t state)
{
    taskENTER_CRITICAL(&s_lock);
    s_system_state = state;
    taskEXIT_CRITICAL(&s_lock);
}

void status_led_set_manual(uint8_t red, uint8_t green, uint8_t blue)
{
    taskENTER_CRITICAL(&s_lock);
    s_manual_r = red;
    s_manual_g = green;
    s_manual_b = blue;
    s_auto_mode = false;
    taskEXIT_CRITICAL(&s_lock);
}

void status_led_set_auto(bool enable)
{
    taskENTER_CRITICAL(&s_lock);
    s_auto_mode = enable;
    taskEXIT_CRITICAL(&s_lock);
}

bool status_led_is_auto(void)
{
    bool result;
    taskENTER_CRITICAL(&s_lock);
    result = s_auto_mode;
    taskEXIT_CRITICAL(&s_lock);
    return result;
}
