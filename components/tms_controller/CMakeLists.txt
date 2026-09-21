idf_component_register(
    SRCS "src/tms_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES project_config motor_pwm limit_input tms_settings freertos
)
