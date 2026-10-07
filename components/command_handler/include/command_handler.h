#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Process one complete JSON command.
 *
 * Input:
 *   A null-terminated JSON object.
 *
 * Return:
 *   Heap-allocated, null-terminated JSON response.
 *
 * Ownership:
 *   Caller MUST free() the returned pointer.
 *
 * The function is safe to call from both the TCP server and HTTP server.
 * Controller/settings synchronization is handled by their respective modules.
 *
 * Example:
 *
 *   char *reply = command_handler_process(
 *       "{\"id\":1,\"cmd\":\"status\"}"
 *   );
 *
 *   if (reply != NULL) {
 *       send(reply);
 *       free(reply);
 *   }
 */
char *command_handler_process(const char *json_text);

#ifdef __cplusplus
}
#endif
