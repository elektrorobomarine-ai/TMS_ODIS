#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/* Waveshare ESP32-S3-ETH onboard W5500 */
#define W5500_SPI_HOST              SPI2_HOST
#define W5500_MISO_GPIO             GPIO_NUM_12
#define W5500_MOSI_GPIO             GPIO_NUM_11
#define W5500_SCLK_GPIO             GPIO_NUM_13
#define W5500_CS_GPIO               GPIO_NUM_14
#define W5500_RST_GPIO              GPIO_NUM_9
#define W5500_INT_GPIO              GPIO_NUM_10
#define W5500_SPI_CLOCK_HZ          (20 * 1000 * 1000)

/* Onboard WS2812 RGB */
#define RGB_LED_GPIO                GPIO_NUM_21
#define RGB_LED_COUNT               1

/* ============================================================
 * TMS MOTOR PWM OUTPUTS
 * M1 = main tether drum
 * M2 = level-wind carriage / power screw
 * M3 = cable output / traction motor
 * ============================================================ */
#define MOTOR_DRUM_PWM_GPIO         GPIO_NUM_33
#define MOTOR_CARRIAGE_PWM_GPIO     GPIO_NUM_34
#define MOTOR_OUTPUT_PWM_GPIO       GPIO_NUM_35

/* ============================================================
 * DIGITAL INPUTS
 * ============================================================ */
#define LIMIT_CARRIAGE_LEFT_GPIO    GPIO_NUM_37
#define LIMIT_CARRIAGE_RIGHT_GPIO   GPIO_NUM_38
#define LIMIT_SPARE1_GPIO           GPIO_NUM_39
#define LIMIT_SPARE2_GPIO           GPIO_NUM_40

/* Motor direction polarity.
 * +1: positive logical direction => neutral + speed
 * -1: positive logical direction => neutral - speed
 * Flip one of these if physical motor direction is reversed.
 */
#define DRUM_DIRECTION_POLARITY      (+1)
#define CARRIAGE_DIRECTION_POLARITY  (+1)
#define OUTPUT_DIRECTION_POLARITY    (+1)

/* Logical operation directions.
 * Adjust these if drum/output motor mechanics rotate opposite to desired.
 */
#define DRUM_DIR_ULUR                (-1)
#define DRUM_DIR_TARIK               (+1)
#define OUTPUT_DIR_ULUR              (+1)
#define OUTPUT_DIR_TARIK             (-1)
#define CARRIAGE_DIR_LEFT            (-1)
#define CARRIAGE_DIR_RIGHT           (+1)
