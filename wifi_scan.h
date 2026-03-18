#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "types.h"
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void wifi_scan_init(AppContext *ctx);
const char* enc_str(uint8_t enc);   // <-- added declaration

void sta_sniffer_restore(AppContext *ctx);
void ap_scan_start(AppContext *ctx);
void scan_list_render(AppContext *ctx);
void promiscuous_channel_set(uint8_t ch);
void mac_str(const uint8_t *mac, char *out);
void wifi_scan_tick(lv_timer_t *timer);

#ifdef __cplusplus
}
#endif