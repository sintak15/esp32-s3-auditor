#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "types.h"
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void wifi_scanner_init(AppContext *ctx);
const char* enc_str(uint8_t enc);   // <-- added declaration

void restore_sta_sniffer(AppContext *ctx);
void run_ap_scan(AppContext *ctx);
void render_scan_list(AppContext *ctx);
void set_promiscuous_channel(uint8_t ch);
void mac_str(const uint8_t *mac, char *out);
void scan_tick(lv_timer_t *timer);

#ifdef __cplusplus
}
#endif

// C++ specific declarations
void IRAM_ATTR add_or_update_sta(AppContext* ctx, const uint8_t* mac, const uint8_t* ap_bssid, int8_t rssi, bool hasAP);