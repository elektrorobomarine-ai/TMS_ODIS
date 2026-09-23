idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES project_config status_led motor_pwm limit_input tms_settings tms_controller ethernet_w5500 tcp_json_server web_server nvs_flash
)
