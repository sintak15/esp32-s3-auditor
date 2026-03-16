#include "pcap_and_probes.h"
#include "constants.h"
#include "sd_logger.h"
#include <WiFi.h>
#include <esp_wifi.h>

extern lv_obj_t *lbl_pcap_status, *probe_list, *btn_pcap_start, *btn_probe_start;
extern bool pcap_ch_locked;
extern uint8_t pcap_locked_ch;

static AppContext* sniffer_context = nullptr;

void pcap_and_probes_init(AppContext* context) {
    sniffer_context = context;
    context->sniffer.pcap_queue = xQueueCreate(PCAP_QUEUE_SIZE, sizeof(pcap_record_t));
    context->sniffer.probe_queue = xQueueCreate(PROBE_QUEUE_SIZE, PROBE_MAX_SSID_LEN + 1);
}

void IRAM_ATTR wifi_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!sniffer_context) return;
    SnifferState& sniffer = sniffer_context->sniffer;

    if (!sniffer.pcap_active && !sniffer.probe_active) return;
    
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t *frame = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if (sniffer.pcap_active && len <= MAX_PCAP_PACKET_SIZE) {
        pcap_record_t rec;
        rec.ts_sec = millis() / 1000;
        rec.ts_usec = (millis() % 1000) * 1000;
        rec.len = len;
        memcpy(rec.payload, frame, len);
        xQueueSendFromISR(sniffer.pcap_queue, &rec, NULL);
    }

    if (sniffer.probe_active && type == WIFI_PKT_MGMT && frame[0] == 0x40) {
        if (len > 26 && frame[24] == 0x00) {
            uint8_t sl = frame[25];
            if (sl > 0 && sl <= PROBE_MAX_SSID_LEN && (26 + sl <= len)) {
                char sb[PROBE_MAX_SSID_LEN + 1];
                memcpy(sb, &frame[26], sl);
                sb[sl] = '\0';
                xQueueSendFromISR(sniffer.probe_queue, sb, NULL);
            }
        }
    }
}

void process_pcap_queue(AppContext* context) {
    if (!context->sniffer.pcap_active || !context->sniffer.pcap_file) return;
    pcap_record_t rec;
    int writes = 0;
    while (xQueueReceive(context->sniffer.pcap_queue, &rec, 0)) {
        pcap_file_write(&rec);
        context->sniffer.pcap_packet_count++;
        if (++writes >= 50) break; // Don't block the main loop for too long
    }
}

void process_probe_queue(AppContext* context) {
    if (!context->sniffer.probe_active) return;
    char sb[PROBE_MAX_SSID_LEN + 1];
    while (xQueueReceive(context->sniffer.probe_queue, sb, 0)) {
        String s(sb);
        if (s.length() > 0 && context->sniffer.unique_probes.find(s) == context->sniffer.unique_probes.end()) {
            if (context->sniffer.unique_probes.size() > MAX_LIST_MEMORY) {
                context->sniffer.unique_probes.clear();
                lv_obj_clean(probe_list);
            }
            context->sniffer.unique_probes.insert(s);
            lv_list_add_text(probe_list, s.c_str());
            if (sd_card_ready()) {
                sd_log_probe(s.c_str());
            }
        }
    }
}

void process_channel_hop(AppContext* context) {
    if (!context->sniffer.pcap_active && !context->sniffer.probe_active) return;
    if (millis() - context->sniffer.last_hop_ms < CHANNEL_HOP_INTERVAL_MS) return;
    
    if (!pcap_ch_locked) { // This external global will be moved to context later
        context->sniffer.channel++;
        if (context->sniffer.channel > 13) context->sniffer.channel = 1;
    } else {
        context->sniffer.channel = pcap_locked_ch;
    }
    
    esp_wifi_set_channel(context->sniffer.channel, WIFI_SECOND_CHAN_NONE);
    context->sniffer.last_hop_ms = millis();
    
    if (context->sniffer.pcap_active) {
        lv_label_set_text_fmt(lbl_pcap_status, "#FFFF00 PCAP ACTIVE#\n\n%sCH: %d  Pkts: %lu",
                              pcap_ch_locked ? "#FF8800 LOCKED# " : "",
                              context->sniffer.channel, context->sniffer.pcap_packet_count);
    }
}

void stop_pcap(AppContext* context) {
    if (!context->sniffer.pcap_active) return;
    context->sniffer.pcap_active = false;
    esp_wifi_set_promiscuous(false);
    pcap_file_close();
    if (btn_pcap_start) lv_label_set_text(lv_obj_get_child(btn_pcap_start, 0), "START PCAP");
    lv_label_set_text(lbl_pcap_status, "#00FF88 Capture saved to SD#");
}

void stop_probe_sniffer(AppContext* context) {
    if (!context->sniffer.probe_active) return;
    context->sniffer.probe_active = false;
    esp_wifi_set_promiscuous(false);
    if (btn_probe_start) lv_label_set_text(lv_obj_get_child(btn_probe_start, 0), "START PROBE SNIFF");
}
