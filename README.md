# AeroGarden Sprout with ESP32 Feather V2

ESP32-based controller for an AeroGarden with MQTT control for LED lighting and water pump.

## Hardware

- **Controller:** ESP32 Feather V2 (powered via buck converter from 12V supply)
- **LED Panel:** GPIO 33, driven by MOSFET, 12V powered
- **Water Pump:** GPIO 27, driven by MOSFET, 12V powered

## Configuration

WiFi and MQTT settings via `sdkconfig` or `Kconfig.projbuild`:
- `CONFIG_WIFI_SSID`
- `CONFIG_WIFI_PASS`
- `CONFIG_MQTT_BROKER_URI`
- `CONFIG_MQTT_USER`
- `CONFIG_MQTT_PASS`

## MQTT Topics

**Subscribe:**
- `home/aerogarden/pump/set` — ON/OFF
- `home/aerogarden/led/set` — ON/OFF

**Publish:**
- `home/aerogarden/pump/state`
- `home/aerogarden/led/state`

## Building

```bash
idf.py build
idf.py flash monitor
```
