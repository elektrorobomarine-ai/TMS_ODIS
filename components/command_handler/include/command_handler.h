#pragma once

/* Returns heap-allocated JSON text. Caller must free(). */
char *command_handler_process(const char *json_text);
