#pragma once

#include "state.h"
#include <esp_wifi_types.h>

void pcap_and_probes_init(AppContext* context);

void start_pcap(AppContext* context);
void stop_pcap(AppContext* context);
void process_pcap_queue(AppContext* context);

void start_probe_sniffer(AppContext* context);
void stop_probe_sniffer(AppContext* context);
void process_probe_queue(AppContext* context);

void process_channel_hop(AppContext* context);

// Main promiscuous callback for PCAP and Probes
void IRAM_ATTR wifi_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type);
