#include "wifi_scanner.h"
#include "constants.h"
#include "types.h"
#include <WiFi.h>
#include <esp_wifi.h>

static AppContext *context = nullptr;

// Extern declarations for UI elements
extern lv_obj_t *scan_list;
extern lv_obj_t *lbl_scan_count;
extern lv_obj_t *tabview;
extern void cb_net_selected(lv_event_t* e);

extern void trace_enter(const char *s);
extern void trace_exit(const char *s);

static bool deferred_render = false;

void wifi_scanner_init(AppContext *ctx) {
    context = ctx;
    if (context && !context->wifi_scan.mutex) {
        context->wifi_scan.mutex = xSemaphoreCreateMutex();
    }
}

const char* enc_str(uint8_t enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        default: return "UNKNOWN";
    }
}

void restore_sta_sniffer(AppContext *ctx) {
    // Deprecated: Promiscuous mode is now safely toggled dynamically by run_ap_scan and scan_tick
}

void run_ap_scan(AppContext *ctx) {
    if (!ctx) return;
    // CRITICAL: Cannot scan while promiscuous mode is enabled, it will crash the ESP32
    esp_wifi_set_promiscuous(false);
    WiFi.scanNetworks(true); // async scan
}

void render_scan_list(AppContext *ctx) {
    if (!ctx || !scan_list || !lbl_scan_count) return;

    uint32_t t0 = millis();

    // Optimization & Safety: Don't rebuild the list if the Scan tab isn't active.
    // Prevents Use-After-Free crashes when navigating away after clicking a button.
    if (tabview && lv_tabview_get_tab_act(tabview) != 1) { // 1 is the Scan tab
        deferred_render = true;
        return;
    }

    lv_indev_t * indev = lv_indev_get_next(NULL);
    bool is_scrolling = scan_list ? lv_obj_is_scrolling(scan_list) : false;
    if ((indev && indev->proc.state == LV_INDEV_STATE_PR) || is_scrolling) {
        deferred_render = true;
        return;
    }
    deferred_render = false;

    static ScanView last_view = (ScanView)-1;

    char buf[128];
    if (ctx->wifi_scan.mutex && xSemaphoreTake(ctx->wifi_scan.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (last_view != ctx->wifi_scan.view) {
            lv_obj_clean(scan_list);
            last_view = ctx->wifi_scan.view;
        }

        snprintf(buf, sizeof(buf), "APs: %d  STAs: %d", ctx->wifi_scan.ap_count, ctx->wifi_scan.sta_count);
        lv_label_set_text(lbl_scan_count, buf);
        
        if (ctx->wifi_scan.view == VIEW_AP) {
            uint32_t child_idx = 0;
            for (int i = 0; i < ctx->wifi_scan.ap_count; i++) {
                if (ctx->wifi_scan.ap_list[i].active) {
                    char bss[18];
                    sprintf(bss, "%02X:%02X:%02X:%02X:%02X:%02X", 
                            ctx->wifi_scan.ap_list[i].bssid[0], ctx->wifi_scan.ap_list[i].bssid[1],
                            ctx->wifi_scan.ap_list[i].bssid[2], ctx->wifi_scan.ap_list[i].bssid[3],
                            ctx->wifi_scan.ap_list[i].bssid[4], ctx->wifi_scan.ap_list[i].bssid[5]);
                    char txt[128];
                    snprintf(txt, sizeof(txt), "[%s] %s (Ch:%d, %ddBm)", enc_str(ctx->wifi_scan.ap_list[i].enc), ctx->wifi_scan.ap_list[i].ssid, ctx->wifi_scan.ap_list[i].channel, ctx->wifi_scan.ap_list[i].rssi);
                    
                    lv_obj_t *btn = lv_obj_get_child(scan_list, child_idx);
                    if (!btn || lv_obj_get_child_cnt(btn) < 2) {
                        if (btn) lv_obj_del(btn);
                        btn = lv_list_add_btn(scan_list, LV_SYMBOL_WIFI, txt);
                        if (btn) lv_obj_add_event_cb(btn, cb_net_selected, LV_EVENT_CLICKED, nullptr);
                    } else {
                        lv_obj_t *label = lv_obj_get_child(btn, 1);
                        if (label) lv_label_set_text(label, txt);
                    }
                    if (btn) lv_obj_set_user_data(btn, (void*)(intptr_t)i);
                    child_idx++;
                }
            }
            while (lv_obj_get_child_cnt(scan_list) > child_idx) {
                lv_obj_del(lv_obj_get_child(scan_list, child_idx));
            }
        } else if (ctx->wifi_scan.view == VIEW_STA) {
            uint32_t child_idx = 0;
             for (int i = 0; i < ctx->wifi_scan.sta_count; i++) {
                if (ctx->wifi_scan.sta_list[i].active) {
                    char mac[18];
                    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", 
                            ctx->wifi_scan.sta_list[i].mac[0], ctx->wifi_scan.sta_list[i].mac[1],
                            ctx->wifi_scan.sta_list[i].mac[2], ctx->wifi_scan.sta_list[i].mac[3],
                            ctx->wifi_scan.sta_list[i].mac[4], ctx->wifi_scan.sta_list[i].mac[5]);
                    char txt[128];
                    snprintf(txt, sizeof(txt), "%s (%ddBm)", mac, ctx->wifi_scan.sta_list[i].rssi);
                    
                    lv_obj_t *btn = lv_obj_get_child(scan_list, child_idx);
                    if (!btn || lv_obj_get_child_cnt(btn) < 2) {
                        if (btn) lv_obj_del(btn);
                        btn = lv_list_add_btn(scan_list, LV_SYMBOL_BLUETOOTH, txt);
                    } else {
                        lv_obj_t *label = lv_obj_get_child(btn, 1);
                        if (label) lv_label_set_text(label, txt);
                    }
                    if (btn) lv_obj_set_user_data(btn, (void*)(intptr_t)i);
                    child_idx++;
                }
            }
            while (lv_obj_get_child_cnt(scan_list) > child_idx) {
                lv_obj_del(lv_obj_get_child(scan_list, child_idx));
            }
        } else if (ctx->wifi_scan.view == VIEW_LINKED) {
            lv_obj_clean(scan_list); // Always clean for LINKED due to mixed obj types
            bool found_any = false;
            
            for (int i = 0; i < ctx->wifi_scan.ap_count; i++) {
                if (!ctx->wifi_scan.ap_list[i].active) continue;
                
                bool has_linked_stas = false;
                for (int j = 0; j < ctx->wifi_scan.sta_count; j++) {
                    if (ctx->wifi_scan.sta_list[j].active && ctx->wifi_scan.sta_list[j].hasAP &&
                        memcmp(ctx->wifi_scan.sta_list[j].apBssid, ctx->wifi_scan.ap_list[i].bssid, 6) == 0) {
                        has_linked_stas = true;
                        break;
                    }
                }
                
                if (has_linked_stas) {
                    char txt[128];
                    snprintf(txt, sizeof(txt), "AP: %s", ctx->wifi_scan.ap_list[i].ssid);
                    lv_list_add_text(scan_list, txt);
                    
                    for (int j = 0; j < ctx->wifi_scan.sta_count; j++) {
                        if (ctx->wifi_scan.sta_list[j].active && ctx->wifi_scan.sta_list[j].hasAP &&
                            memcmp(ctx->wifi_scan.sta_list[j].apBssid, ctx->wifi_scan.ap_list[i].bssid, 6) == 0) {
                            
                            char mac[18];
                            mac_str(ctx->wifi_scan.sta_list[j].mac, mac);
                            snprintf(txt, sizeof(txt), "  %s (%ddBm)", mac, ctx->wifi_scan.sta_list[j].rssi);
                            
                            lv_obj_t *btn = lv_list_add_btn(scan_list, LV_SYMBOL_RIGHT, txt);
                            if (btn) {
                                lv_obj_set_user_data(btn, (void*)(intptr_t)i); // Pass AP index for targeting
                                lv_obj_add_event_cb(btn, cb_net_selected, LV_EVENT_CLICKED, nullptr);
                                found_any = true;
                            }
                        }
                    }
                }
            }
            
            if (!found_any) {
                lv_list_add_text(scan_list, "No associated clients found yet");
            }
        }

        xSemaphoreGive(ctx->wifi_scan.mutex);
    }

    uint32_t dt = millis() - t0;
    if (dt > 10) {
        Serial.printf("[DIAG] slow: render_scan_list %lu ms\n", (unsigned long)dt);
    }
}

void set_promiscuous_channel(uint8_t ch) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void mac_str(const uint8_t *mac, char *out) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void scan_tick(lv_timer_t *timer) {
    uint32_t start = millis();
    trace_enter("scan_tick");
    
    AppContext *ctx = (AppContext *)timer->user_data;
    if (!ctx || ctx->wifi_scan.paused) { 
        trace_exit("scan_tick"); 
        if (millis() - start > 10) {
            Serial.printf("[DIAG] slow: scan_tick %lu ms\n", (unsigned long)(millis() - start));
        }
        return; 
    }

    if (deferred_render) {
        lv_indev_t * indev = lv_indev_get_next(NULL);
        bool is_scrolling = scan_list ? lv_obj_is_scrolling(scan_list) : false;
        if ((!indev || indev->proc.state == LV_INDEV_STATE_REL) && !is_scrolling) {
            render_scan_list(ctx);
        }
    }

    // Check if scan is complete
    int16_t n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) {
        trace_exit("scan_tick");
        if (millis() - start > 10) {
            Serial.printf("[DIAG] slow: scan_tick %lu ms\n", (unsigned long)(millis() - start));
        }
        return; // Wait for the current scan to finish to prevent driver panic
    }

    if (n >= 0) { // Scan finished successfully
        if (ctx->wifi_scan.mutex && xSemaphoreTake(ctx->wifi_scan.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ctx->wifi_scan.ap_count = 0;
            for (int i = 0; i < n && i < MAX_APS; i++) {
                APRecord &r = ctx->wifi_scan.ap_list[i];
                strncpy(r.ssid, WiFi.SSID(i).c_str(), sizeof(r.ssid) - 1);
                r.ssid[sizeof(r.ssid) - 1] = '\0';
                memcpy(r.bssid, WiFi.BSSID(i), 6);
                r.rssi = WiFi.RSSI(i);
                r.channel = WiFi.channel(i);
                r.enc = WiFi.encryptionType(i);
                r.active = true;
                ctx->wifi_scan.ap_count++;
            }
            xSemaphoreGive(ctx->wifi_scan.mutex);
        }
        WiFi.scanDelete();
        render_scan_list(ctx);
        
        // Re-enable promiscuous mode after AP scan is fully processed to allow STA sniffing
        if (!ctx->wifi_scan.paused) {
            esp_wifi_set_promiscuous(true);
        }
    }
    else if (n == WIFI_SCAN_FAILED) {
        // Reset the scanner if it failed so it can try again cleanly
        WiFi.scanDelete();
    }

    // Only start a new scan if enough time has passed and we aren't already scanning
    if (millis() - ctx->wifi_scan.last_scan_ms > AP_SCAN_INTERVAL_MS) {
        run_ap_scan(ctx);
        ctx->wifi_scan.last_scan_ms = millis();
    }
    trace_exit("scan_tick");
    
    uint32_t dt = millis() - start;
    if (dt > 10) {
        Serial.printf("[DIAG] slow: scan_tick %lu ms\n", (unsigned long)dt);
    }
}