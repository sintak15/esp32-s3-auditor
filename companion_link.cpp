#include "companion_link.h"

#include <Wire.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t g_i2cMutex;

static constexpr uint8_t  COMPANION_ADDR = 0x42;
static constexpr uint8_t  PROTO_VERSION  = 1;
static constexpr uint32_t PROBE_INTERVAL_MS = 2000;
// Only the first ~34 bytes are used by the main firmware today. Requesting fewer bytes
// improves reliability on shared I2C buses (touch + companion) and reduces timeouts
// under load.
static constexpr size_t   STATUS_LEN = 34;
static constexpr uint32_t PRESENT_GRACE_MS = 6000;

enum CompanionCmd : uint8_t {
  CMD_GET_STATUS   = 0x01,
  CMD_SET_TARGET   = 0x02,
  CMD_START_PMKID  = 0x03,
  CMD_STOP_ALL     = 0x04,
  CMD_CLEAR_RESULT = 0x05,
  CMD_SET_CHANNEL  = 0x06
};

static bool g_present = false;
static uint32_t g_last_probe_ms = 0;
static uint32_t g_last_ok_ms = 0;

static bool i2c_lock(uint32_t timeout_ms) {
  if (!g_i2cMutex) return true;
  return xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void i2c_unlock() {
  if (!g_i2cMutex) return;
  xSemaphoreGive(g_i2cMutex);
}

static bool read_status_frame(uint8_t out[STATUS_LEN]) {
  if (!i2c_lock(60)) return false;

  bool ok = false;
  for (int attempt = 0; attempt < 2 && !ok; attempt++) {
    memset(out, 0, STATUS_LEN);
    Wire.requestFrom((int)COMPANION_ADDR, (int)STATUS_LEN);

    size_t i = 0;
    while (Wire.available() && i < STATUS_LEN) {
      out[i++] = (uint8_t)Wire.read();
    }

    ok = (i == STATUS_LEN &&
          out[0] == 'C' &&
          out[1] == 'M' &&
          out[2] == PROTO_VERSION);

    if (!ok) {
      // Flush any leftover bytes so the next request starts cleanly.
      while (Wire.available()) (void)Wire.read();
      delay(2);
    }
  }

  i2c_unlock();
  return ok;
}

static bool write_cmd(uint8_t cmd, const uint8_t* payload, size_t payload_len) {
  if (!i2c_lock(60)) return false;
  Wire.beginTransmission(COMPANION_ADDR);
  Wire.write(cmd);
  if (payload && payload_len) Wire.write(payload, payload_len);
  uint8_t rc = Wire.endTransmission(true);
  i2c_unlock();
  return rc == 0;
}

bool companion_probe() {
  const uint32_t now = millis();
  if (now - g_last_probe_ms < PROBE_INTERVAL_MS) return g_present;
  g_last_probe_ms = now;

  uint8_t frame[STATUS_LEN];
  if (read_status_frame(frame)) {
    g_present = true;
    g_last_ok_ms = now;
  } else if (g_last_ok_ms && (now - g_last_ok_ms) > PRESENT_GRACE_MS) {
    g_present = false;
  }
  return g_present;
}

bool companion_present() {
  return g_present;
}

bool companion_read_status(CompanionStatus* out) {
  if (!out) return false;

  uint8_t frame[STATUS_LEN];
  if (!read_status_frame(frame)) {
    const uint32_t now = millis();
    if (g_last_ok_ms && (now - g_last_ok_ms) > PRESENT_GRACE_MS) g_present = false;
    return false;
  }

  g_present = true;
  g_last_ok_ms = millis();

  const uint8_t flags = frame[3];
  out->present = true;
  out->proto_version = frame[2];
  out->monitor_active = (flags & 0x01) != 0;
  out->target_set     = (flags & 0x02) != 0;
  out->pmkid_found    = (flags & 0x04) != 0;
  out->channel        = frame[4];
  memcpy(out->target_bssid, &frame[5], 6);
  memcpy(out->sta_mac,      &frame[11], 6);
  memcpy(out->pmkid,        &frame[17], 16);
  out->last_rssi = (int8_t)frame[33];
  return true;
}

bool companion_set_target(uint8_t channel, const uint8_t bssid[6]) {
  if (!bssid) return false;
  uint8_t payload[7];
  payload[0] = channel;
  memcpy(&payload[1], bssid, 6);
  return write_cmd(CMD_SET_TARGET, payload, sizeof(payload));
}

bool companion_start_pmkid() {
  return write_cmd(CMD_START_PMKID, nullptr, 0);
}

bool companion_stop_all() {
  return write_cmd(CMD_STOP_ALL, nullptr, 0);
}

bool companion_clear_result() {
  return write_cmd(CMD_CLEAR_RESULT, nullptr, 0);
}
