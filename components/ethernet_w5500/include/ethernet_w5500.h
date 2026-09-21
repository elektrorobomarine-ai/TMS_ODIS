#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t ethernet_w5500_init(void);
bool ethernet_w5500_is_link_up(void);
esp_err_t ethernet_w5500_get_ip(char *buf, size_t buf_len);
