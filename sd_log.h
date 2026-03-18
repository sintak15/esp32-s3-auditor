#pragma once

#include "types.h" // Changed from state.h to types.h

void sd_log_init();
bool sd_card_ready();
void sd_reinit();
void sd_log_scanned_aps(AppContext* context);
void sd_log_captured_pmkid(const uint8_t *pmkid, const uint8_t *ap_mac, const uint8_t *cl_mac, const char *ssid);
void sd_log_ble_advertisement(unsigned long timestamp, const char* mac, int8_t rssi);
void sd_log_probe_request(const char* ssid);

// Functions to manage PCAP file
bool pcap_file_open(AppContext* context);
bool pcap_file_write_record(AppContext* context, pcap_record_t* record);
void pcap_file_close(AppContext* context);
