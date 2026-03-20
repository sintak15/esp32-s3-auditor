#pragma once

#include <Arduino.h>
#include "types.h" // Changed from state.h to types.h
#include <esp_wifi_types.h>

void audit_actions_init(AppContext* context);
void stop_audit_action(AppContext* context);

// LVGL timer callbacks for active audit actions
void reconnect_tick(lv_timer_t *timer);
void beacon_tick(lv_timer_t *timer);
void pmkid_tick(lv_timer_t *timer);

// NVS functions for beacon SSIDs
void load_beacon_ssids_from_nvs(AppContext* context);
void save_beacon_ssids_to_nvs(AppContext* context);

// Promiscuous callback for PMKID monitoring
void IRAM_ATTR pmkid_monitor_cb(void *buf, wifi_promiscuous_pkt_type_t type);

// Helper: randomize the STA MAC (use responsibly)
void randomize_wifi_mac();
