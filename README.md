# ESP32-S3 Auditor Suite

Touchscreen toolkit for the ESP32-S3 (CYD/S3 hardware) focused on authorized wireless security auditing and Meshtastic (LoRa) serial integration.

## Disclaimer
This project is intended for educational use and authorized testing only. You are responsible for complying with local laws and getting explicit permission before testing any network or device you do not own.

## Features (high level)
- WiFi scan (AP / STA / Linked)
- Audit actions (reconnect test, beacon load test, PMKID capture)
- PCAP capture + probe request monitoring to SD
- BLE scanning + advertisement test
- Meshtastic (LoRa) serial UI (terminal/chat/stats/nodedb)

## Arduino IDE setup
### Board manager
1. Arduino IDE -> File > Preferences
2. Add to Additional Board Manager URLs:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Install `esp32` by Espressif (tested with 3.3.7)

### Board settings (known-good baseline)
- Board: ESP32S3 Dev Module
- USB CDC On Boot: Enabled
- Flash Size: 16MB
- Partition Scheme: 16M Flash (3MB APP/9MB FATFS)
- PSRAM: OPI PSRAM
- CPU Frequency: 240MHz

### Libraries
Install via Tools > Manage Libraries:
- lvgl (8.3.x)
- TFT_eSPI (2.5.x)
- NimBLE-Arduino (2.3.x)
- Nanopb (required for Meshtastic protobuf; provides `pb_encode.h` / `pb_decode.h`)

This repo includes:
- `lv_conf.h` (LVGL config) - copy into your `lvgl` library folder if needed
- `User_Setup.h` (TFT_eSPI setup) - copy into your `TFT_eSPI` library folder if needed

## Build / flash
1. Open `esp32-s3-auditor.ino` in Arduino IDE.
2. Confirm `constants.h` matches your pinout/hardware.
3. Click Upload.

## Usage guide
See `workflows.md` for suggested operational workflows.

## Troubleshooting
- Boot loops: usually PSRAM config mismatch (ensure OPI PSRAM).
- SD failures: format FAT32 and verify wiring/pin config.
- LoRa/Meshtastic: requires an external Meshtastic node connected over UART and configured for serial.
