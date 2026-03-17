#include "sd_logger.h"
#include "constants.h"
#include <SD_MMC.h>
#include "wifi_scanner.h"

static bool sd_ready = false;

enum LogTarget {
    LOG_TARGET_SCAN,
    LOG_TARGET_PMKID_HASH,
    LOG_TARGET_PMKID_CSV,
    LOG_TARGET_BLE,
    LOG_TARGET_PROBE
};

#define MAX_LOG_LINE_LEN 128
struct LogMsg {
    LogTarget target;
    char line[MAX_LOG_LINE_LEN];
};
static QueueHandle_t sd_log_queue = nullptr;

const char* get_file_path(LogTarget target) {
    switch(target) {
        case LOG_TARGET_SCAN: return WIFI_SCAN_LOG;
        case LOG_TARGET_PMKID_HASH: return PMKID_HASH_LOG;
        case LOG_TARGET_PMKID_CSV: return PMKID_CSV_LOG;
        case LOG_TARGET_BLE: return BLE_SNIFF_LOG;
        case LOG_TARGET_PROBE: return PROBE_REQ_LOG;
        default: return "/unknown.log";
    }
}

static void sd_log_task(void* pvParameters) {
    LogMsg msg;
    while (true) {
        // Block until a message is available in the queue
        if (xQueueReceive(sd_log_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (sd_ready) {
                const char* path = get_file_path(msg.target);
                File f = SD_MMC.open(path, FILE_APPEND);
                if (f) {
                    if (f.size() == 0) {
                        if (msg.target == LOG_TARGET_SCAN) f.println("lat,lon,ssid,bssid,rssi,channel,enc");
                        else if (msg.target == LOG_TARGET_PMKID_CSV) f.println("ssid,bssid,client,pmkid");
                        else if (msg.target == LOG_TARGET_BLE) f.println("timestamp,mac,rssi");
                    }
                    f.print(msg.line);
                    
                    // Drain queued messages for the SAME target while the file is already open
                    while (uxQueueMessagesWaiting(sd_log_queue) > 0) {
                        LogMsg next_msg;
                        if (xQueuePeek(sd_log_queue, &next_msg, 0) == pdTRUE) {
                            if (next_msg.target == msg.target) {
                                xQueueReceive(sd_log_queue, &next_msg, 0); // Consume it
                                f.print(next_msg.line);
                            } else {
                                break; // Switch to a different file on the next loop iteration
                            }
                        }
                    }
                    f.close();
                }
            }
        }
    }
}

void sd_logger_init() {
    SD_MMC.setPins(38, 40, 39, 41, 48, 47);
    sd_ready = SD_MMC.begin("/sdcard", true);
    if (sd_ready) {
        File f = SD_MMC.open(BLE_SNIFF_LOG, FILE_APPEND);
        if (f) {
            if (f.size() == 0) f.println("timestamp,mac,rssi");
            f.close();
        }
        Serial.println("[SD] card mounted");
    } else {
        Serial.println("[SD] card mount failed");
    }

    // Initialize the background logging task and queue
    if (!sd_log_queue) {
        sd_log_queue = xQueueCreate(128, sizeof(LogMsg));
        if (sd_log_queue) {
            xTaskCreate(sd_log_task, "sd_logger_task", 4096, nullptr, 1, nullptr);
        }
    }
}

bool sd_card_ready() {
    return sd_ready;
}

void sd_reinit() {
    // This can be called periodically if the SD card was not initially present
    SD_MMC.end();
    sd_ready = SD_MMC.begin("/sdcard", true);
    if (sd_ready) {
        Serial.println("[SD] card re-mounted");
    }
}

void sd_log_scan(AppContext* context) {
    if (!sd_ready || !sd_log_queue) return;
    
    double lat = 0.0;
    double lon = 0.0;
    
    if (context && context->wifi_scan.mutex) {
        // Take the mutex just long enough to format the strings into the queue
        if (xSemaphoreTake(context->wifi_scan.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int i = 0; i < context->wifi_scan.ap_count; i++) {
                if (context->wifi_scan.ap_list[i].active) {
                    LogMsg msg;
                    msg.target = LOG_TARGET_SCAN;
                    char bss[18];
                    sprintf(bss, "%02X:%02X:%02X:%02X:%02X:%02X", 
                        context->wifi_scan.ap_list[i].bssid[0], context->wifi_scan.ap_list[i].bssid[1],
                        context->wifi_scan.ap_list[i].bssid[2], context->wifi_scan.ap_list[i].bssid[3],
                        context->wifi_scan.ap_list[i].bssid[4], context->wifi_scan.ap_list[i].bssid[5]);
                    
                    snprintf(msg.line, sizeof(msg.line), "%.6f,%.6f,\"%s\",%s,%d,%d,%s\n",
                             lat, lon, context->wifi_scan.ap_list[i].ssid, bss,
                             context->wifi_scan.ap_list[i].rssi, context->wifi_scan.ap_list[i].channel,
                             enc_str(context->wifi_scan.ap_list[i].enc));

                    // Send to background task without blocking the UI
                    xQueueSend(sd_log_queue, &msg, 0); 
                }
            }
            xSemaphoreGive(context->wifi_scan.mutex);
        }
    }
}

void sd_log_pmkid(const uint8_t *pmkid, const uint8_t *ap_mac, const uint8_t *cl_mac, const char *ssid) {
    if (!sd_ready || !sd_log_queue) return;
    auto hex = [](const uint8_t *b, int n, char *o) {
        for (int i = 0; i < n; i++) sprintf(o + i * 2, "%02x", b[i]);
        o[n * 2] = 0;
    };
    char ph[33], ah[13], ch[13], sh[65];
    hex(pmkid, 16, ph);
    hex(ap_mac, 6, ah);
    hex(cl_mac, 6, ch);

    LogMsg msg1;
    msg1.target = LOG_TARGET_PMKID_HASH;
    snprintf(msg1.line, sizeof(msg1.line), "PMKID*%s*%s*%s\n", ph, ah, ssid); // Hashcat expects raw SSID
    xQueueSend(sd_log_queue, &msg1, 0);

    LogMsg msg2;
    msg2.target = LOG_TARGET_PMKID_CSV;
    snprintf(msg2.line, sizeof(msg2.line), "\"%s\",%s,%s,%s\n", ssid, ah, ch, ph);
    xQueueSend(sd_log_queue, &msg2, 0);
}

void sd_log_ble_sniff(unsigned long timestamp, const char* mac, int8_t rssi) {
    if (!sd_ready || !sd_log_queue) return;
    LogMsg msg;
    msg.target = LOG_TARGET_BLE;
    snprintf(msg.line, sizeof(msg.line), "%lu,%s,%d\n", timestamp, mac, rssi);
    xQueueSend(sd_log_queue, &msg, 0);
}

void sd_log_probe(const char* ssid) {
    if (!sd_ready || !sd_log_queue) return;
    LogMsg msg;
    msg.target = LOG_TARGET_PROBE;
    snprintf(msg.line, sizeof(msg.line), "%s\n", ssid);
    xQueueSend(sd_log_queue, &msg, 0);
}

// --- PCAP File Management ---

bool sd_logger_pcap_file_open(AppContext* context) { // Renamed function
    if (!sd_ready) return false;
    char fn[32];
    sprintf(fn, "/cap_%lu.pcap", millis());
    context->sniffer.pcap_file = SD_MMC.open(fn, FILE_WRITE);
    if (context->sniffer.pcap_file) {
        pcap_global_header gh = {0xa1b2c3d4, 2, 4, 0, 0, 65535, 105};
        context->sniffer.pcap_file.write((uint8_t*)&gh, sizeof(gh));
        context->sniffer.pcap_file.flush();
        return true;
    }
    return false;
}

void sd_logger_pcap_file_write(AppContext* context, pcap_record_t* record) { // Renamed function
    if (context->sniffer.pcap_file) {
        pcap_packet_header h;
        h.ts_sec = record->ts_sec;
        h.ts_usec = record->ts_usec;
        h.incl_len = record->len;
        h.orig_len = record->len;
        context->sniffer.pcap_file.write((uint8_t*)&h, sizeof(h));
        context->sniffer.pcap_file.write(record->payload, record->len);
    }
}

void sd_logger_pcap_file_close(AppContext* context) { // Renamed function
    if (context->sniffer.pcap_file) {
        context->sniffer.pcap_file.flush();
        context->sniffer.pcap_file.close();
    }
}
