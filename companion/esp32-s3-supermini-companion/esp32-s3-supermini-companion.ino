/*
  ESP32-S3 Super Mini Companion (I2C slave)

  Purpose:
    - Provide a small companion MCU that can stay channel-locked and monitor for PMKID
      while the main CYD ESP32-S3 continues UI/SD work.

  Notes:
    - This sketch is intended for authorized auditing/diagnostics only.
    - The companion is an I2C *slave* and cannot initiate transfers; the main MCU must poll.
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

extern "C" {
#include <esp_wifi.h>
#include <esp_wifi_types.h>
}

// ----------------------------
// Configuration
// ----------------------------
static constexpr uint8_t  COMPANION_I2C_ADDR = 0x42;
static constexpr uint32_t COMPANION_I2C_HZ   = 100000;

// Suggested Super Mini wiring (adjust if your board breaks out different pins):
//   CYD SDA (GPIO16) -> Super Mini SDA (GPIO8)
//   CYD SCL (GPIO15) -> Super Mini SCL (GPIO9)
//   GND <-> GND
static constexpr int COMPANION_SDA_PIN = 8;
static constexpr int COMPANION_SCL_PIN = 9;

static constexpr uint8_t PROTO_VERSION = 1;
static constexpr size_t  I2C_TX_SIZE   = 64;

// ----------------------------
// Protocol
// ----------------------------
// Commands are written as: [cmd][payload...]
enum CompanionCmd : uint8_t {
  CMD_GET_STATUS   = 0x01, // no payload (optional; status is always available via read)
  CMD_SET_TARGET   = 0x02, // payload: [channel:1][bssid:6]
  CMD_START_PMKID  = 0x03, // no payload
  CMD_STOP_ALL     = 0x04, // no payload
  CMD_CLEAR_RESULT = 0x05, // no payload
  CMD_SET_CHANNEL  = 0x06  // payload: [channel:1]
};

// Status response (64 bytes):
//   [0]  'C'
//   [1]  'M'
//   [2]  PROTO_VERSION
//   [3]  flags bitmask
//        bit0: monitor_active
//        bit1: target_set
//        bit2: pmkid_found
//   [4]  channel (1-14)
//   [5..10]  target_bssid (6)
//   [11..16] sta_mac (6)
//   [17..32] pmkid (16)
//   [33] last_rssi (int8_t, 2's complement)
//   [34..63] reserved (0)

// ----------------------------
// Shared state (small + IRQ-safe)
// ----------------------------
static volatile bool    g_monitor_active = false;
static volatile bool    g_target_set     = false;
static volatile bool    g_pmkid_found    = false;
static volatile uint8_t g_channel        = 1;
static volatile int8_t  g_last_rssi      = 0;

static uint8_t g_target_bssid[6] = {0};
static uint8_t g_sta_mac[6]      = {0};
static uint8_t g_pmkid[16]       = {0};

static volatile bool g_stop_requested = false;

// I2C TX buffer is served directly from onRequest(); protect it from partial updates.
static uint8_t g_i2c_tx[I2C_TX_SIZE] = {0};
static portMUX_TYPE g_i2c_tx_mux = portMUX_INITIALIZER_UNLOCKED;

// Pending command capture (filled in onReceive, processed in loop).
struct PendingCmd {
  volatile bool pending;
  uint8_t cmd;
  uint8_t payload[16];
  uint8_t len;
};
static PendingCmd g_pending = {false, 0, {0}, 0};
static portMUX_TYPE g_pending_mux = portMUX_INITIALIZER_UNLOCKED;

// ----------------------------
// Helpers (PMKID extraction)
// ----------------------------
static inline __attribute__((always_inline)) uint16_t read_be16(const uint8_t* p) {
  return ((uint16_t)p[0] << 8) | p[1];
}

static inline __attribute__((always_inline)) uint16_t read_le16(const uint8_t* p) {
  return ((uint16_t)p[1] << 8) | p[0];
}

static inline __attribute__((always_inline)) bool extract_pmkid_from_rsn_ie(const uint8_t* ie, size_t ie_total_len, uint8_t out_pmkid[16]) {
  if (!ie || ie_total_len < 2) return false;
  if (ie[0] != 0x30) return false; // RSN IE

  const uint8_t ie_len = ie[1];
  if ((size_t)ie_len + 2 > ie_total_len) return false;

  const uint8_t* p = ie + 2;
  size_t remaining = ie_len;

  // Version (2) + Group Cipher (4) + Pairwise Count (2) + AKM Count (2) + RSN Caps (2) + PMKID Count (2)
  if (remaining < 2 + 4 + 2) return false;

  p += 2; remaining -= 2; // version
  p += 4; remaining -= 4; // group cipher suite

  if (remaining < 2) return false;
  uint16_t pairwise_count = read_le16(p);
  p += 2; remaining -= 2;
  if (pairwise_count > remaining / 4) return false;
  size_t pairwise_bytes = (size_t)pairwise_count * 4;
  p += pairwise_bytes; remaining -= pairwise_bytes;

  if (remaining < 2) return false;
  uint16_t akm_count = read_le16(p);
  p += 2; remaining -= 2;
  if (akm_count > remaining / 4) return false;
  size_t akm_bytes = (size_t)akm_count * 4;
  p += akm_bytes; remaining -= akm_bytes;

  if (remaining < 2) return false;
  p += 2; remaining -= 2; // RSN capabilities

  if (remaining < 2) return false;
  uint16_t pmkid_count = read_le16(p);
  p += 2; remaining -= 2;

  if (pmkid_count < 1) return false;
  if (pmkid_count > remaining / 16) return false;

  memcpy(out_pmkid, p, 16);
  return true;
}

// ----------------------------
// WiFi promiscuous callback
// ----------------------------
static void IRAM_ATTR pmkid_promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf) return;
  if (!g_monitor_active) return;
  if (g_pmkid_found) return;
  if (!g_target_set) return;
  if (type != WIFI_PKT_DATA) return;

  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* frame = pkt->payload;
  const uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 24 + 8 + 4 + 95) return;

  const uint16_t fc = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
  const uint8_t frame_type = (fc >> 2) & 0x3;
  if (frame_type != 2) return; // data

  const bool to_ds = (fc & 0x0100) != 0;
  const bool from_ds = (fc & 0x0200) != 0;
  if (to_ds || !from_ds) return; // AP->STA frames (FromDS=1, ToDS=0)

  // Addr2 is BSSID for FromDS frames
  if (memcmp(frame + 10, g_target_bssid, 6) != 0) return;

  size_t hdr_len = 24;
  const uint8_t subtype = (fc >> 4) & 0xF;
  if ((subtype & 0x08) != 0) hdr_len += 2; // QoS control
  if ((fc & 0x8000) != 0) hdr_len += 4;    // HT control
  if (hdr_len + 8 + 4 > len) return;

  const size_t llc_off = hdr_len;
  if (frame[llc_off] != 0xAA || frame[llc_off + 1] != 0xAA || frame[llc_off + 2] != 0x03) return;
  if (frame[llc_off + 3] != 0x00 || frame[llc_off + 4] != 0x00 || frame[llc_off + 5] != 0x00) return;
  if (frame[llc_off + 6] != 0x88 || frame[llc_off + 7] != 0x8E) return; // 802.1X / EAPOL

  const size_t eapol_off = llc_off + 8;
  const uint8_t eapol_type = frame[eapol_off + 1];
  if (eapol_type != 3) return; // EAPOL-Key only

  const uint16_t eapol_len = read_be16(frame + eapol_off + 2);
  if (eapol_len < 95) return;
  if (eapol_off + 4 + eapol_len > len) return;

  const size_t key_off = eapol_off + 4;
  const uint16_t key_info = read_be16(frame + key_off + 1);

  const bool key_ack = (key_info & 0x0080) != 0;
  const bool install = (key_info & 0x0040) != 0;
  const bool key_mic = (key_info & 0x0100) != 0;
  const bool enc_kd  = (key_info & 0x1000) != 0;
  if (!key_ack || install || key_mic || enc_kd) return; // expected M1

  const uint16_t kd_len = read_be16(frame + key_off + 93);
  const size_t kd_off = key_off + 95;
  if (kd_off + kd_len > eapol_off + 4 + eapol_len) return;

  const uint8_t* key_data = frame + kd_off;
  const size_t key_data_len = kd_len;

  uint8_t pmkid[16] = {0};
  bool got_pmkid = false;

  for (size_t i = 0; i + 2 <= key_data_len;) {
    const uint8_t id = key_data[i];
    const uint8_t l  = key_data[i + 1];
    const size_t total = 2 + (size_t)l;
    if (i + total > key_data_len) break;

    if (id == 0x30) { // RSN IE
      got_pmkid = extract_pmkid_from_rsn_ie(key_data + i, total, pmkid);
      break;
    }
    i += total;
  }

  if (!got_pmkid) return;

  memcpy(g_pmkid, pmkid, sizeof(g_pmkid));
  memcpy(g_sta_mac, frame + 4, sizeof(g_sta_mac)); // Addr1 (DA) = STA MAC
  g_last_rssi = pkt->rx_ctrl.rssi;

  g_pmkid_found = true;
  g_stop_requested = true;
}

// ----------------------------
// WiFi control
// ----------------------------
static void apply_channel(uint8_t ch) {
  if (ch < 1 || ch > 14) return;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

static void stop_all() {
  esp_wifi_set_promiscuous(false);
  g_monitor_active = false;
}

static void start_pmkid_monitor() {
  if (!g_target_set) return;

  g_pmkid_found = false;
  memset(g_pmkid, 0, sizeof(g_pmkid));
  memset(g_sta_mac, 0, sizeof(g_sta_mac));
  g_last_rssi = 0;
  g_stop_requested = false;

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);

  apply_channel(g_channel);
  esp_wifi_set_promiscuous(true);
  g_monitor_active = true;
}

// ----------------------------
// I2C callbacks
// ----------------------------
static void on_i2c_receive(int nbytes) {
  if (nbytes <= 0) return;

  uint8_t buf[1 + sizeof(g_pending.payload)] = {0};
  size_t r = 0;
  while (Wire.available() && r < sizeof(buf)) {
    buf[r++] = (uint8_t)Wire.read();
  }
  if (r < 1) return;

  portENTER_CRITICAL(&g_pending_mux);
  g_pending.cmd = buf[0];
  g_pending.len = (uint8_t)((r > 1) ? (r - 1) : 0);
  if (g_pending.len > sizeof(g_pending.payload)) g_pending.len = sizeof(g_pending.payload);
  if (g_pending.len > 0) memcpy(g_pending.payload, &buf[1], g_pending.len);
  g_pending.pending = true;
  portEXIT_CRITICAL(&g_pending_mux);
}

static void on_i2c_request() {
  portENTER_CRITICAL(&g_i2c_tx_mux);
  Wire.write(g_i2c_tx, sizeof(g_i2c_tx));
  portEXIT_CRITICAL(&g_i2c_tx_mux);
}

static void update_i2c_tx() {
  uint8_t tmp[I2C_TX_SIZE] = {0};
  tmp[0] = 'C';
  tmp[1] = 'M';
  tmp[2] = PROTO_VERSION;

  uint8_t flags = 0;
  if (g_monitor_active) flags |= 0x01;
  if (g_target_set)     flags |= 0x02;
  if (g_pmkid_found)    flags |= 0x04;
  tmp[3] = flags;
  tmp[4] = g_channel;

  memcpy(&tmp[5],  g_target_bssid, 6);
  memcpy(&tmp[11], g_sta_mac,      6);
  memcpy(&tmp[17], g_pmkid,        16);
  tmp[33] = (uint8_t)g_last_rssi;

  portENTER_CRITICAL(&g_i2c_tx_mux);
  memcpy(g_i2c_tx, tmp, sizeof(tmp));
  portEXIT_CRITICAL(&g_i2c_tx_mux);
}

// ----------------------------
// Arduino entry points
// ----------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // Bring up WiFi stack (promiscuous stays disabled until started)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  esp_wifi_set_promiscuous_rx_cb(&pmkid_promisc_cb);
  esp_wifi_set_promiscuous(false);

  // Start I2C in slave mode
  bool ok = Wire.begin(COMPANION_I2C_ADDR, COMPANION_SDA_PIN, COMPANION_SCL_PIN, COMPANION_I2C_HZ);
  if (!ok) {
    Serial.println("[COMPANION] I2C slave start failed");
  } else {
    Wire.onReceive(on_i2c_receive);
    Wire.onRequest(on_i2c_request);
    Serial.printf("[COMPANION] I2C slave ready addr=0x%02X SDA=%d SCL=%d\n",
                  COMPANION_I2C_ADDR, COMPANION_SDA_PIN, COMPANION_SCL_PIN);
  }

  update_i2c_tx();
}

static void handle_cmd(uint8_t cmd, const uint8_t* payload, uint8_t len) {
  switch (cmd) {
    case CMD_GET_STATUS:
      // No-op; status is always available through reads.
      break;

    case CMD_SET_TARGET:
      if (len >= 7) {
        const uint8_t ch = payload[0];
        if (ch >= 1 && ch <= 14) g_channel = ch;
        memcpy(g_target_bssid, &payload[1], 6);
        g_target_set = true;
      }
      break;

    case CMD_SET_CHANNEL:
      if (len >= 1) {
        const uint8_t ch = payload[0];
        if (ch >= 1 && ch <= 14) {
          g_channel = ch;
          if (g_monitor_active) apply_channel(ch);
        }
      }
      break;

    case CMD_START_PMKID:
      start_pmkid_monitor();
      break;

    case CMD_STOP_ALL:
      stop_all();
      break;

    case CMD_CLEAR_RESULT:
      g_pmkid_found = false;
      memset(g_pmkid, 0, sizeof(g_pmkid));
      memset(g_sta_mac, 0, sizeof(g_sta_mac));
      g_last_rssi = 0;
      break;

    default:
      break;
  }
}

void loop() {
  // Process pending I2C command (if any)
  uint8_t cmd = 0;
  uint8_t payload[16] = {0};
  uint8_t len = 0;
  bool have = false;

  portENTER_CRITICAL(&g_pending_mux);
  if (g_pending.pending) {
    cmd = g_pending.cmd;
    len = g_pending.len;
    if (len > sizeof(payload)) len = sizeof(payload);
    if (len > 0) memcpy(payload, g_pending.payload, len);
    g_pending.pending = false;
    have = true;
  }
  portEXIT_CRITICAL(&g_pending_mux);

  if (have) {
    handle_cmd(cmd, payload, len);
  }

  // If the monitor found a PMKID, stop promiscuous mode in task context (not ISR).
  if (g_stop_requested) {
    stop_all();
    g_stop_requested = false;
  }

  static uint32_t last_tx_ms = 0;
  if (millis() - last_tx_ms > 50) {
    update_i2c_tx();
    last_tx_ms = millis();
  }

  delay(5);
}

