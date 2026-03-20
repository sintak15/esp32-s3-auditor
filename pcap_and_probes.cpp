#include "pcap_and_probes.h"
#include "constants.h"
#include "sd_logger.h" // sd_logger.h now includes types.h
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "ui_events.h"
#include "wifi_scanner.h" // For add_or_update_sta

// Global AppContext instance (defined in main .ino file)
extern AppContext g_app_context;

extern lv_obj_t *lbl_pcap_status, *probe_list, *btn_pcap_start, *btn_probe_start;
extern lv_obj_t *tabview;

void pcap_and_probes_init(AppContext* context) {
    // State lives in AppContext; no separate context pointer needed.
    context->capture.pcap_queue = xQueueCreate(PCAP_QUEUE_SIZE, sizeof(pcap_record_t));
    context->capture.probe_queue = xQueueCreate(PROBE_QUEUE_SIZE, sizeof(ProbeSsid)); // Corrected size
}

void IRAM_ATTR wifi_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t *frame = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;
    int8_t rssi = pkt->rx_ctrl.rssi;

    // 1. Client (STA) Discovery - Resolve "sta and linked never populate"
    if (type == WIFI_PKT_DATA && len >= 24) {
        uint8_t *mac_addr = nullptr;
        uint8_t *ap_bssid = nullptr;
        bool has_ap = false;

        uint8_t fc = frame[1]; // Frame Control Byte 2
        bool to_ds = fc & 0x01;
        bool from_ds = fc & 0x02;

        if (to_ds && !from_ds) { // To AP: Addr1=BSSID, Addr2=SA
            mac_addr = frame + 10; ap_bssid = frame + 4; has_ap = true;
        } else if (!to_ds && from_ds) { // From AP: Addr1=DA, Addr2=BSSID
            mac_addr = frame + 4; ap_bssid = frame + 10; has_ap = true;
        } else if (!to_ds && !from_ds) { // Ad-hoc/Direct: Addr2=SA
            mac_addr = frame + 10;
        }

        if (mac_addr) {
            add_or_update_sta(&g_app_context, mac_addr, ap_bssid, rssi, has_ap);
        }
    }

    // 2. Capture Tasks (PCAP + probe requests)
    CaptureState& capture = g_app_context.capture;
    if (!capture.pcap_active && !capture.probe_active) return;

    if (capture.pcap_active && capture.pcap_file && len <= MAX_PCAP_PACKET_SIZE) {
        // Ensure pcap_queue is valid before using it
        if (!capture.pcap_queue) return;

        // Apply backpressure to prevent queue memory exhaustion
        if (uxQueueMessagesWaitingFromISR(capture.pcap_queue) > (PCAP_QUEUE_SIZE - 4)) {
            // Drop packet safely
        } else {
            pcap_record_t rec;
            uint64_t us = esp_timer_get_time(); // Get microsecond timestamp
            rec.ts_sec = us / 1000000;
            rec.ts_usec = us % 1000000;
            rec.len = len;
            memcpy(rec.payload, frame, len);
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(capture.pcap_queue, &rec, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
        }
    }

    if (capture.probe_active && type == WIFI_PKT_MGMT && frame[0] == 0x40) {
        if (len > 26 && frame[24] == 0x00) {
            uint8_t sl = frame[25];
            if (sl > 0 && sl <= PROBE_MAX_SSID_LEN && (26 + sl <= len)) {
                // Create a ProbeSsid struct and send it
                ProbeSsid probe_data;
                memcpy(probe_data.data, &frame[26], sl);
                probe_data.data[sl] = '\0'; // Ensure null termination
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xQueueSendFromISR(capture.probe_queue, &probe_data, &xHigherPriorityTaskWoken);
                if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
            }
        }
    }
}

void process_pcap_queue(AppContext* context) {
    if (!context->capture.pcap_queue) return;
    pcap_record_t rec;
    int writes = 0;
    uint32_t t0 = millis();
    const uint32_t budgetMs = 6;

    while (xQueueReceive(context->capture.pcap_queue, &rec, 0) == pdTRUE) { // Pass address
        if (!context->capture.pcap_active || !context->capture.pcap_file) {
            // Drain safely if we are no longer active
            continue;
        }
        
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 24576) {
            Serial.println("[PCAP] stopping capture: low internal heap");
            context->capture.pcap_active = false;
            sd_logger_pcap_file_close(context);
            continue; // Continue loop to fast-drain remaining items out of memory
        }

        if (!sd_logger_pcap_file_write(context, &rec)) {
            Serial.println("[PCAP] stopping due to write failure");
            context->capture.pcap_active = false;
            sd_logger_pcap_file_close(context);   // 🔴 CLOSE and strictly nullify
            continue; // Continue loop to fast-drain remaining items out of memory
        }
        context->capture.pcap_packet_count++;
        writes++;

        if ((millis() - t0) >= budgetMs || writes >= 8) {
            break;
        }
    }
}

void process_probe_queue(AppContext* context) {
    if (!g_app_context.capture.probe_active) return;
    ProbeSsid received_probe_ssid; // Receive into a struct
    int processed = 0;
    uint32_t t0 = millis();
    const uint32_t budgetMs = 6;
    
    while (xQueueReceive(g_app_context.capture.probe_queue, &received_probe_ssid, 0) == pdTRUE) { // Pass address
        if (strlen(received_probe_ssid.data) > 0 && g_app_context.capture.unique_probes.find(received_probe_ssid) == g_app_context.capture.unique_probes.end()) {
            if (g_app_context.capture.unique_probes.size() > MAX_LIST_MEMORY) {
                g_app_context.capture.unique_probes.clear();
                queue_local_ui_text(UI_EVT_CLEAR_PROBE_LIST, nullptr);
            }
            
            g_app_context.capture.unique_probes.insert(received_probe_ssid);
            queue_local_ui_text(UI_EVT_ADD_PROBE_TEXT, received_probe_ssid.data);

            if (sd_card_ready()) {
                sd_log_probe(received_probe_ssid.data);
            }
        }
        
        processed++;
        if ((millis() - t0) >= budgetMs || processed >= 8) {
            break;
        }
    }
}

void process_channel_hop(AppContext* context) {
    // 1) PCAP / probe monitoring (existing behavior)
    if (g_app_context.capture.pcap_active || g_app_context.capture.probe_active) {
        if (millis() - g_app_context.capture.last_hop_ms < CHANNEL_HOP_INTERVAL_MS) return;

        if (!g_app_context.capture.pcap_ch_locked) {
            g_app_context.capture.channel++;
            if (g_app_context.capture.channel > 13) g_app_context.capture.channel = 1;
        } else {
            g_app_context.capture.channel = g_app_context.capture.pcap_locked_ch;
        }
        
        esp_wifi_set_channel(g_app_context.capture.channel, WIFI_SECOND_CHAN_NONE);
        g_app_context.capture.last_hop_ms = millis();
        
        static uint32_t last_ui = 0;
        if (g_app_context.capture.pcap_active && millis() - last_ui >= 500) {
            char buf[96];
            snprintf(buf, sizeof(buf), "#FFFF00 PCAP ACTIVE#\n\n%sCH: %d  Pkts: %lu",
                     g_app_context.capture.pcap_ch_locked ? "#FF8800 LOCKED# " : "",
                     g_app_context.capture.channel,
                     g_app_context.capture.pcap_packet_count);
            queue_local_ui_text(UI_EVT_SET_PCAP_STATUS, buf);
            last_ui = millis();
        }
        return;
    }

    // 2) WiFi scan STA discovery: hop channels while between active scans so the STA/LINKED views populate.
    if (!(g_app_context.wifi_scan.started && !g_app_context.wifi_scan.paused)) return;
    if (g_app_context.audit.current_mode != AUDIT_NONE) return;

    // Do not fight active AP scans (promiscuous must be off during scanNetworks).
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;

    static uint32_t last_scan_hop_ms = 0;
    static uint8_t scan_ch = 1;
    static constexpr uint32_t SCAN_HOP_INTERVAL_MS = 250;

    if (millis() - last_scan_hop_ms < SCAN_HOP_INTERVAL_MS) return;

    scan_ch++;
    if (scan_ch > 13) scan_ch = 1;
    esp_wifi_set_channel(scan_ch, WIFI_SECOND_CHAN_NONE);
    last_scan_hop_ms = millis();
}

void start_pcap(AppContext* context) {
    if (g_app_context.capture.pcap_active) return; // Already active
    g_app_context.capture.pcap_active = true;
    
    if (!g_app_context.capture.probe_active) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_cb);
    }
    
    if (!sd_card_ready()) {
        Serial.println("[PCAP] SD card not ready, cannot open file.");
        g_app_context.capture.pcap_active = false;
        return;
    }
    if (!sd_logger_pcap_file_open(&g_app_context)) { // Use sd_logger's function
        Serial.println("[PCAP] Failed to open PCAP file.");
        g_app_context.capture.pcap_active = false;
        g_app_context.capture.pcap_file = File(); // Invalidate file object immediately
        
        // Flush queue
        pcap_record_t dump; // Use g_app_context.capture.pcap_queue
        while (xQueueReceive(g_app_context.capture.pcap_queue, &dump, 0) == pdTRUE);
        
        return;
    }
    g_app_context.capture.pcap_packet_count = 0;
    g_app_context.capture.channel = 1;
    g_app_context.capture.last_hop_ms = 0;
    // UI button text update handled in cb_toggle_pcap in .ino
}

void stop_pcap(AppContext* context) {
    if (!g_app_context.capture.pcap_active) return;
    g_app_context.capture.pcap_active = false;
    if (!g_app_context.capture.probe_active) {
        esp_wifi_set_promiscuous(false);
        // DO NOT unregister the callback! Gated booleans are safer against Core 0 panics.
    }
    sd_logger_pcap_file_close(&g_app_context); // Corrected function call
    g_app_context.capture.pcap_file = File(); // explicitly invalidate
    queue_local_ui_text(UI_EVT_SET_PCAP_BUTTON, "START PCAP");
    queue_local_ui_text(UI_EVT_SET_PCAP_STATUS, "#00FF88 Capture saved to SD#");
}

void start_probe_monitor(AppContext* context) {
    if (g_app_context.capture.probe_active) return; // Already active
    g_app_context.capture.probe_active = true;
    if (!g_app_context.capture.pcap_active) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_cb);
    }
    g_app_context.capture.unique_probes.clear();
    queue_local_ui_text(UI_EVT_CLEAR_PROBE_LIST, nullptr);
    // UI button text update handled in cb_toggle_probes in .ino
}

void stop_probe_monitor(AppContext* context) {
    if (!g_app_context.capture.probe_active) return;
    g_app_context.capture.probe_active = false;
    if (!g_app_context.capture.pcap_active) {
        esp_wifi_set_promiscuous(false);
        // DO NOT unregister the callback! Gated booleans are safer against Core 0 panics.
    }
    queue_local_ui_text(UI_EVT_SET_PROBE_BUTTON, "START PROBE MONITOR");
}
