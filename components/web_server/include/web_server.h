#pragma once

#include "esp_err.h"

/** Start the embedded TMS HTTP server. Safe to call once after networking init. */
esp_err_t web_server_start(void);

/** Stop the HTTP server if it is running. */
esp_err_t web_server_stop(void);
