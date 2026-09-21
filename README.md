# ODIS TMS Controller v0.3.0

ESP-IDF / ESP32-S3-ETH controller for a ROV Tether Management System.

## Motor assignment
- Motor 1 `drum`: main tether drum, payout/recovery/tension.
- Motor 2 `carriage`: level-wind carriage via power screw. Left/right endpoint switches automatically reverse the carriage.
- Motor 3 `output`: cable output/traction motor, coordinated with the drum.

## Network hard-coded configuration
Edit only:
`components/ethernet_w5500/include/ethernet_w5500_config.h`

Defaults:
- IP `192.168.3.200`
- Netmask `255.255.255.0`
- Gateway `192.168.3.1`
- TCP server port `5000`

## Motor/IO pins and direction polarity
Edit:
`components/project_config/include/board_config.h`

## Persistent speed settings
Speed values are PWM offsets from 1500 us neutral and saved in ESP32 NVS.
Range `0..500 us`.
Defaults are in `components/tms_settings/include/tms_settings.h`.

## NDJSON commands
Terminate each JSON object with newline (`\n`).

```json
{"id":1,"cmd":"status"}
{"id":2,"cmd":"ulur"}
{"id":3,"cmd":"tarik"}
{"id":4,"cmd":"stop"}
{"id":5,"cmd":"carriage_reverse"}
{"id":6,"cmd":"set_speed","motor":"drum","speed_us":250}
{"id":7,"cmd":"set_speed","motor":"carriage","speed_us":180}
{"id":8,"cmd":"set_speed","motor":"output","speed_us":250}
```

`set_speed` writes the new value to NVS immediately and reapplies it if TMS is moving.

## Build
```bat
idf.py set-target esp32s3
idf.py fullclean
idf.py reconfigure
idf.py build
```
