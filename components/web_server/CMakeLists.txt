idf_component_register(
    SRCS
        "src/web_server.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_http_server
        command_handler
)

target_add_binary_data(
    ${COMPONENT_LIB}
    "web/index.html"
    TEXT
)