#pragma once

#include <stdint.h>
#include "constants.h"

// ──────────────────────────────────────────────
// Data Structures & Enums
// ──────────────────────────────────────────────
struct APRecord {
  char    ssid[33];
  uint8_t bssid[6];
  int8_t  rssi;
  uint8_t channel;
  uint8_t enc;
  int     staCount;
  bool    active;
};

struct StaRecord {
  uint8_t  mac[6];
  uint8_t  apBssid[6];
  bool     hasAP;
  int8_t   rssi;
  uint32_t lastSeen;
  bool     active;
};

struct TouchPoint { uint16_t x, y; bool pressed; };

struct pcap_record_t {
  uint32_t ts_sec, ts_usec, len;
  uint8_t  payload[MAX_PCAP_PACKET_SIZE];
};

struct pcap_global_header {
  uint32_t magic_number;
  uint16_t version_major, version_minor;
  int32_t  thiszone;
  uint32_t sigfigs, snaplen, network;
};

struct pcap_packet_header {
  uint32_t ts_sec, ts_usec, incl_len, orig_len;
};

enum ScanView { VIEW_AP, VIEW_STA, VIEW_LINKED };
enum PentestMode { PT_NONE, PT_DEAUTH, PT_BEACON, PT_PMKID };

struct GpsSnapshot {
  bool locValid, timeValid, dateValid, altValid;
  bool speedValid, courseValid, hdopValid, satsValid;
  double lat, lon, altMeters, speedKmph, courseDeg, hdop;
  uint32_t sats, charsProcessed;
  uint8_t hour, minute, second, day, month;
  uint16_t year;
};

struct StatusSnapshot {
  bool sdMounted;
  int  batteryPct;
};

struct BLERing {
  char mac[18]; int8_t rssi; bool fresh;
};