#pragma once

#include <Arduino.h>

struct CompanionStatus {
  bool present;
  bool monitor_active;
  bool target_set;
  bool pmkid_found;
  uint8_t channel;
  uint8_t target_bssid[6];
  uint8_t sta_mac[6];
  uint8_t pmkid[16];
  int8_t last_rssi;
  uint8_t proto_version;
};

// Initialize/probe the companion link (I2C slave at 0x42).
// Safe to call repeatedly; it caches presence with a periodic probe.
bool companion_probe();

// Returns the last known presence state (may be stale until companion_probe()).
bool companion_present();

// Returns the number of bytes requested for each status read.
size_t companion_status_len();

// Returns the number of bytes read by the most recent status read attempt.
size_t companion_last_read_len();

// Bus diagnostics: performs a zero-length I2C transaction and returns the
// underlying Wire endTransmission() result code (0 = ACK).
// Returns false if the I2C bus mutex couldn't be acquired.
bool companion_ping(uint8_t* out_rc);
bool companion_touch_ping(uint8_t* out_rc);

// Read the latest status frame from the companion.
// Returns false if the device is not responding or the frame is invalid.
bool companion_read_status(CompanionStatus* out);

// Commands
bool companion_set_target(uint8_t channel, const uint8_t bssid[6]);
bool companion_start_pmkid();
bool companion_stop_all();
bool companion_clear_result();
