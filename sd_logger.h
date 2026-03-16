#pragma once

#include "types.h" // Changed from state.h to types.h

void sd_logger_init();
bool sd_card_ready();
void sd_reinit();
void sd_log_scan(AppContext* context);
void sd_log_pmkid(const uint8_t *pmkid, const uint8_t *ap_mac, const uint8_t *cl_mac, const char *ssid);
void sd_log_ble_sniff(unsigned long timestamp, const char* mac, int8_t rssi);
void sd_log_probe(const char* ssid);

// Functions to manage PCAP file
bool sd_logger_pcap_file_open(AppContext* context); // Renamed
void sd_logger_pcap_file_write(pcap_record_t* record); // Renamed
void sd_logger_pcap_file_close(); // Renamed
