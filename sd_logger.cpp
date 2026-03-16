#include "sd_logger.h"
#include "constants.h"
#include <SD_MMC.h>

static bool sd_ready = false;

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

static GpsSnapshot read_gps_snapshot_sd(AppContext* context) {
  GpsSnapshot s = {};
  if (context && context->gps.mutex && xSemaphoreTake(context->gps.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    s = context->gps.snap;
    xSemaphoreGive(context->gps.mutex);
  }
  return s;
}

void sd_log_scan(AppContext* context) {
    if (!sd_ready) return;
    File f = SD_MMC.open(WIFI_SCAN_LOG, FILE_APPEND);
    if (!f) return;
    if (f.size() == 0) f.println("lat,lon,ssid,bssid,rssi,channel,enc");
    
    GpsSnapshot gs = read_gps_snapshot_sd(context);
    double lat = gs.locValid ? gs.lat : 0.0;
    double lon = gs.locValid ? gs.lon : 0.0;
    
    for (int i = 0; i < context->wifi_scan.ap_count; i++) {
        if (context->wifi_scan.ap_list[i].active) {
            char bss[18];
            sprintf(bss, "%02X:%02X:%02X:%02X:%02X:%02X", 
                context->wifi_scan.ap_list[i].bssid[0], context->wifi_scan.ap_list[i].bssid[1],
                context->wifi_scan.ap_list[i].bssid[2], context->wifi_scan.ap_list[i].bssid[3],
                context->wifi_scan.ap_list[i].bssid[4], context->wifi_scan.ap_list[i].bssid[5]);
            
            f.printf("%.6f,%.6f,\"%s\",%s,%d,%d,%s\n",
                     lat, lon, context->wifi_scan.ap_list[i].ssid, bss,
                     context->wifi_scan.ap_list[i].rssi, context->wifi_scan.ap_list[i].channel,
                     "ENC_TYPE" /* TODO: Pass enc_str logic here */);
        }
    }
    f.close();
}

void sd_log_pmkid(const uint8_t *pmkid, const uint8_t *ap_mac, const uint8_t *cl_mac, const char *ssid) {
    if (!sd_ready) return;
    auto hex = [](const uint8_t *b, int n, char *o) {
        for (int i = 0; i < n; i++) sprintf(o + i * 2, "%02x", b[i]);
        o[n * 2] = 0;
    };
    char ph[33], ah[13], ch[13], sh[65];
    hex(pmkid, 16, ph);
    hex(ap_mac, 6, ah);
    hex(cl_mac, 6, ch);

    File f1 = SD_MMC.open(PMKID_HASH_LOG, FILE_APPEND);
    if (f1) {
        f1.printf("PMKID*%s*%s*%s\n", ph, ah, ssid); // Hashcat expects raw SSID
        f1.close();
    }
    File f2 = SD_MMC.open(PMKID_CSV_LOG, FILE_APPEND);
    if (f2) {
        if (f2.size() == 0) f2.println("ssid,bssid,client,pmkid");
        f2.printf("\"%s\",%s,%s,%s\n", ssid, ah, ch, ph);
        f2.close();
    }
}

void sd_log_ble_sniff(unsigned long timestamp, const char* mac, int8_t rssi) {
    if (!sd_ready) return;
    File f = SD_MMC.open(BLE_SNIFF_LOG, FILE_APPEND);
    if (f) {
        f.printf("%lu,%s,%d\n", timestamp, mac, rssi);
        f.close();
    }
}

void sd_log_probe(const char* ssid) {
    if (!sd_ready) return;
    File f = SD_MMC.open(PROBE_REQ_LOG, FILE_APPEND);
    if (f) {
        f.println(ssid);
        f.close();
    }
}

// --- PCAP File Management ---

static File sd_logger_pcap_file_handle; // Renamed to avoid collision

bool sd_logger_pcap_file_open(AppContext* context) { // Renamed function
    if (!sd_ready) return false;
    char fn[32];
    sprintf(fn, "/cap_%lu.pcap", millis());
    sd_logger_pcap_file_handle = SD_MMC.open(fn, FILE_WRITE); // Use renamed handle
    if (sd_logger_pcap_file_handle) {
        pcap_global_header gh = {0xa1b2c3d4, 2, 4, 0, 0, 65535, 105};
        sd_logger_pcap_file_handle.write((uint8_t*)&gh, sizeof(gh));
        sd_logger_pcap_file_handle.flush();
        context->sniffer.pcap_file = sd_logger_pcap_file_handle;
        return true;
    }
    return false;
}

void sd_logger_pcap_file_write(pcap_record_t* record) { // Renamed function
    if (sd_logger_pcap_file_handle) {
        pcap_packet_header h;
        h.ts_sec = record->ts_sec;
        h.ts_usec = record->ts_usec;
        h.incl_len = record->len;
        h.orig_len = record->len;
        sd_logger_pcap_file_handle.write((uint8_t*)&h, sizeof(h));
        sd_logger_pcap_file_handle.write(record->payload, record->len);
    }
}

void sd_logger_pcap_file_close() { // Renamed function
    if (sd_logger_pcap_file_handle) {
        sd_logger_pcap_file_handle.flush();
        sd_logger_pcap_file_handle.close();
    }
}
