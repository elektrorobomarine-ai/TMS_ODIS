#pragma once

#define APP_NAME                    "ODIS TMS Controller"
#define APP_VERSION                 "0.4.0"

/* Three RC-PWM motor controllers / bidirectional ESCs */
#define MOTOR_COUNT                 3
#define MOTOR_PWM_MIN_US            1000
#define MOTOR_PWM_MAX_US            2000
#define MOTOR_PWM_NEUTRAL_US        1500
#define MOTOR_PWM_SAFE_US           MOTOR_PWM_NEUTRAL_US
#define MOTOR_PWM_PERIOD_US         20000
#define MOTOR_PWM_RESOLUTION_HZ     1000000

/* Limit inputs: left + right carriage endpoints + two spare inputs */
#define LIMIT_COUNT                 4
#define LIMIT_ACTIVE_LEVEL          0
#define LIMIT_USE_INTERNAL_PULLUP   1
#define LIMIT_DEBOUNCE_MS           20
#define LIMIT_EVENT_QUEUE_LEN       16
#define LIMIT_TASK_STACK_SIZE       3072
#define LIMIT_TASK_PRIORITY         10

/* TMS controller task */
#define TMS_CTRL_TASK_STACK_SIZE    4096
#define TMS_CTRL_TASK_PRIORITY      9
#define TMS_CTRL_EVENT_QUEUE_LEN    16

/* TCP server */
#define TCP_RX_LINE_MAX             1024
#define TCP_SERVER_BACKLOG          1
#define TCP_SERVER_TASK_STACK_SIZE  6144
#define TCP_SERVER_TASK_PRIORITY    8
#define TCP_KEEPALIVE_IDLE_S        10
#define TCP_KEEPALIVE_INTERVAL_S    5
#define TCP_KEEPALIVE_COUNT         3

/* RGB LED */
#define STATUS_LED_TASK_STACK_SIZE  3072
#define STATUS_LED_TASK_PRIORITY    5
#define STATUS_LED_UPDATE_MS        100
