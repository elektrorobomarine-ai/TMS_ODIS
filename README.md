# ODIS TMS Controller

Firmware controller untuk **ODIS Tether Management System (TMS)** berbasis **ESP32-S3-ETH + W5500**.

**Current version: v0.5.0**

Firmware menyediakan dua antarmuka kontrol yang menggunakan command handler yang sama:

- TCP JSON Server
- Embedded Web UI / HTTP API

---

## Hardware Platform

- MCU: ESP32-S3
- Board: Waveshare ESP32-S3-ETH
- Ethernet: W5500
- CPU clock: 240 MHz
- Flash: 16 MB
- Motor control: bidirectional RC PWM
- Limit input: digital input
- Status LED: onboard WS2812

### W5500 Pin Assignment

| Function | GPIO |
|---|---:|
| MISO | GPIO12 |
| MOSI | GPIO11 |
| SCLK | GPIO13 |
| CS | GPIO14 |
| RST | GPIO9 |
| INT | GPIO10 |

### Motor PWM

| Motor | GPIO |
|---|---:|
| Drum Main | GPIO33 |
| Carriage / Level Wind | GPIO34 |
| Cable Output | GPIO35 |

### Limit Input

| Input | GPIO |
|---|---:|
| Carriage Left Limit | GPIO37 |
| Carriage Right Limit | GPIO38 |
| Spare 1 | GPIO39 |
| Spare 2 | GPIO40 |

---

## PWM Model

Motor menggunakan bidirectional RC PWM:

```text
1000 us = maximum reverse
1500 us = neutral / stop
2000 us = maximum forward
```

Nilai `speed_us` disimpan sebagai offset dari neutral 1500 us.

Contoh:

```text
speed_us = 250

positive direction -> 1750 us
negative direction -> 1250 us
```

Range:

```text
0 ... 500 us
```

---

# v0.5.0 Update

Versi v0.5.0 mengubah konfigurasi kecepatan dari satu nilai per motor menjadi **directional speed profile**.

```text
Drum Main
├── ULUR
└── TARIK

Carriage
├── LEFT
└── RIGHT

Cable Output
├── ULUR
└── TARIK
```

Total terdapat **6 persistent speed profiles**.

## NVS Keys

Namespace:

```text
tms_cfg
```

| Motor | Direction | NVS Key |
|---|---|---|
| Drum | ULUR | `drum_ulur` |
| Drum | TARIK | `drum_tarik` |
| Carriage | LEFT | `carr_left` |
| Carriage | RIGHT | `carr_right` |
| Output | ULUR | `out_ulur` |
| Output | TARIK | `out_tarik` |

Contoh:

```text
drum_ulur  = 220
drum_tarik = 300

carr_left  = 140
carr_right = 180

out_ulur   = 210
out_tarik  = 270
```

### Legacy NVS Migration

Firmware dapat memigrasikan key lama:

```text
drum_spd
carr_spd
out_spd
```

Contoh:

```text
drum_spd = 250
```

akan menjadi:

```text
drum_ulur  = 250
drum_tarik = 250
```

---

# Operating Modes

Controller memiliki empat mode:

```text
STOP
ULUR
TARIK
MANUAL
```

## STOP

Semua motor neutral.

```text
1500 us
```

## ULUR

- Drum menggunakan `drum_ulur`
- Output menggunakan `out_ulur`
- Carriage menggunakan LEFT/RIGHT profile sesuai arah saat itu

## TARIK

- Drum menggunakan `drum_tarik`
- Output menggunakan `out_tarik`
- Carriage menggunakan LEFT/RIGHT profile sesuai arah saat itu

## MANUAL

Motor dapat dikontrol individual menggunakan:

- motor
- direction
- temporary `speed_us`

Manual speed **tidak disimpan ke NVS**.

---

# Carriage Logic

Automatic mode:

```text
Carriage LEFT
    |
LEFT LIMIT
    |
    v
Carriage RIGHT
```

dan:

```text
Carriage RIGHT
    |
RIGHT LIMIT
    |
    v
Carriage LEFT
```

Saat arah berubah, firmware otomatis menggunakan speed profile arah baru.

Jika kedua limit aktif bersamaan:

```text
LEFT LIMIT  = ACTIVE
RIGHT LIMIT = ACTIVE
```

carriage dihentikan.

## Manual Carriage Interlock

Jika LEFT limit aktif:

```json
{
  "cmd": "motor_run",
  "motor": "carriage",
  "direction": "left",
  "speed_us": 180
}
```

akan ditolak.

Tetapi:

```json
{
  "cmd": "motor_run",
  "motor": "carriage",
  "direction": "right",
  "speed_us": 180
}
```

tetap diizinkan karena bergerak menjauhi limit.

Manual carriage tidak auto-reverse. Jika limit aktif saat carriage sedang bergerak ke arah limit, carriage dihentikan.

---

# JSON Commands

TCP menggunakan NDJSON: satu JSON object per baris.

## Status

```json
{"cmd":"status"}
```

## Automatic Operation

```json
{"cmd":"ulur"}
```

```json
{"cmd":"tarik"}
```

```json
{"cmd":"stop"}
```

```json
{"cmd":"carriage_reverse"}
```

---

# Directional Speed Configuration

## Drum ULUR

```json
{
  "cmd":"set_speed",
  "motor":"drum",
  "direction":"ulur",
  "speed_us":220
}
```

## Drum TARIK

```json
{
  "cmd":"set_speed",
  "motor":"drum",
  "direction":"tarik",
  "speed_us":300
}
```

## Carriage LEFT

```json
{
  "cmd":"set_speed",
  "motor":"carriage",
  "direction":"left",
  "speed_us":140
}
```

## Carriage RIGHT

```json
{
  "cmd":"set_speed",
  "motor":"carriage",
  "direction":"right",
  "speed_us":180
}
```

## Output ULUR

```json
{
  "cmd":"set_speed",
  "motor":"output",
  "direction":"ulur",
  "speed_us":210
}
```

## Output TARIK

```json
{
  "cmd":"set_speed",
  "motor":"output",
  "direction":"tarik",
  "speed_us":270
}
```

Legacy format masih didukung:

```json
{
  "cmd":"set_speed",
  "motor":"drum",
  "speed_us":250
}
```

Firmware akan menerapkan nilai tersebut ke ULUR dan TARIK.

---

# Manual Motor Control

## Run Drum

```json
{
  "cmd":"motor_run",
  "motor":"drum",
  "direction":"tarik",
  "speed_us":300
}
```

## Run Carriage

```json
{
  "cmd":"motor_run",
  "motor":"carriage",
  "direction":"left",
  "speed_us":150
}
```

## Run Cable Output

```json
{
  "cmd":"motor_run",
  "motor":"output",
  "direction":"ulur",
  "speed_us":220
}
```

## Stop Individual Motor

```json
{
  "cmd":"motor_stop",
  "motor":"drum"
}
```

Jika motor terakhir pada MANUAL mode dihentikan, mode kembali ke STOP.

Jika `motor_stop` dikirim saat ULUR/TARIK aktif, controller melakukan global stop untuk menghindari mixed automatic/manual state.

---

# Restore Defaults

```json
{"cmd":"restore_defaults"}
```

Semua 6 speed profile dikembalikan ke compile-time default dan disimpan ke NVS.

---

# Status Response

Contoh:

```json
{
  "ok": true,
  "cmd": "status",
  "data": {
    "app": "ODIS TMS Controller",
    "version": "0.5.0",
    "ip": "192.168.3.200",
    "tcp_port": 5000,

    "mode": "tarik",
    "carriage_direction": "left",

    "limits": {
      "left": false,
      "right": false
    },

    "settings": {
      "drum": {
        "ulur": 220,
        "tarik": 300
      },
      "carriage": {
        "left": 140,
        "right": 180
      },
      "output": {
        "ulur": 210,
        "tarik": 270
      }
    },

    "motor": {
      "drum": {
        "active": true,
        "direction": "tarik",
        "speed_us": 300,
        "pwm_us": 1800
      },
      "carriage": {
        "active": true,
        "direction": "left",
        "speed_us": 140,
        "pwm_us": 1360
      },
      "output": {
        "active": true,
        "direction": "tarik",
        "speed_us": 270,
        "pwm_us": 1230
      }
    }
  }
}
```

---

# Embedded Web UI

Default address:

```text
http://192.168.3.200/
```

Web UI v0.5.0 menyediakan:

- Live system status
- ULUR / TARIK / STOP
- Reverse carriage
- Left/right limit indication
- 6 directional speed profiles
- Apply individual profile
- Apply all profiles
- Restore defaults
- Manual motor control
- Temporary manual speed
- Individual motor stop
- Actual runtime PWM
- Runtime motor direction
- Event log

Shortcut:

```text
ESC = Global Software STOP
```

HTTP endpoints:

```text
GET  /api/status
POST /api/command
```

TCP dan HTTP menggunakan `command_handler` yang sama.

---

# Firmware Architecture

```text
TCP Client
    |
    v
TCP JSON Server ----+
                    |
                    v
              command_handler
                    ^
                    |
Web UI / HTTP ------+
                    |
                    v
              tms_controller
                    |
        +-----------+-----------+
        |           |           |
        v           v           v
      Drum      Carriage      Output
                    |
                    v
                motor_pwm

tms_settings
    |
    v
   NVS
```

---

# Main Components

```text
components/
├── ethernet_w5500/
├── motor_pwm/
├── limit_input/
├── tms_settings/
├── tms_controller/
├── command_handler/
├── tcp_json_server/
├── web_server/
├── status_led/
└── project_config/
```

Responsibilities:

- `ethernet_w5500`: W5500 and network configuration
- `motor_pwm`: low-level RC PWM
- `limit_input`: digital limit switch handling
- `tms_settings`: directional speed profiles and NVS
- `tms_controller`: operating modes, carriage logic, manual control, interlock
- `command_handler`: JSON parsing, validation, response generation
- `tcp_json_server`: TCP NDJSON interface
- `web_server`: embedded HTTP server and Web UI
- `status_led`: onboard RGB status LED
- `project_config`: board and application configuration

---

# Important Configuration

## Network

```text
components/ethernet_w5500/include/ethernet_w5500_config.h
```

Example:

```c
#define TMS_NET_USE_STATIC_IP       1
#define TMS_NET_IPV4_ADDR           "192.168.3.200"
#define TMS_NET_IPV4_NETMASK        "255.255.255.0"
#define TMS_NET_IPV4_GATEWAY        "192.168.3.1"
#define TMS_TCP_SERVER_PORT         5000
```

## Motor Direction

```text
components/project_config/include/board_config.h
```

Example:

```c
#define DRUM_DIRECTION_POLARITY      (+1)
#define CARRIAGE_DIRECTION_POLARITY  (+1)
#define OUTPUT_DIRECTION_POLARITY    (+1)

#define DRUM_DIR_ULUR                (-1)
#define DRUM_DIR_TARIK               (+1)

#define OUTPUT_DIR_ULUR              (+1)
#define OUTPUT_DIR_TARIK             (-1)

#define CARRIAGE_DIR_LEFT            (-1)
#define CARRIAGE_DIR_RIGHT           (+1)
```

## Version

```text
components/project_config/include/app_config.h
```

```c
#define APP_NAME       "ODIS TMS Controller"
#define APP_VERSION    "0.5.0"
```

---

# Build Environment

Known working configuration:

```text
ESP-IDF    : 6.0.1
Target     : ESP32-S3
CPU Clock  : 240 MHz
Flash Size : 16 MB
```

cJSON dependency:

```yaml
dependencies:
  espressif/cjson: "^1.7.19"
```

---

# Build

```powershell
idf.py reconfigure
idf.py build
```

Clean build:

```powershell
Remove-Item -Recurse -Force .\build
idf.py reconfigure
idf.py build
```

---

# Flash

```powershell
idf.py flash monitor
```

Optional COM port:

```powershell
idf.py -p COM5 flash monitor
```

---

# Web UI Embedded File

```text
components/web_server/web/index.html
```

HTML di-embed ke application image dan tidak membutuhkan SPIFFS/LittleFS.

Pada project ini generated symbol yang digunakan adalah:

```text
_binary_index_html_start
_binary_index_html_end
```

---

# Recommended Test Sequence

1. Boot ESP32-S3.
2. Verify W5500 link.
3. Verify static IP.
4. Open Web UI.
5. Verify mode awal STOP.
6. Verify all PWM neutral.
7. Verify left/right limit.
8. Test carriage manual pada speed rendah.
9. Test carriage interlock.
10. Test drum manual.
11. Test output manual.
12. Test ULUR.
13. Test TARIK.
14. Verify left/right carriage speed dapat berbeda.
15. Verify ULUR/TARIK drum dan output speed dapat berbeda.
16. Reboot dan verify NVS tetap tersimpan.

---

# Safety

`STOP` dari TCP atau Web UI adalah **software stop**.

Untuk sistem TMS aktual, hardware emergency stop harus tetap independen dari:

- ESP32-S3
- Ethernet
- W5500
- TCP
- HTTP
- browser
- firmware application state

---

# Version History

## v0.5.0

Added:

- 6 directional motor speed profiles
- Separate drum ULUR/TARIK speed
- Separate carriage LEFT/RIGHT speed
- Separate output ULUR/TARIK speed
- NVS persistence
- Legacy NVS migration
- MANUAL operating mode
- `motor_run`
- `motor_stop`
- Temporary manual speed
- Carriage manual limit interlock
- Automatic directional carriage speed selection
- Runtime motor status
- Updated status JSON
- Web UI v0.5.0
- Legacy `set_speed` compatibility
- `restore_defaults`

## v0.4.0

Added:

- Embedded Web UI
- HTTP server
- `GET /api/status`
- `POST /api/command`
- Web ULUR/TARIK/STOP control
- Web speed configuration
- Runtime status display

## v0.3.0

Initial operational TMS release:

- Three motor control
- Drum
- Carriage
- Cable output
- Automatic carriage limit reverse
- NVS motor speed
- TCP JSON server
- RC PWM control

---

# Git Update

```powershell
git status
git add .
git commit -m "docs: update README for ODIS TMS v0.5.0"
git push
```

Optional release tag:

```powershell
git tag -a v0.5.0 -m "ODIS TMS v0.5.0 directional motor control"
git push origin v0.5.0
```