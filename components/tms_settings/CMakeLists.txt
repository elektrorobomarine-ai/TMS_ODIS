idf_component_register(
    SRCS "src/tms_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES motor_pwm nvs_flash
)
