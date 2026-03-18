# ESP32-S3 Auditor Suite

A comprehensive security auditing and Meshtastic-integrated tool for the ESP32-S3 (CYD/S3 hardware).

## 🛠 Arduino IDE Setup

### Board Manager
1. Open **File > Preferences**.
2. Add this URL to "Additional Board Manager URLs": 
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Go to **Tools > Board > Boards Manager**, search for `esp32` by Espressif, and install version **3.3.7** (matching your current environment).

### Board Settings (Critical)
Select **Tools** and configure these exact settings:
- **Board:** "ESP32S3 Dev Module"
- **USB CDC On Boot:** "Enabled" (Allows Serial monitoring via the USB-C port)
- **Flash Size:** "16MB (128Mb)"
- **Partition Scheme:** "16M Flash (3MB APP/9MB FATFS)" 
- **PSRAM:** "OPI PSRAM" (Required for high-speed UI buffers)
- **CPU Frequency:** "240MHz (WiFi/BT)"

### Required Libraries
Install these via **Tools > Manage Libraries**:
1. **lvgl** (v8.3.11) - *Note: requires a custom `lv_conf.h` in your libraries folder*
2. **TFT_eSPI** (v2.5.43) - *Note: requires a custom `User_Setup.h` for your display pins*
3. **NimBLE-Arduino** (v2.3.9) - For BLE sniffing and flooding
4. **Nanopb** - For Meshtastic Protobuf communication

## 📂 Project Structure
- `esp32-s3-auditor.ino`: Main entry point and task management.
- `ui_module.cpp`: LVGL UI definitions and layout.
- `display.cpp`: Low-level display drivers and I2C touch handling.
- `wifi_scanner.cpp`: AP and Station detection logic.
- `pentest_attacks.cpp`: Deauth, Beacon, and PMKID capture implementations.
- `lora_service_task`: (Internal to .ino) Handles Meshtastic Protobuf over Serial.

## 🚀 Build Instructions
1. Open `esp32-s3-auditor.ino` in Arduino IDE.
2. Verify the pins in `constants.h` match your specific hardware (Current: Heltec/CYD-S3 layout).
3. Press **Upload**.

## ⚠️ Troubleshooting
- **Boot Loops:** Usually caused by choosing "Internal RAM" for LVGL buffers instead of PSRAM. Ensure PSRAM is set to **OPI**.
- **SD Card Failed:** Ensure the SD card is formatted as FAT32.
- **LoRa No Signal:** This suite connects to an *external* Meshtastic node via Serial (TX/RX). Ensure the node is in "Serial" mode.

---
**Firmware Version:** v1.0.0  
**Build Environment:** Arduino IDE
```