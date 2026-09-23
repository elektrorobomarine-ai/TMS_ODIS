#pragma once

/* ============================================================
 * ODIS TMS - EMBEDDED WEB SERVER CONFIGURATION
 * ============================================================ */

/* Browser URL: http://<device-ip>:TMS_WEB_SERVER_PORT/ */
#define TMS_WEB_SERVER_PORT             80

/* Maximum JSON body accepted by POST /api/command. */
#define TMS_WEB_MAX_JSON_BODY           1024

/* HTTP server task configuration. */
#define TMS_WEB_TASK_STACK_SIZE         6144
#define TMS_WEB_TASK_PRIORITY           5
#define TMS_WEB_MAX_URI_HANDLERS        8
#define TMS_WEB_RECV_WAIT_TIMEOUT_S     5
#define TMS_WEB_SEND_WAIT_TIMEOUT_S     5
