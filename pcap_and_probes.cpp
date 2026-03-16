#include "pcap_and_probes.h"
#include "constants.h"
#include "sd_logger.h" // sd_logger.h now includes types.h
#include <WiFi.h>
#include <esp_wifi.h>

// Global AppContext instance (defined in main .ino file)
extern AppContext g_app_context;

extern lv_obj_t *lbl_pcap_status, *probe_list, *btn_pcap_start, *btn_probe_start;

static AppContext* sniffer_context = nullptr; // Declaration for sniffer_context

void pcap_and_probes_init(AppContext* context) {
    sniffer_context = context;
    context->sniffer.pcap_queue = xQueueCreate(PCAP_QUEUE_SIZE, sizeof(pcap_record_t));
    context->sniffer.probe_queue = xQueueCreate(PROBE_QUEUE_SIZE, sizeof(ProbeSsid)); // Corrected size
}

void IRAM_ATTR wifi_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!sniffer_context || !sniffer_context->sniffer.pcap_queue) return;
    SnifferState& sniffer = sniffer_context->sniffer;

    if (!sniffer.pcap_active && !sniffer.probe_active) return;
    
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t *frame = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if (sniffer.pcap_active && len <= MAX_PCAP_PACKET_SIZE) {
        pcap_record_t rec;
        uint64_t us = esp_timer_get_time(); // Get microsecond timestamp
        rec.ts_sec = us / 1000000;
        rec.ts_usec = us % 1000000;
        rec.len = len;
        memcpy(rec.payload, frame, len);
        xQueueSendFromISR(sniffer.pcap_queue, &rec, NULL);
    }

    if (sniffer.probe_active && type == WIFI_PKT_MGMT && frame[0] == 0x40) {
        if (len > 26 && frame[24] == 0x00) {
            uint8_t sl = frame[25];
            if (sl > 0 && sl <= PROBE_MAX_SSID_LEN && (26 + sl <= len)) {
                // Create a ProbeSsid struct and send it
                ProbeSsid probe_data;
                memcpy(probe_data.data, &frame[26], sl);
                probe_data.data[PROBE_MAX_SSID_LEN] = '\0'; // Ensure null termination
                xQueueSendFromISR(sniffer.probe_queue, &probe_data, NULL);
            }
        }
    }
}

void process_pcap_queue(AppContext* context) {
    if (!context->sniffer.pcap_active || !context->sniffer.pcap_file) return;
    pcap_record_t rec;
    int writes = 0;
    while (xQueueReceive(context->sniffer.pcap_queue, &rec, 0) == pdTRUE) { // Pass address
        sd_logger_pcap_file_write(context, &rec); // Corrected function call
        context->sniffer.pcap_packet_count++;
        if (++writes >= 50) break; // Don't block the main loop for too long
    }
}

void process_probe_queue(AppContext* context) {
    if (!context->sniffer.probe_active) return;
    ProbeSsid received_probe_ssid; // Receive into a struct
    while (xQueueReceive(context->sniffer.probe_queue, &received_probe_ssid, 0) == pdTRUE) { // Pass address
        if (strlen(received_probe_ssid.data) > 0 && context->sniffer.unique_probes.find(received_probe_ssid) == context->sniffer.unique_probes.end()) {
            if (context->sniffer.unique_probes.size() > MAX_LIST_MEMORY) {
                context->sniffer.unique_probes.clear();
                if (probe_list) lv_obj_clean(probe_list); // Guard against probe_list not being initialized
            }
            context->sniffer.unique_probes.insert(received_probe_ssid);
            lv_list_add_text(probe_list, received_probe_ssid.data);
            if (sd_card_ready()) {
                sd_log_probe(received_probe_ssid.data);
            }
        }
    }
}

void process_channel_hop(AppContext* context) {
    if (!context->sniffer.pcap_active && !context->sniffer.probe_active) return;
    if (millis() - context->sniffer.last_hop_ms < CHANNEL_HOP_INTERVAL_MS) return;

    if (!context->sniffer.pcap_ch_locked) { // Use context->sniffer.pcap_ch_locked
        context->sniffer.channel++;
        if (context->sniffer.channel > 13) context->sniffer.channel = 1;
    } else {
        context->sniffer.channel = context->sniffer.pcap_locked_ch;
    }
    
    esp_wifi_set_channel(context->sniffer.channel, WIFI_SECOND_CHAN_NONE);
    context->sniffer.last_hop_ms = millis();
    
    if (context->sniffer.pcap_active) {
        lv_label_set_text_fmt(lbl_pcap_status, "#FFFF00 PCAP ACTIVE#\n\n%sCH: %d  Pkts: %lu", // Use context->sniffer.pcap_ch_locked
                              context->sniffer.pcap_ch_locked ? "#FF8800 LOCKED# " : "",
                              context->sniffer.channel, context->sniffer.pcap_packet_count);
    }
}

void start_pcap(AppContext* context) {
    if (context->sniffer.pcap_active) return; // Already active
    context->sniffer.pcap_active = true;
    
    if (!context->sniffer.probe_active) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_cb);
    }
    
    if (!sd_card_ready()) {
        Serial.println("[PCAP] SD card not ready, cannot open file.");
        context->sniffer.pcap_active = false;
        return;
    }
    if (!sd_logger_pcap_file_open(context)) { // Use sd_logger's function
        Serial.println("[PCAP] Failed to open PCAP file.");
        context->sniffer.pcap_active = false;
        return;
    }
    context->sniffer.pcap_packet_count = 0;
    context->sniffer.channel = 1;
    context->sniffer.last_hop_ms = 0;
    // UI button text update handled in cb_toggle_pcap in .ino
}

void stop_pcap(AppContext* context) {
    if (!context->sniffer.pcap_active) return;
    context->sniffer.pcap_active = false;
    if (!context->sniffer.probe_active) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);
    }
    sd_logger_pcap_file_close(context); // Corrected function call
    if (btn_pcap_start) lv_label_set_text(lv_obj_get_child(btn_pcap_start, 0), "START PCAP");
    lv_label_set_text(lbl_pcap_status, "#00FF88 Capture saved to SD#");
}

void start_probe_sniffer(AppContext* context) {
    if (context->sniffer.probe_active) return; // Already active
    context->sniffer.probe_active = true;
    if (!context->sniffer.pcap_active) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_cb);
    }
    context->sniffer.unique_probes.clear();
    if (probe_list) lv_obj_clean(probe_list);
    // UI button text update handled in cb_toggle_probes in .ino
}

void stop_probe_sniffer(AppContext* context) {
    if (!context->sniffer.probe_active) return;
    context->sniffer.probe_active = false;
    if (!context->sniffer.pcap_active) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);
    }
    if (btn_probe_start) lv_label_set_text(lv_obj_get_child(btn_probe_start, 0), "START PROBE SNIFF");
}
