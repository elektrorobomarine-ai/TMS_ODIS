#include "command_handler.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "app_config.h"
#include "ethernet_w5500.h"
#include "ethernet_w5500_config.h"
#include "tms_controller.h"
#include "tms_settings.h"
#include "status_led.h"
#include "cJSON.h"
#include "esp_timer.h"

static void echo_id(cJSON *dst, const cJSON *src)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(src, "id");
    if (id) { cJSON *dup = cJSON_Duplicate(id, true); if (dup) cJSON_AddItemToObject(dst, "id", dup); }
}

static cJSON *base(const cJSON *req, const char *cmd, bool ok)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", ok);
    if (cmd) cJSON_AddStringToObject(r, "cmd", cmd);
    if (req) echo_id(r, req);
    return r;
}

static cJSON *error(const cJSON *req, const char *cmd, const char *code, const char *msg)
{
    cJSON *r = base(req, cmd, false);
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddStringToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", msg);
    return r;
}

static bool parse_motor(const cJSON *req, motor_id_t *motor)
{
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(req, "motor");
    if (!cJSON_IsString(m)) return false;
    if (!strcmp(m->valuestring, "drum")) *motor = MOTOR_DRUM;
    else if (!strcmp(m->valuestring, "carriage")) *motor = MOTOR_CARRIAGE;
    else if (!strcmp(m->valuestring, "output")) *motor = MOTOR_OUTPUT;
    else return false;
    return true;
}

static void add_status(cJSON *data)
{
    tms_status_t st; tms_controller_get_status(&st);
    const tms_settings_t *cfg = tms_settings_get();
    cJSON_AddStringToObject(data, "mode", tms_mode_name(st.mode));
    cJSON_AddStringToObject(data, "carriage_direction", tms_carriage_dir_name(st.carriage_dir));
    cJSON *lim = cJSON_AddObjectToObject(data, "limits");
    cJSON_AddBoolToObject(lim, "left", st.left_limit);
    cJSON_AddBoolToObject(lim, "right", st.right_limit);
    cJSON *spd = cJSON_AddObjectToObject(data, "speed_us");
    cJSON_AddNumberToObject(spd, "drum", cfg->drum_speed_us);
    cJSON_AddNumberToObject(spd, "carriage", cfg->carriage_speed_us);
    cJSON_AddNumberToObject(spd, "output", cfg->output_speed_us);
    cJSON *pwm = cJSON_AddObjectToObject(data, "pwm_us");
    cJSON_AddNumberToObject(pwm, "drum", st.motor_pulse_us[MOTOR_DRUM]);
    cJSON_AddNumberToObject(pwm, "carriage", st.motor_pulse_us[MOTOR_CARRIAGE]);
    cJSON_AddNumberToObject(pwm, "output", st.motor_pulse_us[MOTOR_OUTPUT]);
}

char *command_handler_process(const char *json_text)
{
    if (!json_text) return strdup("{\"ok\":false,\"error\":{\"code\":\"NULL_INPUT\",\"message\":\"No JSON received\"}}");
    cJSON *req = cJSON_Parse(json_text), *resp = NULL;
    if (!req || !cJSON_IsObject(req)) { resp = error(NULL, NULL, "INVALID_JSON", "Request must be a JSON object"); goto done; }
    const cJSON *ci = cJSON_GetObjectItemCaseSensitive(req, "cmd");
    if (!cJSON_IsString(ci)) { resp = error(req, NULL, "MISSING_CMD", "Field 'cmd' is required"); goto done; }
    const char *cmd = ci->valuestring;

    if (!strcmp(cmd, "ping")) {
        resp = base(req, cmd, true); cJSON *d = cJSON_AddObjectToObject(resp, "data");
        cJSON_AddStringToObject(d, "reply", "pong");
        cJSON_AddNumberToObject(d, "uptime_ms", (double)(esp_timer_get_time()/1000ULL));
    } else if (!strcmp(cmd, "status")) {
        resp = base(req, cmd, true); cJSON *d = cJSON_AddObjectToObject(resp, "data");
        cJSON_AddStringToObject(d, "app", APP_NAME); cJSON_AddStringToObject(d, "version", APP_VERSION);
        cJSON_AddBoolToObject(d, "ethernet_link", ethernet_w5500_is_link_up());
        char ip[20] = "0.0.0.0"; ethernet_w5500_get_ip(ip, sizeof(ip)); cJSON_AddStringToObject(d, "ip", ip);
        cJSON_AddNumberToObject(d, "tcp_port", TMS_TCP_SERVER_PORT); add_status(d);
    } else if (!strcmp(cmd, "stop") || !strcmp(cmd, "ulur") || !strcmp(cmd, "tarik")) {
        esp_err_t errc = !strcmp(cmd,"stop") ? tms_controller_stop() : (!strcmp(cmd,"ulur") ? tms_controller_ulur() : tms_controller_tarik());
        if (errc != ESP_OK) resp = error(req, cmd, "CONTROL_ERROR", "Failed to change TMS motion state");
        else { resp = base(req, cmd, true); cJSON *d = cJSON_AddObjectToObject(resp,"data"); add_status(d); }
    } else if (!strcmp(cmd, "carriage_reverse")) {
        if (tms_controller_reverse_carriage() != ESP_OK) resp = error(req,cmd,"CONTROL_ERROR","Failed to reverse carriage");
        else { resp = base(req,cmd,true); cJSON *d=cJSON_AddObjectToObject(resp,"data"); add_status(d); }
    } else if (!strcmp(cmd, "set_speed")) {
        motor_id_t motor; const cJSON *speed = cJSON_GetObjectItemCaseSensitive(req,"speed_us");
        if (!parse_motor(req,&motor) || !cJSON_IsNumber(speed) || speed->valueint < TMS_SPEED_MIN_US || speed->valueint > TMS_SPEED_MAX_US) {
            resp = error(req,cmd,"INVALID_ARGUMENT","motor must be drum/carriage/output and speed_us 0..500");
        } else {
            esp_err_t e = tms_settings_set_speed(motor,(uint16_t)speed->valueint,true);
            if (e == ESP_OK) e = tms_controller_apply_speed_change(motor);
            if (e != ESP_OK) resp = error(req,cmd,"SETTINGS_ERROR","Failed to save/apply speed");
            else { resp=base(req,cmd,true); cJSON *d=cJSON_AddObjectToObject(resp,"data"); add_status(d); }
        }
    } else if (!strcmp(cmd,"help")) {
        resp=base(req,cmd,true); cJSON *d=cJSON_AddObjectToObject(resp,"data"); cJSON *a=cJSON_AddArrayToObject(d,"commands");
        const char *cmds[]={"ping","status","stop","ulur","tarik","carriage_reverse","set_speed","help"};
        for (unsigned i=0;i<sizeof(cmds)/sizeof(cmds[0]);++i) cJSON_AddItemToArray(a,cJSON_CreateString(cmds[i]));
    } else resp = error(req,cmd,"UNKNOWN_CMD","Unknown command; use cmd=help");

done:
    char *txt = cJSON_PrintUnformatted(resp); cJSON_Delete(resp); cJSON_Delete(req);
    return txt ? txt : strdup("{\"ok\":false,\"error\":{\"code\":\"NO_MEMORY\",\"message\":\"Response allocation failed\"}}");
}
