#pragma once

// ──────────────────────────────────────────────
//              PIN DEFINITIONS
// ──────────────────────────────────────────────
#define I2C_SDA         16
#define I2C_SCL         15
#define TP_RST          18
#define TFT_BL          45
#define BATT_ADC         9
#define GPS_RX_PIN      43
#define GPS_TX_PIN      44

// ──────────────────────────────────────────────
//              DISPLAY & TOUCH
// ──────────────────────────────────────────────
#define SCREEN_W        240
#define SCREEN_H        320
#define TOUCH_ADDR      0x38

// ──────────────────────────────────────────────
//              SCAN & TIMING
// ──────────────────────────────────────────────
#define MAX_APS                 40
#define MAX_STAS                60
#define STA_TIMEOUT_MS          30000
#define AP_SCAN_INTERVAL_MS     5000 // Interval for AP scanning
#define SCAN_INTERVAL_MS        5000
#define MAX_LIST_MEMORY         500
#define BEACON_SSID_COUNT       10
#define BLE_RING_SIZE           8

// ──────────────────────────────────────────────
//              PCAP & SNIFFING
// ──────────────────────────────────────────────
#define MAX_PCAP_PACKET_SIZE    256
#define PCAP_QUEUE_SIZE         20
#define PROBE_QUEUE_SIZE        50
#define PROBE_MAX_SSID_LEN      32
#define CHANNEL_HOP_INTERVAL_MS 250

// ──────────────────────────────────────────────
//              FILE PATHS
// ──────────────────────────────────────────────
#define WIFI_SCAN_LOG   "/WIFI_SCAN.CSV"
#define PMKID_HASH_LOG  "/PMKID.hc22000"
#define PMKID_CSV_LOG   "/PMKID.csv"
#define BLE_SNIFF_LOG   "/BLE_LOG.CSV"
#define PROBE_REQ_LOG   "/PROBES.CSV"

// ──────────────────────────────────────────────
//                 UI COLORS
// ──────────────────────────────────────────────
#define LV_COLOR_DARK_BG        0x0E0E0E
#define LV_COLOR_LIGHT_BG       0x111822
#define LV_COLOR_DARK_GRAY      0x444444
#define LV_COLOR_GRAY           0x777777
#define LV_COLOR_LIGHT_GRAY     0xAAAAAA
#define LV_COLOR_WHITE          0xFFFFFF
#define LV_COLOR_CYAN           0x00FFCC
#define LV_COLOR_GREEN          0x00FF88
#define LV_COLOR_YELLOW         0xFFAA00
#define LV_COLOR_ORANGE         0xFF8800
#define LV_COLOR_RED            0xFF4444
#define LV_COLOR_BLUE           0x00AAFF
#define LV_COLOR_PURPLE         0x8800FF

// ──────────────────────────────────────────────
//              FAKE BEACON SSIDS
// ──────────────────────────────────────────────
// Note: BEACON_SSID_COUNT must match the number of items
static const char *FAKE_BEACON_SSIDS[BEACON_SSID_COUNT] = {
  "FBI_Surveillance_Van","Not_Your_WiFi","Pretty_Fly_4_A_WiFi",
  "Tell_My_WiFi_Love_Her","Virus.exe","SkyNet_Node_7",
  "DEA_Unit_12","NSA_Listening_Post","Router_of_Doom","Free_5G_COVID"
};
