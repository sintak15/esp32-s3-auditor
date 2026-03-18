#pragma once

#include "types.h" // Changed from state.h to types.h
#include <esp_wifi_types.h>

void wifi_analysis_init(AppContext* context);
void start_deauth_demonstration(AppContext* context);
void start_beacon_spam(AppContext* context);
void start_pmkid_capture(AppContext* context);
void stop_analysis(AppContext* context);

// LVGL timer callbacks for analysis
void deauth_demonstration_tick(lv_timer_t *timer);
void beacon_spam_tick(lv_timer_t *timer);
void pmkid_capture_tick(lv_timer_t *timer);

// NVS functions for beacon SSIDs
void load_beacon_ssids_from_nvs(AppContext* context);
void save_beacon_ssids_to_nvs(AppContext* context);
// Promiscuous callback for PMKID sniffing
void pmkid_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type);
