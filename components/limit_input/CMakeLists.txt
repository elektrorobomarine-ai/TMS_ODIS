idf_component_register(
    SRCS "src/limit_input.c"
    INCLUDE_DIRS "include"
    REQUIRES project_config esp_driver_gpio freertos
)
