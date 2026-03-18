#pragma once

// UI
#define SCREEN_W 240
#define SCREEN_H 320
#define LV_COLOR_DARK_BG 0x111111
#define LV_COLOR_LIGHT_BG 0x181818
#define LV_COLOR_RED 0xFF0000
#define LV_COLOR_YELLOW 0xFFFF00
#define LV_COLOR_GREEN 0x00FF00
#define LV_COLOR_BLUE 0x0000FF
#define LV_COLOR_CYAN 0x00FFFF
#define LV_COLOR_DARK_GRAY 0x444444

// Hardware Pins
#define TFT_BL 45 // Backlight control
#define BT_PWM_CHANNEL 0
#define BT_PWM_FREQ    5000
#define BT_PWM_RES     8

#define TP_RST 18 // Touch reset
#define I2C_SDA 16 // Touch I2C SDA
#define I2C_SCL 15 // Touch I2C SCL
#define TOUCH_ADDR 0x38 // Touch controller I2C address
#define TP_INT 17 // Touch interrupt pin
#define GPS_RX_PIN 43 // GPS on the standard UART pins
#define GPS_TX_PIN 44
#define LORA_RX_PIN 14 // Unencumbered GPIO for Heltec
#define LORA_TX_PIN 21 // Unencumbered GPIO for Heltec
#define BATT_ADC 9 // Verified via ADC scanner (2100mV * 2 = 4.2V)

// WiFi Scanner
#define MAX_APS 64
#define MAX_STAS 128
#define STA_TIMEOUT_MS 30000 // 30 seconds
#define AP_SCAN_INTERVAL_MS 5000 // 5 seconds

// PCAP Sniffer
#define MAX_PCAP_PACKET_SIZE 256 // Max size of 802.11 frame to capture
#define PCAP_QUEUE_SIZE 50 // Number of packets in the ring buffer
#define CHANNEL_HOP_INTERVAL_MS 100 // Time to stay on a channel during sniffing

// Probe Sniffer
#define PROBE_MAX_SSID_LEN 32
#define PROBE_QUEUE_SIZE 25
#define MAX_LIST_MEMORY 100 // Max items in UI list before clearing

// BLE
#define BLE_RING_SIZE 32 // Size of BLE ring buffer for UI display (expanded for larger bursts)

// NVS (Non-Volatile Storage)
#define NVS_NAMESPACE "pentester"
#define NVS_BEACON_SSIDS_KEY "beacon_ssids"
#define MAX_BEACON_SSIDS 10
#define MAX_BEACON_SSID_LENGTH 32

// SD Logger File Names
#define WIFI_SCAN_LOG "/WIFI_SCAN.CSV"
#define PMKID_HASH_LOG "/PMKID.hc22000"
#define PMKID_CSV_LOG "/PMKID.CSV"
#define BLE_SNIFF_LOG "/BLE_LOG.CSV"
#define PROBE_REQ_LOG "/PROBES.TXT"