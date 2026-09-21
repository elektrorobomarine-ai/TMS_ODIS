# Header-only project configuration component.
# board_config.h exports GPIO_NUM_xx and SPI2_HOST symbols, so these
# dependencies must be PUBLIC (REQUIRES), not private.
idf_component_register(
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_spi
)
