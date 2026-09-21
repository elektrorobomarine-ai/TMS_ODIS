idf_component_register(
    SRCS "src/tcp_json_server.c"
    INCLUDE_DIRS "include"
    REQUIRES project_config ethernet_w5500 command_handler status_led lwip freertos
)
