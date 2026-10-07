idf_component_register(
    SRCS
        "src/command_handler.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        project_config
        ethernet_w5500
        motor_pwm
        tms_controller
        tms_settings
)
