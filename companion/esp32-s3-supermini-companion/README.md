# ESP32-S3 Super Mini Companion Firmware

This folder contains a small companion firmware that exposes a simple I2C-slave interface and can run a channel-locked PMKID monitor while the main CYD ESP32-S3 continues UI/SD work.

## Wiring (to CYD ESP32-S3)

- CYD `GPIO16` (SDA) → Super Mini `GPIO8` (SDA)
- CYD `GPIO15` (SCL) → Super Mini `GPIO9` (SCL)
- **GND ↔ GND**

The CYD touch controller uses I2C address `0x38`. This companion uses `0x42` by default, so they can share the same bus.

## Build / Flash

- Board: **ESP32S3 Dev Module**
- Core: `esp32` by Espressif (tested with `3.3.7`)
- Flash/PSRAM options: set these to match your Super Mini hardware (this sketch does not require PSRAM).

Sketch: `companion/esp32-s3-supermini-companion/esp32-s3-supermini-companion.ino`

## I2C Protocol (v1)

The main MCU (I2C master) writes commands as `[cmd][payload...]` and then polls by reading 64 bytes.

### Commands

- `0x02` **SET_TARGET**: `[channel][bssid(6)]`
- `0x03` **START_PMKID**
- `0x04` **STOP_ALL**
- `0x05` **CLEAR_RESULT**
- `0x06` **SET_CHANNEL**: `[channel]`

### Status (64 bytes)

Byte offsets:

- `[0..1]` magic: `'C' 'M'`
- `[2]` protocol version
- `[3]` flags: bit0 monitor_active, bit1 target_set, bit2 pmkid_found
- `[4]` channel
- `[5..10]` target_bssid
- `[11..16]` sta_mac
- `[17..32]` pmkid
- `[33]` last_rssi (int8)

## Notes

- The companion is a *slave* and cannot initiate transfers; the main MCU must poll for status/results.
- Use only on networks/devices you own or have explicit permission to assess.
