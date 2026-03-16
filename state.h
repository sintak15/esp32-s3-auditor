#pragma once

#include <Arduino.h>
#include <FS.h>
#include <lvgl.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <set>
#include "types.h"
#include "constants.h"

// Encapsulates all GPS-related data
struct GpsState {
    GpsSnapshot snap;
    SemaphoreHandle_t mutex;
    TaskHandle_t service_task;
};

// Encapsulates general device status
struct StatusState {
    StatusSnapshot snap;
    SemaphoreHandle_t mutex;
    TaskHandle_t service_task;
};

// Encapsulates all state related to WiFi scanning
struct ScanState {
    APRecord ap_list[MAX_APS];
    StaRecord sta_list[MAX_STAS];
    int ap_count = 0;
    int sta_count = 0;
    
    ScanView view = VIEW_AP;
    bool paused = true;
    bool started = false;
    uint32_t last_scan_ms = 0;
    lv_timer_t* scan_timer = nullptr;
};

// Encapsulates state for all pentesting activities
struct PentestState {
    PentestMode current_mode = PT_NONE;
    lv_timer_t* pentest_timer = nullptr;
    int beacon_idx = 0;

    // PMKID attack state
    uint8_t pmkid_target_bssid[6];
    bool pmkid_found = false;

    // Deauth attack state
    int deauth_sta_target = -1; // -1 for broadcast
    int selected_net = -1; // AP index for targeting
};

// Encapsulates all BLE-related state
struct BleState {
    bool nimble_ready = false;
    volatile bool busy = false;
    bool flood_active = false;
    bool sniff_active = false;

    std::set<std::string> unique_macs;
    uint32_t packet_count = 0;
    String last_mac;
    NimBLEScan* scanner = nullptr;

    BLERing ring_buf[BLE_RING_SIZE];
    volatile uint8_t ring_head = 0;
};

// Encapsulates state for PCAP and Probe sniffing
struct SnifferState {
    // PCAP state
    volatile bool pcap_busy = false;
    bool pcap_active = false;
    File pcap_file;
    uint32_t pcap_packet_count = 0;
    QueueHandle_t pcap_queue = nullptr;
    uint8_t channel = 1;
    uint32_t last_hop_ms = 0;

    // Probe state
    volatile bool probe_busy = false;
    bool probe_active = false;
    std::set<String> unique_probes;
    QueueHandle_t probe_queue = nullptr;
};

// Main application context gathering all state
struct AppContext {
    GpsState gps;
    StatusState status;
    ScanState wifi_scan;
    PentestState pentest;
    BleState ble;
    SnifferState sniffer;

    // Global flag to prevent UI updates during transitions
    bool ui_busy = false;
};
