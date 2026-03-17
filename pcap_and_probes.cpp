#include "pcap_and_probes.h"
#include "constants.h"
#include "sd_logger.h" // sd_logger.h now includes types.h
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include "ui_events.h"

// Global AppContext instance (defined in main .ino file)
extern AppContext g_app_context;

extern lv_obj_t *lbl_pcap_status, *probe_list, *btn_pcap_start, *btn_probe_start;
extern lv_obj_t *tabview;

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

    if (sniffer.pcap_active && sniffer.pcap_file && len <= MAX_PCAP_PACKET_SIZE) {
        // Apply backpressure to prevent queue memory exhaustion
        if (uxQueueMessagesWaitingFromISR(sniffer.pcap_queue) > (PCAP_QUEUE_SIZE - 4)) {
            // Drop packet safely
        } else {
            pcap_record_t rec;
            uint64_t us = esp_timer_get_time(); // Get microsecond timestamp
            rec.ts_sec = us / 1000000;
            rec.ts_usec = us % 1000000;
            rec.len = len;
            memcpy(rec.payload, frame, len);
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(sniffer.pcap_queue, &rec, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
        }
    }

    if (sniffer.probe_active && type == WIFI_PKT_MGMT && frame[0] == 0x40) {
        if (len > 26 && frame[24] == 0x00) {
            uint8_t sl = frame[25];
            if (sl > 0 && sl <= PROBE_MAX_SSID_LEN && (26 + sl <= len)) {
                // Create a ProbeSsid struct and send it
                ProbeSsid probe_data;
                memcpy(probe_data.data, &frame[26], sl);
                probe_data.data[sl] = '\0'; // Ensure null termination
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xQueueSendFromISR(sniffer.probe_queue, &probe_data, &xHigherPriorityTaskWoken);
                if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
            }
        }
    }
}

void process_pcap_queue(AppContext* context) {
    if (!context->sniffer.pcap_queue) return;
    pcap_record_t rec;
    int writes = 0;
    uint32_t start = millis();

    while (xQueueReceive(context->sniffer.pcap_queue, &rec, 0) == pdTRUE) { // Pass address
        if (!context->sniffer.pcap_active || !context->sniffer.pcap_file) {
            // Drain safely if we are no longer active
            continue;
        }
        
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 24576) {
            Serial.println("[PCAP] stopping capture: low internal heap");
            context->sniffer.pcap_active = false;
            sd_logger_pcap_file_close(context);
            continue; // Continue loop to fast-drain remaining items out of memory
        }

        if (!sd_logger_pcap_file_write(context, &rec)) {
            Serial.println("[PCAP] stopping due to write failure");
            context->sniffer.pcap_active = false;
            sd_logger_pcap_file_close(context);   // 🔴 CLOSE and strictly nullify
            continue; // Continue loop to fast-drain remaining items out of memory
        }
        context->sniffer.pcap_packet_count++;
        
        // Yield briefly every 4 writes so the watchdog doesn't starve if the SD card lags
        if ((++writes & 3) == 0) vTaskDelay(1); 
        if (writes >= 8) break; // Ultra-strict cap to guarantee no lv_timer_handler starvation
    }
    
    if (writes > 0) {
        Serial.printf("[DIAG] pcap drained=%d remain=%u dt=%lu\n",
                      writes,
                      context->sniffer.pcap_queue ? uxQueueMessagesWaiting(context->sniffer.pcap_queue) : 0,
                      (unsigned long)(millis() - start));
    }
}

void process_probe_queue(AppContext* context) {
    if (!context->sniffer.probe_active) return;
    ProbeSsid received_probe_ssid; // Receive into a struct
    int processed = 0;
    int ui_added = 0; // Cap UI churn
    uint32_t start = millis();
    
    while (xQueueReceive(context->sniffer.probe_queue, &received_probe_ssid, 0) == pdTRUE) { // Pass address
        processed++;
        if (strlen(received_probe_ssid.data) > 0 && context->sniffer.unique_probes.find(received_probe_ssid) == context->sniffer.unique_probes.end()) {
            if (context->sniffer.unique_probes.size() > MAX_LIST_MEMORY) {
                lv_indev_t * indev = lv_indev_get_next(NULL);
                bool is_scrolling = probe_list ? lv_obj_is_scrolling(probe_list) : false;
                if ((indev && indev->proc.state == LV_INDEV_STATE_PR) || is_scrolling) {
                    // Defer clearing the list to prevent LVGL crash while user is touching the screen
                    xQueueSendToFront(context->sniffer.probe_queue, &received_probe_ssid, 0);
                    break;
                }
                context->sniffer.unique_probes.clear();
                // ISOLATION TEST B: Comment out probe UI churn
                /*
                UiEvent* e = ui_queue.get_write_slot();
                if (e) {
                    e->type = UiEvent::CLEAR_PROBES;
                    ui_queue.commit_write();
                }
                */
                ui_added = 0;
            }
            context->sniffer.unique_probes.insert(received_probe_ssid);
            
            if (probe_list && tabview && lv_tabview_get_tab_act(tabview) == 5) {
                if (ui_added < 4) {
                    lv_indev_t * indev = lv_indev_get_next(NULL);
                    bool is_touched = (indev && indev->proc.state == LV_INDEV_STATE_PR) || lv_obj_is_scrolling(probe_list);
                    if (!is_touched) {
                        // ISOLATION TEST B: Comment out probe UI churn
                        /*
                        UiEvent* e = ui_queue.get_write_slot();
                        if (e) {
                            e->type = UiEvent::ADD_PROBE;
                            strncpy(e->text, received_probe_ssid.data, sizeof(e->text) - 1);
                            e->text[sizeof(e->text) - 1] = '\0';
                            ui_queue.commit_write();
                        }
                        */
                        ui_added++;
                    }
                }
            }

            if (sd_card_ready()) {
                sd_log_probe(received_probe_ssid.data);
            }
        }
        
        // Yield briefly every 4 writes so the watchdog doesn't starve
        if ((++processed & 3) == 0) vTaskDelay(1);
        if (processed >= 8) break; // Ultra-strict cap to guarantee no lv_timer_handler starvation
    }
    
    if (processed > 0) {
        Serial.printf("[DIAG] probe drained=%d added=%d remain=%u unique=%u dt=%lu\n",
                      processed, ui_added,
                      context->sniffer.probe_queue ? uxQueueMessagesWaiting(context->sniffer.probe_queue) : 0,
                      (unsigned)context->sniffer.unique_probes.size(),
                      (unsigned long)(millis() - start));
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
    
    static uint32_t last_ui = 0;
    // ISOLATION TEST C: Throttle to 500ms
    if (context->sniffer.pcap_active && millis() - last_ui >= 500) {
        UiEvent* e = ui_queue.get_write_slot();
        if (e) {
            e->type = UiEvent::SET_PCAP_STATUS;
            snprintf(e->text, sizeof(e->text), "#FFFF00 PCAP ACTIVE#\n\n%sCH: %d  Pkts: %lu",
                     context->sniffer.pcap_ch_locked ? "#FF8800 LOCKED# " : "",
                     context->sniffer.channel,
                     context->sniffer.pcap_packet_count);
            ui_queue.commit_write();
        }
        last_ui = millis();
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
        context->sniffer.pcap_file = File(); // Invalidate file object immediately
        
        // Flush queue
        pcap_record_t dump;
        while (xQueueReceive(context->sniffer.pcap_queue, &dump, 0) == pdTRUE);
        
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
        // DO NOT unregister the callback! Gated booleans are safer against Core 0 panics.
    }
    sd_logger_pcap_file_close(context); // Corrected function call
    context->sniffer.pcap_file = File(); // explicitly invalidate
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
        // DO NOT unregister the callback! Gated booleans are safer against Core 0 panics.
    }
    if (btn_probe_start) lv_label_set_text(lv_obj_get_child(btn_probe_start, 0), "START PROBE SNIFF");
}
