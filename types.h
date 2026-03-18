#pragma once

#include <cstdint> // Use cstdint for C++ style integer types
#include "constants.h"
#include <set> // For std::set<String>
#include <WString.h> // For String (Arduino String class)
#include <freertos/FreeRTOS.h> // For QueueHandle_t and vTaskDelay
#include <vector> // For std::vector
#include <freertos/semphr.h> // For SemaphoreHandle_t
#include <lvgl.h> // For lv_timer_t (used in PentestState)
#include <freertos/task.h> // For TaskHandle_t
#include <NimBLEDevice.h> // For NimBLEScan (used in BleState)
#include <FS.h> // For File (used in SnifferState)
#include "psram_allocator.h" // Custom allocator to force STL containers into PSRAM

// ──────────────────────────────────────────────
// Data Structures & Enums
// ──────────────────────────────────────────────
struct APRecord {
  char    ssid[33];
  uint8_t bssid[6];
  int8_t  rssi;
  uint8_t channel;
  uint8_t enc;
  int     staCount;
  bool    active;
};

struct StaRecord {
  uint8_t  mac[6];
  uint8_t  apBssid[6];
  bool     hasAP;
  int8_t   rssi;
  uint32_t lastSeen;
  bool     active;
};

// Forward declaration for AppContext (used in extern declaration)
struct AppContext;

struct TouchPoint { uint16_t x, y; bool pressed; };

struct pcap_record_t {
  uint32_t ts_sec, ts_usec, len;
  uint8_t  payload[MAX_PCAP_PACKET_SIZE];
};

struct pcap_global_header {
  uint32_t magic_number;
  uint16_t version_major, version_minor;
  int32_t  thiszone;
  uint32_t sigfigs, snaplen, network;
};

struct pcap_packet_header {
  uint32_t ts_sec, ts_usec, incl_len, orig_len;
};

enum ScanView { VIEW_AP, VIEW_STA, VIEW_LINKED };

enum PentestMode { PT_NONE, PT_DEAUTH, PT_BEACON, PT_PMKID };

// Custom struct for MAC addresses to avoid String overhead in std::set
struct MacAddress {
  uint8_t data[6];
  bool operator<(const MacAddress& other) const {
    return memcmp(data, other.data, 6) < 0;
  }
  bool operator==(const MacAddress& other) const {
    return memcmp(data, other.data, 6) == 0;
  }
};

// Custom struct for Probe SSIDs to avoid String overhead in std::set
struct ProbeSsid {
  char data[PROBE_MAX_SSID_LEN + 1];
  bool operator<(const ProbeSsid& other) const {
    return strcmp(data, other.data) < 0;
  }
};

struct StatusSnapshot {
  bool sdMounted;
  int  batteryPct;
  bool isCharging;
};

struct BLERing {
  char mac[18]; int8_t rssi; bool fresh;
};

// Define ScanState
struct ScanState {
  APRecord* ap_list;
  int ap_count;
  StaRecord* sta_list;
  int sta_count;
  ScanView view;
  bool paused;
  uint32_t last_scan_ms; // Added to track last AP scan time
  int selected_net; // Used in pentest_attacks.cpp
  int deauth_sta_target; // Used in pentest_attacks.cpp
  bool started; // Added for scan state
  lv_timer_t* scan_timer; // Added for scan timer handle
  SemaphoreHandle_t mutex; // Added for thread-safe access to ap_list/sta_list
};

// Define PentestState
struct PentestState {
  PentestMode current_mode;
  lv_timer_t* pentest_timer;
  int beacon_idx;
  bool pmkid_found;
  uint8_t pmkid_target_bssid[6]; // Used in pentest_attacks.cpp
  std::vector<String, PsramAllocator<String>> custom_beacon_ssids; // For user-defined beacon SSIDs
};

// Define SnifferState
struct SnifferState {
  volatile bool pcap_active; // Made volatile for multi-task access
  volatile bool probe_active; // Made volatile for multi-task access
  QueueHandle_t pcap_queue;
  QueueHandle_t probe_queue;
  File pcap_file;
  uint32_t pcap_packet_count;
  uint32_t last_hop_ms;
  uint8_t channel;
  bool pcap_ch_locked; // Added for channel locking
  uint8_t pcap_locked_ch; // Added for locked channel value
  std::set<ProbeSsid, std::less<ProbeSsid>, PsramAllocator<ProbeSsid>> unique_probes; // Changed from std::set<String>
  TaskHandle_t probe_task_handle; // For managing probe processing task
};

// Define BleState
struct BleState {
  bool sniff_active;
  bool flood_active;
  bool busy;
  bool nimble_ready;
  NimBLEScan* scanner; // NimBLEScan is a static object, no need to manage its memory directly
  std::set<MacAddress, std::less<MacAddress>, PsramAllocator<MacAddress>> unique_macs; // Changed from std::set<String>
  uint32_t packet_count;
  char last_mac[18];
  BLERing ring_buf[BLE_RING_SIZE];
  uint8_t ring_head;
  uint8_t ring_tail;
};

// Define StatusState
struct StatusState {
  StatusSnapshot snap;
  SemaphoreHandle_t mutex;
  TaskHandle_t service_task; // Added for Status service task handle
};

// Define LoRa Stats
struct LoraDeviceStats {
  uint32_t my_node_num;
  uint32_t uptime_seconds;
  float channel_utilization;
  float air_util_tx;
  uint32_t num_packets_tx;
  uint32_t num_packets_rx;
  uint16_t num_online_nodes;
  uint16_t num_total_nodes;
  uint32_t battery_level;
  float voltage;
  bool updated;
};

struct NodeRecord {
  uint32_t num;
  char long_name[40];
  uint32_t last_heard;
  float snr;
};

// Define LoRa/Serial State
struct LoraState {
  SemaphoreHandle_t mutex;
  TaskHandle_t service_task;
  char* log_data; // Buffer for incoming LoRa data (allocated in PSRAM)
  char* chat_data; // Buffer for LoRa chat (allocated in PSRAM)
  bool log_updated;
  bool chat_updated;
  bool nodedb_updated;
  bool unread_chat;
  LoraDeviceStats stats;
  std::vector<NodeRecord, PsramAllocator<NodeRecord>> known_nodes;
};

// Define GpsState
struct GpsState {
  double latitude;
  double longitude;
  int32_t altitude;
  uint32_t sats_in_view;
  uint32_t fix_quality;
  uint32_t last_update_ms;
  bool valid;
};

// Main AppContext
struct AppContext {
  ScanState wifi_scan;
  PentestState pentest;
  SnifferState sniffer;
  BleState ble;
  StatusState status;
  LoraState lora;
  GpsState gps;
  bool ui_busy; // Used in wifi_scanner.cpp
  bool web_server_active;
  TaskHandle_t main_task_handle;
  TaskHandle_t wifi_task_handle;
  TaskHandle_t ui_task_handle;
};

// Global AppContext instance (needs to be defined in the main .ino file)
extern AppContext g_app_context;