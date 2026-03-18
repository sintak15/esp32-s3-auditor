# ESP32-S3 Auditor (Refactored)

A high-performance ESP32-S3-based wireless analysis and auditing toolkit featuring Wi-Fi scanning, BLE sniffing/flooding, LoRa support, SD logging, and a real-time LVGL UI.

This version is heavily refactored for stability, performance, and memory safety.

---

## 🚀 Features

### 📡 Wi-Fi
- Active AP scanning
- Station (client) detection
- AP ↔ client association mapping (linked view)
- Channel hopping
- Promiscuous mode capture
- PCAP queue pipeline (for logging/export)

### 📶 BLE
- BLE advertisement sniffing (NimBLE-based)
- BLE resilience testing (advertising flood audit)
- Fixed-size unique device tracking (no heap fragmentation)
- Ring-buffer packet pipeline
- SD logging support

### 📻 LoRa
- Packet decoding pipeline
- Background service task
- UI + logging integration

### 💾 Storage
- SD card logging (Wi-Fi + BLE)
- FAT partition support
- Non-blocking logging pipeline

### 🖥 UI (LVGL)
- Real-time scan display
- Multi-tab interface
- Touch-safe rendering (no redraw during scroll)
- Fixed object pools (no LVGL fragmentation)

---

## 🧠 Architecture

### RTOS Task Layout

| Task | Core | Purpose |
|------|------|--------|
| main_app_task | 0 | UI + coordination |
| ble_task | 0 | BLE control + flood |
| lora_service_task | 1 | LoRa processing |
| LVGL timer | 1 | UI rendering |

---

## 📜 License (MIT)

MIT License

Copyright (c) 2026 Justin Stephenson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
