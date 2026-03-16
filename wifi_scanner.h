#pragma once

#include "types.h" // Changed from state.h to types.h
#include <esp_wifi_types.h>

void wifi_scanner_init(AppContext* context);
void run_ap_scan(AppContext* context);
void expire_stas(AppContext* context);
void restore_sta_sniffer(AppContext* context);
void render_scan_list(AppContext* context);
void set_promiscuous_channel(uint8_t ch);
void sta_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type);
void scan_tick(lv_timer_t * timer);
void mac_str(const uint8_t *m, char *out);
