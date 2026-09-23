# ODIS TMS Web UI integration

New component: `components/web_server`.

Browser:

- `http://<device-ip>/`
- Default static IP from W5500 config: `http://192.168.3.200/`

HTTP API:

- `GET /api/status`
- `POST /api/command`

`POST /api/command` accepts the same JSON body handled by the existing TCP command handler, for example:

```json
{"cmd":"ulur"}
{"cmd":"tarik"}
{"cmd":"stop"}
{"cmd":"carriage_reverse"}
{"cmd":"set_speed","motor":"drum","speed_us":250}
```

Web configuration is in:

`components/web_server/include/web_server_config.h`

The HTML/CSS/JavaScript is embedded into application flash at build time from:

`components/web_server/web/index.html`

No SPIFFS/LittleFS partition is required.
