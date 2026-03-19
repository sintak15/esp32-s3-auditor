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

// Helper to add/update STA records - Marked IRAM_ATTR for safe calling from promiscuous callback
void IRAM_ATTR add_or_update_sta(AppContext* ctx, const uint8_t* mac, const uint8_t* ap_bssid, int8_t rssi, bool hasAP) {
    if (!ctx || !ctx->wifi_scan.mutex || !ctx->wifi_scan.sta_list) return;

    // Use a non-blocking take for the mutex, as this is called from promiscuous callback
    // or a high-priority task. If it fails, we drop the packet for this cycle.
    if (xSemaphoreTake(ctx->wifi_scan.mutex, 0) == pdTRUE) {
        const uint32_t now = millis();
        int reuse_idx = -1;

        for (int i = 0; i < ctx->wifi_scan.sta_count; i++) {
            StaRecord& sta = ctx->wifi_scan.sta_list[i];

            if (memcmp(sta.mac, mac, 6) == 0) {
                sta.rssi = rssi;
                sta.lastSeen = now;
                sta.active = true;
                if (hasAP && ap_bssid) {
                    memcpy(sta.apBssid, ap_bssid, 6);
                    sta.hasAP = true;
                }
                xSemaphoreGive(ctx->wifi_scan.mutex);
                return;
            }

            if (reuse_idx < 0 && !sta.active) {
                reuse_idx = i;
            }
        }

        int insert_idx = -1;
        if (reuse_idx >= 0) {
            insert_idx = reuse_idx;
        } else if (ctx->wifi_scan.sta_count < MAX_STAS) {
            insert_idx = ctx->wifi_scan.sta_count++;
        }

        if (insert_idx >= 0) {
            StaRecord& new_sta = ctx->wifi_scan.sta_list[insert_idx];
            memcpy(new_sta.mac, mac, 6);
            new_sta.rssi = rssi;
            new_sta.lastSeen = now;
            new_sta.active = true;
            if (hasAP && ap_bssid) {
                memcpy(new_sta.apBssid, ap_bssid, 6);
                new_sta.hasAP = true;
            } else {
                memset(new_sta.apBssid, 0, sizeof(new_sta.apBssid));
                new_sta.hasAP = false;
            }
        }
        xSemaphoreGive(ctx->wifi_scan.mutex);
    }
}

void render_scan_list(AppContext *ctx) {
    if (!ctx || !scan_list || !lbl_scan_count) return;

    uint32_t t0 = millis();

    // Don't waste time rebuilding if Scan tab isn't visible.
    if (tabview && lv_tabview_get_tab_act(tabview) != 1) { // 1 = Scan tab
        deferred_render = true;
        return;
    }

    // Avoid rebuilding while the user is touching / scrolling.
    lv_indev_t * indev = lv_indev_get_next(NULL);
    bool is_scrolling = scan_list ? lv_obj_is_scrolling(scan_list) : false;
    if ((indev && indev->proc.state == LV_INDEV_STATE_PR) || is_scrolling) {
        deferred_render = true;
        return;
    }
    deferred_render = false;

    // Throttle UI rebuilds a bit. This alone helps a lot.
    static uint32_t last_render_ms = 0;
    if (millis() - last_render_ms < 300) return;
    last_render_ms = millis();

    static ScanView last_view = (ScanView)-1;
    static bool pool_initialized = false;

    // Fixed UI pool: no delete/recreate churn every scan.
    // Keep this conservative to avoid over-allocating LVGL objects.
    static constexpr uint16_t MAX_SCAN_UI_ITEMS = 40;
    static lv_obj_t *item_btns[MAX_SCAN_UI_ITEMS] = {nullptr};

    auto ensure_pool = [&]() {
        if (pool_initialized) return;

        for (uint16_t i = 0; i < MAX_SCAN_UI_ITEMS; i++) {
            lv_obj_t *btn = lv_list_add_btn(scan_list, LV_SYMBOL_WIFI, "");
            item_btns[i] = btn;

            if (btn) {
                lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
                // Safe default; callback only matters for AP/LINKED items.
                lv_obj_add_event_cb(btn, cb_net_selected, LV_EVENT_CLICKED, nullptr);
                lv_obj_set_user_data(btn, (void*)(intptr_t)-1);
            }
        }

        pool_initialized = true;
    };

    auto hide_all_items = [&]() {
        for (uint16_t i = 0; i < MAX_SCAN_UI_ITEMS; i++) {
            if (item_btns[i]) {
                lv_obj_add_flag(item_btns[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_user_data(item_btns[i], (void*)(intptr_t)-1);
            }
        }
    };

    auto set_item = [&](uint16_t slot, const void *icon_src, const char *txt, intptr_t user_data) {
        if (slot >= MAX_SCAN_UI_ITEMS) return;
        lv_obj_t *btn = item_btns[slot];
        if (!btn) return;

        // Child 0 = icon label, child 1 = text label for lv_list_add_btn
        lv_obj_t *icon = lv_obj_get_child(btn, 0);
        lv_obj_t *label = lv_obj_get_child(btn, 1);

        if (icon && icon_src) lv_label_set_text(icon, (const char *)icon_src);
        if (label) lv_label_set_text(label, txt ? txt : "");
        lv_obj_set_user_data(btn, (void*)user_data);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
    };

    ensure_pool();

    char count_buf[64];

    if (ctx->wifi_scan.mutex &&
        xSemaphoreTake(ctx->wifi_scan.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

        snprintf(count_buf, sizeof(count_buf), "APs: %d  STAs: %d",
                 ctx->wifi_scan.ap_count, ctx->wifi_scan.sta_count);
        lv_label_set_text(lbl_scan_count, count_buf);

        // View change: just hide the pool, do not destroy anything.
        if (last_view != ctx->wifi_scan.view) {
            hide_all_items();
            last_view = ctx->wifi_scan.view;
        }

        uint16_t slot = 0;

        if (ctx->wifi_scan.view == VIEW_AP) {
            for (int i = 0; i < ctx->wifi_scan.ap_count && slot < MAX_SCAN_UI_ITEMS; i++) {
                if (!ctx->wifi_scan.ap_list[i].active) continue;

                char txt[128];
                snprintf(txt, sizeof(txt), "[%s] %s (Ch:%d, %ddBm)",
                         enc_str(ctx->wifi_scan.ap_list[i].enc),
                         ctx->wifi_scan.ap_list[i].ssid,
                         ctx->wifi_scan.ap_list[i].channel,
                         ctx->wifi_scan.ap_list[i].rssi);

                set_item(slot, LV_SYMBOL_WIFI, txt, (intptr_t)i);
                slot++;
            }

            // Hide unused slots
            for (; slot < MAX_SCAN_UI_ITEMS; slot++) {
                if (item_btns[slot]) {
                    lv_obj_add_flag(item_btns[slot], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_user_data(item_btns[slot], (void*)(intptr_t)-1);
                }
            }
        }
        else if (ctx->wifi_scan.view == VIEW_STA) {
            for (int i = 0; i < ctx->wifi_scan.sta_count && slot < MAX_SCAN_UI_ITEMS; i++) {
                if (!ctx->wifi_scan.sta_list[i].active) continue;

                char mac[18];
                sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
                        ctx->wifi_scan.sta_list[i].mac[0], ctx->wifi_scan.sta_list[i].mac[1],
                        ctx->wifi_scan.sta_list[i].mac[2], ctx->wifi_scan.sta_list[i].mac[3],
                        ctx->wifi_scan.sta_list[i].mac[4], ctx->wifi_scan.sta_list[i].mac[5]);

                char txt[128];
                snprintf(txt, sizeof(txt), "%s (%ddBm)", mac, ctx->wifi_scan.sta_list[i].rssi);

                // No meaningful click action here, but keep callback harmless.
                set_item(slot, LV_SYMBOL_BLUETOOTH, txt, (intptr_t)i);
                slot++;
            }

            for (; slot < MAX_SCAN_UI_ITEMS; slot++) {
                if (item_btns[slot]) {
                    lv_obj_add_flag(item_btns[slot], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_user_data(item_btns[slot], (void*)(intptr_t)-1);
                }
            }
        }
        else if (ctx->wifi_scan.view == VIEW_LINKED) {
            bool found_any = false;

            for (int i = 0; i < ctx->wifi_scan.ap_count && slot < MAX_SCAN_UI_ITEMS; i++) {
                if (!ctx->wifi_scan.ap_list[i].active) continue;

                bool has_linked_stas = false;
                for (int j = 0; j < ctx->wifi_scan.sta_count; j++) {
                    if (ctx->wifi_scan.sta_list[j].active &&
                        ctx->wifi_scan.sta_list[j].hasAP &&
                        memcmp(ctx->wifi_scan.sta_list[j].apBssid,
                               ctx->wifi_scan.ap_list[i].bssid, 6) == 0) {
                        has_linked_stas = true;
                        break;
                    }
                }

                if (!has_linked_stas) continue;

                // AP header row
                {
                    char txt[128];
                    snprintf(txt, sizeof(txt), "AP: %s", ctx->wifi_scan.ap_list[i].ssid);
                    set_item(slot, LV_SYMBOL_WIFI, txt, (intptr_t)i);
                    slot++;
                    found_any = true;
                    if (slot >= MAX_SCAN_UI_ITEMS) break;
                }

                // Child STA rows
                for (int j = 0; j < ctx->wifi_scan.sta_count && slot < MAX_SCAN_UI_ITEMS; j++) {
                    if (!(ctx->wifi_scan.sta_list[j].active &&
                          ctx->wifi_scan.sta_list[j].hasAP &&
                          memcmp(ctx->wifi_scan.sta_list[j].apBssid,
                                 ctx->wifi_scan.ap_list[i].bssid, 6) == 0)) {
                        continue;
                    }

                    char mac[18];
                    mac_str(ctx->wifi_scan.sta_list[j].mac, mac);

                    char txt[128];
                    snprintf(txt, sizeof(txt), "  %s (%ddBm)", mac, ctx->wifi_scan.sta_list[j].rssi);

                    // Pass AP index for targeting, same as your old code.
                    set_item(slot, LV_SYMBOL_RIGHT, txt, (intptr_t)i);
                    slot++;
                    found_any = true;
                }
            }

            if (!found_any && slot < MAX_SCAN_UI_ITEMS) {
                set_item(slot, LV_SYMBOL_WARNING, "No associated clients found yet", (intptr_t)-1);
                slot++;
            }

            for (; slot < MAX_SCAN_UI_ITEMS; slot++) {
                if (item_btns[slot]) {
                    lv_obj_add_flag(item_btns[slot], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_user_data(item_btns[slot], (void*)(intptr_t)-1);
                }
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
    static uint8_t phase = 0;
    static bool ui_scan_dirty = false;
    uint32_t start = millis();
    
    AppContext *ctx = (AppContext *)timer->user_data;
    if (!ctx || ctx->wifi_scan.paused) { 
        if (millis() - start > 10) {
            Serial.printf("[DIAG] slow: scan_tick %lu ms\n", (unsigned long)(millis() - start));
        }
        return; 
    }

    switch (phase) {
        case 0: {
            int16_t n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) break;
            
            if (n >= 0) { // Scan finished successfully
                if (ctx->wifi_scan.mutex && xSemaphoreTake(ctx->wifi_scan.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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
                ui_scan_dirty = true;

                // STA cleanup: Mark old STAs as inactive
                if (ctx->wifi_scan.mutex && xSemaphoreTake(ctx->wifi_scan.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    for (int i = 0; i < ctx->wifi_scan.sta_count; i++) {
                        if (millis() - ctx->wifi_scan.sta_list[i].lastSeen > STA_TIMEOUT_MS) {
                            ctx->wifi_scan.sta_list[i].active = false;
                        }
                    }
                    xSemaphoreGive(ctx->wifi_scan.mutex);
                }
                
                if (!ctx->wifi_scan.paused) {
                    esp_wifi_set_promiscuous(true);
                }
            } else if (n == WIFI_SCAN_FAILED) {
                WiFi.scanDelete();
            }

            if (millis() - ctx->wifi_scan.last_scan_ms > AP_SCAN_INTERVAL_MS) {
                run_ap_scan(ctx);
                ctx->wifi_scan.last_scan_ms = millis();
            }
            break;
        }
        case 1:
            if (deferred_render) {
                ui_scan_dirty = true;
            }
            break;
        case 2:
            if (ui_scan_dirty) {
                lv_indev_t * indev = lv_indev_get_next(NULL);
                bool is_scrolling = scan_list ? lv_obj_is_scrolling(scan_list) : false;
                if ((!indev || indev->proc.state == LV_INDEV_STATE_REL) && !is_scrolling) {
                    render_scan_list(ctx);
                    ui_scan_dirty = false;
                } else {
                    deferred_render = true; // Try again next time
                }
            }
            break;
    }
    
    phase = (phase + 1) % 3;

    uint32_t dt = millis() - start;
    if (dt > 10) {
        Serial.printf("[DIAG] slow: scan_tick %lu ms\n", (unsigned long)dt);
    }
}
