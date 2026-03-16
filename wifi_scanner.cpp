#include "wifi_scanner.h"
#include "constants.h"
#include "sd_logger.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>

// Forward declarations for UI elements from ui_module.h
extern lv_obj_t *scan_list, *lbl_scan_count, *btn_view_ap, *btn_view_sta, *btn_view_linked;
extern lv_style_t style_view_active, style_view_inactive;
extern void cb_net_selected(lv_event_t *e);


static AppContext* scanner_context = nullptr;

// Helper functions for rendering, previously globals
static const char* enc_str(uint8_t enc) {
  switch(enc) {
    case WIFI_AUTH_OPEN:          return "OPEN";
    case WIFI_AUTH_WEP:           return "WEP";
    case WIFI_AUTH_WPA_PSK:       return "WPA";
    case WIFI_AUTH_WPA2_PSK:      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default:                      return "????";
  }
}

static int rssi_bars(int8_t rssi) {
  if (rssi >= -55) return 4; if (rssi >= -65) return 3;
  if (rssi >= -75) return 2; if (rssi >= -85) return 1;
  return 0;
}

static const char* bar_str(int b) {
  switch(b) {
    case 4: return "████";
    case 3: return "███░";
    case 2: return "██░░";
    case 1: return "█░░░";
    default:return "░░░░";
  }
}

static uint32_t bar_color(int b) {
  switch(b) {
    case 4: return LV_COLOR_GREEN; case 3: return 0xAAFF00;
    case 2: return LV_COLOR_YELLOW; case 1: return LV_COLOR_RED;
    default:return LV_COLOR_DARK_GRAY;
  }
}

void mac_str(const uint8_t *m, char *out) {
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0],m[1],m[2],m[3],m[4],m[5]);
}


void wifi_scanner_init(AppContext* context) {
    scanner_context = context;
}

void set_promiscuous_channel(uint8_t ch) {
    // Fully stop/restart so esp_wifi_80211_tx has a valid STA interface
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    delay(50); // let the interface come up before any tx attempts
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void restore_sta_sniffer(AppContext* context) {
    if (context->sniffer.pcap_active || context->sniffer.probe_active) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    delay(20);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(sta_sniffer_cb);
}

static void process_sta_frame(const uint8_t *data, int len, int8_t rssi) {
    if (!scanner_context || len < 24) return;

    ScanState& state = scanner_context->wifi_scan;

    uint8_t fc0 = data[0];
    uint8_t fc1 = data[1];
    uint8_t type = (fc0 & 0x0C) >> 2;
    uint8_t subtype = (fc0 & 0xF0) >> 4;
    uint8_t toDS = (fc1 & 0x01);
    uint8_t fromDS = (fc1 & 0x02) >> 1;
    const uint8_t *addr1 = data + 4;
    const uint8_t *addr2 = data + 10;
    
    if (addr2[0] == 0xFF || addr2[0] & 0x01) return;

    const uint8_t *sta_mac = nullptr;
    const uint8_t *ap_mac = nullptr;
    bool linked = false;

    if (type == 2) { // Data frame
        if (toDS == 1 && fromDS == 0)      { sta_mac = addr2; ap_mac = addr1; linked = true; }
        else if (toDS == 0 && fromDS == 1) { sta_mac = addr1; ap_mac = addr2; linked = true; if (sta_mac[0] == 0xFF || sta_mac[0] & 0x01) return; }
        else if (toDS == 0 && fromDS == 0) { sta_mac = addr2; }
    } else if (type == 0 && (subtype == 4 || subtype == 0 || subtype == 2)) { // Mgmt: Probe Req, Assoc Req, Reassoc Req
        sta_mac = addr2;
        if (subtype != 4) { ap_mac = addr1; linked = true; }
    }

    if (!sta_mac) return;
    
    for (int i = 0; i < state.ap_count; i++) if (memcmp(state.ap_list[i].bssid, sta_mac, 6) == 0) return;
    
    int slot = -1;
    for (int i = 0; i < state.sta_count; i++) {
        if (state.sta_list[i].active && memcmp(state.sta_list[i].mac, sta_mac, 6) == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        if (state.sta_count >= MAX_STAS) return;
        slot = state.sta_count++;
        memset(&state.sta_list[slot], 0, sizeof(StaRecord));
        memcpy(state.sta_list[slot].mac, sta_mac, 6);
        state.sta_list[slot].active = true;
    }

    state.sta_list[slot].rssi = rssi;
    state.sta_list[slot].lastSeen = millis();
    if (linked && ap_mac && !(ap_mac[0] == 0xFF)) {
        memcpy(state.sta_list[slot].apBssid, ap_mac, 6);
        state.sta_list[slot].hasAP = true;
    }
}

void sta_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type == WIFI_PKT_MISC || !scanner_context) return;
    if (scanner_context->pentest.current_mode == PT_PMKID) return;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    process_sta_frame(pkt->payload, pkt->rx_ctrl.sig_len, pkt->rx_ctrl.rssi);
}

void run_ap_scan(AppContext* context) {
    if (context->sniffer.pcap_active || context->sniffer.probe_active) return;
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_task_wdt_reset();
    // max_ms_per_chan=100 keeps total scan under ~1.4s; guard against WIFI_SCAN_FAILED
    int n = WiFi.scanNetworks(false, true, false, 100);
    esp_task_wdt_reset();
    if (n == WIFI_SCAN_FAILED || n < 0) n = 0;

    for (int i = 0; i < n; i++) {
        uint8_t *bssid = WiFi.BSSID(i);
        int slot = -1;
        for (int j = 0; j < context->wifi_scan.ap_count; j++) {
            if (memcmp(context->wifi_scan.ap_list[j].bssid, bssid, 6) == 0) {
                slot = j;
                break;
            }
        }
        if (slot == -1) {
            if (context->wifi_scan.ap_count >= MAX_APS) continue;
            slot = context->wifi_scan.ap_count++;
        }
        strncpy(context->wifi_scan.ap_list[slot].ssid, WiFi.SSID(i).c_str(), 32);
        context->wifi_scan.ap_list[slot].ssid[32] = 0;
        memcpy(context->wifi_scan.ap_list[slot].bssid, bssid, 6);
        context->wifi_scan.ap_list[slot].rssi = WiFi.RSSI(i);
        context->wifi_scan.ap_list[slot].channel = WiFi.channel(i);
        context->wifi_scan.ap_list[slot].enc = WiFi.encryptionType(i);
        context->wifi_scan.ap_list[slot].active = true;
    }
    WiFi.scanDelete();
    sd_log_scan(context);

    for (int i = 0; i < context->wifi_scan.ap_count; i++) context->wifi_scan.ap_list[i].staCount = 0;
    for (int i = 0; i < context->wifi_scan.sta_count; i++) {
        if (context->wifi_scan.sta_list[i].active && context->wifi_scan.sta_list[i].hasAP) {
            for (int j = 0; j < context->wifi_scan.ap_count; j++) {
                if (memcmp(context->wifi_scan.ap_list[j].bssid, context->wifi_scan.sta_list[i].apBssid, 6) == 0) {
                    context->wifi_scan.ap_list[j].staCount++;
                    break;
                }
            }
        }
    }
    restore_sta_sniffer(context);
}

void expire_stas(AppContext* context) {
    uint32_t now = millis();
    for (int i = 0; i < context->wifi_scan.sta_count; i++) {
        if (context->wifi_scan.sta_list[i].active && now - context->wifi_scan.sta_list[i].lastSeen > STA_TIMEOUT_MS) {
            context->wifi_scan.sta_list[i].active = false;
        }
    }
}

void render_scan_list(AppContext* context) {
    if (context->ui_busy) return;
    lv_obj_clean(scan_list);
    int la = 0, ls = 0;
    for (int i = 0; i < context->wifi_scan.ap_count; i++) if (context->wifi_scan.ap_list[i].active) la++;
    for (int i = 0; i < context->wifi_scan.sta_count; i++) if (context->wifi_scan.sta_list[i].active) ls++;
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "APs: %d  STAs: %d", la, ls);
    lv_label_set_text(lbl_scan_count, hdr);

    ScanState& state = context->wifi_scan;

    if (state.view == VIEW_AP) {
        for (int i = 0; i < state.ap_count; i++) {
            if (!state.ap_list[i].active) continue;
            int bars = rssi_bars(state.ap_list[i].rssi);
            lv_obj_t *btn = lv_list_add_btn(scan_list, nullptr, "");
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, cb_net_selected, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_style_bg_color(btn, lv_color_hex(LV_COLOR_DARK_BG), 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 4, 0);
            lv_obj_set_height(btn, LV_SIZE_CONTENT);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_recolor(lbl, true);
            char bss[18];
            mac_str(state.ap_list[i].bssid, bss);
            char full[140];
            snprintf(full, sizeof(full),
                     "#00FFCC %-16.16s#  #FFFF00 CH%-2d#  #AAAAAA %s#  #%06lX %s#\n"
                     "#777777 %s#  #AAAAAA %ddBm#  #00FF88 STA:%d#",
                     state.ap_list[i].ssid, state.ap_list[i].channel, enc_str(state.ap_list[i].enc),
                     (unsigned long)bar_color(bars), bar_str(bars),
                     bss, state.ap_list[i].rssi, state.ap_list[i].staCount);
            lv_label_set_text(lbl, full);
            lv_obj_set_width(lbl, SCREEN_W - 16);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        }
    } else if (state.view == VIEW_STA) {
        for (int i = 0; i < state.sta_count; i++) {
            if (!state.sta_list[i].active) continue;
            char mac[18];
            mac_str(state.sta_list[i].mac, mac);
            bool is_target = (context->wifi_scan.deauth_sta_target == i); // Corrected access
            lv_obj_t *btn = lv_list_add_btn(scan_list, nullptr, "");
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                AppContext* ctx = scanner_context;
                int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
                if (idx < 0 || idx >= ctx->wifi_scan.sta_count || !ctx->wifi_scan.sta_list[idx].active) return;
                ctx->wifi_scan.deauth_sta_target = (ctx->wifi_scan.deauth_sta_target == idx) ? -1 : idx; // Corrected access
                if (ctx->wifi_scan.deauth_sta_target >= 0 && ctx->wifi_scan.sta_list[idx].hasAP) { // Corrected access
                    for (int j = 0; j < ctx->wifi_scan.ap_count; j++) {
                        if (memcmp(ctx->wifi_scan.ap_list[j].bssid, ctx->wifi_scan.sta_list[idx].apBssid, 6) == 0) {
                            ctx->wifi_scan.selected_net = j; // Corrected access
                            break;
                        }
                    }
                }
                render_scan_list(ctx);
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_style_bg_color(btn, lv_color_hex(is_target ? 0x200808 : LV_COLOR_DARK_BG), 0);
            lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
            lv_obj_set_style_border_color(btn, lv_color_hex(is_target ? LV_COLOR_RED : LV_COLOR_BLUE), LV_PART_MAIN);
            lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
            lv_obj_set_style_pad_all(btn, 4, 0);
            lv_obj_set_height(btn, LV_SIZE_CONTENT);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_recolor(lbl, true);
            char full[140];
            if (state.sta_list[i].hasAP) {
                const char *an = "?";
                for (int j = 0; j < state.ap_count; j++) {
                    if (memcmp(state.ap_list[j].bssid, state.sta_list[i].apBssid, 6) == 0) {
                        an = state.ap_list[j].ssid;
                        break;
                    }
                }
                snprintf(full, sizeof(full),
                         "#%s STA# #FFFFFF %s#%s\n#777777 AP:#  #FFAA00 %-14.14s#  #AAAAAA %ddBm#",
                         is_target ? "FF4444" : "00AAFF", mac, is_target ? " #FF4444 [TGT]#" : "", an, state.sta_list[i].rssi);
            } else {
                snprintf(full, sizeof(full),
                         "#%s STA# #FFFFFF %s#%s\n#777777 AP: unassoc#  #AAAAAA %ddBm#",
                         is_target ? "FF4444" : "00AAFF", mac, is_target ? " #FF4444 [TGT]#" : "", state.sta_list[i].rssi);
            }
            lv_label_set_text(lbl, full);
            lv_obj_set_width(lbl, SCREEN_W - 16);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        }
    } else { // VIEW_LINKED
        for (int i = 0; i < state.ap_count; i++) {
            if (!state.ap_list[i].active) continue;
            int bars = rssi_bars(state.ap_list[i].rssi);
            char bss[18];
            mac_str(state.ap_list[i].bssid, bss);
            lv_obj_t *ab = lv_list_add_btn(scan_list, nullptr, "");
            lv_obj_set_user_data(ab, (void*)(intptr_t)i);
            lv_obj_add_event_cb(ab, cb_net_selected, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_style_bg_color(ab, lv_color_hex(LV_COLOR_LIGHT_BG), 0);
            lv_obj_set_style_border_side(ab, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
            lv_obj_set_style_border_color(ab, lv_color_hex(LV_COLOR_CYAN), LV_PART_MAIN);
            lv_obj_set_style_border_width(ab, 2, LV_PART_MAIN);
            lv_obj_set_style_pad_all(ab, 4, 0);
            lv_obj_set_height(ab, LV_SIZE_CONTENT);
            lv_obj_t *al = lv_label_create(ab);
            lv_label_set_recolor(al, true);
            char ar[120];
            snprintf(ar, sizeof(ar),
                     "#00FFCC %-16.16s#  #%06lX %s#\n#777777 %s#  #FFFF00 CH%-2d#  #AAAAAA %s  %ddBm#",
                     state.ap_list[i].ssid, (unsigned long)bar_color(bars), bar_str(bars),
                     bss, state.ap_list[i].channel, enc_str(state.ap_list[i].enc), state.ap_list[i].rssi);
            lv_label_set_text(al, ar);
            lv_obj_set_width(al, SCREEN_W - 20);
            lv_label_set_long_mode(al, LV_LABEL_LONG_WRAP);

            for (int j = 0; j < state.sta_count; j++) {
                if (!state.sta_list[j].active || !state.sta_list[j].hasAP) continue;
                if (memcmp(state.sta_list[j].apBssid, state.ap_list[i].bssid, 6) != 0) continue;
                char sm[18];
                mac_str(state.sta_list[j].mac, sm);
                lv_obj_t *sb = lv_list_add_btn(scan_list, nullptr, "");
                lv_obj_set_style_bg_color(sb, lv_color_hex(0x080808), 0);
                lv_obj_set_style_border_width(sb, 0, 0);
                lv_obj_set_style_pad_left(sb, 20, 0);
                lv_obj_set_style_pad_top(sb, 2, 0);
                lv_obj_set_style_pad_bottom(sb, 2, 0);
                lv_obj_set_height(sb, LV_SIZE_CONTENT);
                lv_obj_t *sl = lv_label_create(sb);
                lv_label_set_recolor(sl, true);
                char sr[60];
                snprintf(sr, sizeof(sr), "#555555 └#  #00AAFF %s#  #777777 %ddBm#", sm, state.sta_list[j].rssi);
                lv_label_set_text(sl, sr);
                lv_obj_set_width(sl, SCREEN_W - 28);
            }
        }
    }
}

// tabview is defined in ui_module.cpp - reference directly to avoid fragile parent traversal
extern lv_obj_t *tabview;

void scan_tick(lv_timer_t * timer) {
    AppContext* context = (AppContext*)timer->user_data;
    if (!context || context->ui_busy || context->wifi_scan.paused || context->pentest.current_mode != PT_NONE) return;
    if (!tabview || !scan_list) return;
    if (lv_tabview_get_tab_act(tabview) != 1) return; // Assuming tab 1 is the scan tab
    
    expire_stas(context); // Remove old STAs
    uint32_t now = millis();
    if (now - context->wifi_scan.last_scan_ms > AP_SCAN_INTERVAL_MS) { // AP_SCAN_INTERVAL_MS defined in constants.h
        run_ap_scan(context); // This function performs the AP scan
        context->wifi_scan.last_scan_ms = now;
    }
    render_scan_list(context);
}